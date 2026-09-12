#!/usr/bin/env python3
"""Generate a portable native Horo extension project from public SDK contracts."""

from __future__ import annotations

import argparse
import json
import re
import shutil
import tempfile
from dataclasses import dataclass
from pathlib import Path


IDENTIFIER_PATTERN = re.compile(r"^[a-z][a-z0-9]*(?:\.[a-z][a-z0-9-]*)+$")
VERSION_PATTERN = re.compile(r"^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$")


@dataclass(frozen=True)
class Module:
    suffix: str
    roles: tuple[str, ...]
    exports_service: bool = False
    imports_backend: bool = False


SHAPES = {
    "gui": (Module("editor", ("editor-presentation",)),),
    "backend": (Module("backend", ("backend-capability", "headless-tooling"), True),),
    "script": (Module("script", ("backend-capability", "script-provider"), True),),
    "hybrid": (
        Module("backend", ("backend-capability", "headless-tooling"), True),
        Module("editor", ("editor-presentation",), imports_backend=True),
        Module("script", ("script-provider",), imports_backend=True),
    ),
}


MODULE_SOURCE = r'''#include "Horo/Extensions/ExtensionAbi.h"

#include <stddef.h>

static const char module_id[] = "@MODULE_ID@";
static const char module_version[] = "@VERSION@";

HORO_EXTENSION_EXPORT HoroExtensionStatus horo_extension_query(HoroExtensionRequirements *requirements) {
    if (requirements == NULL || requirements->structSize < sizeof(*requirements))
        return HORO_EXTENSION_ERROR_INVALID_ARGS;
    *requirements = (HoroExtensionRequirements){
        .structSize = sizeof(*requirements),
        .abiMajorVersion = HORO_EXTENSION_ABI_VERSION,
        .minimumHostMinor = HORO_EXTENSION_ABI_MINOR_VERSION,
        .requiredHostApiSize = sizeof(HoroExtensionHostApi),
    };
    return HORO_EXTENSION_SUCCESS;
}

HORO_EXTENSION_EXPORT HoroExtensionStatus horo_extension_load(const HoroExtensionHostApi *host, HoroExtensionModuleApi *module) {
    if (host == NULL || module == NULL || module->structSize < sizeof(*module) ||
        host->structSize < sizeof(*host) || host->abiVersion != HORO_EXTENSION_ABI_VERSION)
        return HORO_EXTENSION_ERROR_VERSION_MISMATCH;
    *module = (HoroExtensionModuleApi){
        .structSize = sizeof(*module),
        .moduleId = {module_id, sizeof(module_id) - 1},
        .moduleVersion = {module_version, sizeof(module_version) - 1},
    };
    return HORO_EXTENSION_SUCCESS;
}

HORO_EXTENSION_EXPORT void horo_extension_unload(HoroExtensionModuleApi *module) {
    if (module != NULL)
        *module = (HoroExtensionModuleApi){0};
}
'''


CONTRACT_TEST = r'''#include "Horo/Extensions/ExtensionAbi.h"

#include <stddef.h>
#include <string.h>

HoroExtensionStatus horo_extension_query(HoroExtensionRequirements *requirements);
HoroExtensionStatus horo_extension_load(const HoroExtensionHostApi *host, HoroExtensionModuleApi *module);
void horo_extension_unload(HoroExtensionModuleApi *module);

int main(void) {
    static const char expected_id[] = "@MODULE_ID@";
    HoroExtensionRequirements requirements = {.structSize = sizeof(requirements)};
    if (horo_extension_query(&requirements) != HORO_EXTENSION_SUCCESS ||
        requirements.abiMajorVersion != HORO_EXTENSION_ABI_VERSION ||
        requirements.minimumHostMinor > HORO_EXTENSION_ABI_MINOR_VERSION)
        return 1;
    const HoroExtensionHostApi host = {
        .structSize = sizeof(host),
        .abiVersion = HORO_EXTENSION_ABI_VERSION,
        .abiMinorVersion = HORO_EXTENSION_ABI_MINOR_VERSION,
    };
    HoroExtensionModuleApi module = {.structSize = sizeof(module)};
    if (horo_extension_load(&host, &module) != HORO_EXTENSION_SUCCESS ||
        module.moduleId.length != sizeof(expected_id) - 1 ||
        memcmp(module.moduleId.data, expected_id, sizeof(expected_id) - 1) != 0)
        return 2;
    horo_extension_unload(&module);
    return module.moduleContext == NULL ? 0 : 3;
}
'''


def module_id(package_id: str, module: Module) -> str:
    return f"{package_id}.{module.suffix}"


def target_name(package_id: str, module: Module) -> str:
    return f"{re.sub(r'[^A-Za-z0-9]', '_', package_id)}_{module.suffix}"


def render(text: str, replacements: dict[str, str]) -> str:
    for key, value in replacements.items():
        text = text.replace(f"@{key}@", value)
    return text


def manifest(package_id: str, name: str, version: str, modules: tuple[Module, ...]) -> dict:
    service_id = f"{package_id}.service"
    backend_id = f"{package_id}.backend"
    module_values = []
    for module in modules:
        value = {
            "id": module_id(package_id, module),
            "version": version,
            "kind": "native",
            "entry": target_name(package_id, module),
            "roles": list(module.roles),
            "abi": {"major": 1, "minimumMinor": 1},
        }
        if module.exports_service:
            value["exports"] = [{"id": service_id, "contract": service_id, "version": version}]
        if module.imports_backend:
            value["dependencies"] = [backend_id]
            value["imports"] = [{
                "id": f"{module_id(package_id, module)}.backend",
                "service": service_id,
                "contract": service_id,
                "minimumVersion": version,
            }]
        module_values.append(value)
    return {
        "schemaVersion": 1,
        "id": package_id,
        "displayName": name,
        "version": version,
        "modules": module_values,
        "contributions": [],
    }


def cmake_project(package_id: str, version: str, modules: tuple[Module, ...]) -> str:
    lines = [
        "cmake_minimum_required(VERSION 3.25)",
        f"project(HoroExtension VERSION {version} LANGUAGES C)",
        "find_package(HoroEngineExtensionSdk CONFIG REQUIRED)",
        "include(CTest)",
        "",
    ]
    for module in modules:
        target = target_name(package_id, module)
        source = f"src/{module.suffix}.c"
        test_target = f"{target}_contract_test"
        lines.extend([
            f"add_library({target} MODULE {source})",
            f"target_compile_features({target} PRIVATE c_std_11)",
            f"target_link_libraries({target} PRIVATE HoroEngine::ExtensionSdk)",
            f"set_target_properties({target} PROPERTIES PREFIX \"\" C_EXTENSIONS OFF C_VISIBILITY_PRESET hidden)",
            f"add_executable({test_target} tests/{module.suffix}_contract.c {source})",
            f"target_compile_features({test_target} PRIVATE c_std_11)",
            f"target_link_libraries({test_target} PRIVATE HoroEngine::ExtensionSdk)",
            f"set_target_properties({test_target} PROPERTIES C_EXTENSIONS OFF)",
            f"add_test(NAME {test_target} COMMAND {test_target})",
            f"install(TARGETS {target} DESTINATION bin)",
            "",
        ])
    lines.extend([
        "install(FILES extension.json DESTINATION .)",
        f'set(CPACK_PACKAGE_NAME "{package_id}")',
        f'set(CPACK_PACKAGE_VERSION "{version}")',
        f'set(CPACK_PACKAGE_FILE_NAME "{package_id}-{version}")',
        'set(CPACK_GENERATOR "ZIP")',
        "set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY OFF)",
        "include(CPack)",
        "",
    ])
    return "\n".join(lines)


def write_project(root: Path, package_id: str, name: str, version: str, shape: str) -> None:
    modules = SHAPES[shape]
    (root / "src").mkdir(parents=True)
    (root / "tests").mkdir()
    (root / "CMakeLists.txt").write_text(cmake_project(package_id, version, modules), encoding="utf-8")
    (root / "extension.json").write_text(
        json.dumps(manifest(package_id, name, version, modules), indent=2) + "\n", encoding="utf-8"
    )
    for module in modules:
        replacements = {"MODULE_ID": module_id(package_id, module), "VERSION": version}
        (root / "src" / f"{module.suffix}.c").write_text(render(MODULE_SOURCE, replacements), encoding="utf-8")
        (root / "tests" / f"{module.suffix}_contract.c").write_text(render(CONTRACT_TEST, replacements), encoding="utf-8")
    (root / "README.md").write_text(
        f"# {name}\n\nGenerated `{shape}` Horo extension scaffold.\n\n"
        "Configure with `HoroEngineExtensionSdk_DIR` pointing to the SDK's "
        "`lib/cmake/HoroEngineExtensionSdk` directory. Build, run CTest, then build "
        "the `package` target to create the ZIP artifact.\n",
        encoding="utf-8",
    )


def validate(args: argparse.Namespace) -> None:
    if not IDENTIFIER_PATTERN.fullmatch(args.id):
        raise ValueError("--id must be a lowercase reverse-domain identifier")
    if not VERSION_PATTERN.fullmatch(args.version):
        raise ValueError("--version must be a canonical major.minor.patch version")
    if not args.name.strip():
        raise ValueError("--name must not be empty")
    if args.output.exists() and any(args.output.iterdir()):
        raise ValueError("--output must not already contain files")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--shape", choices=tuple(SHAPES), required=True)
    parser.add_argument("--id", required=True)
    parser.add_argument("--name", required=True)
    parser.add_argument("--version", default="1.0.0")
    parser.add_argument("--output", type=Path, required=True)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    try:
        validate(args)
        output = args.output.absolute()
        output.parent.mkdir(parents=True, exist_ok=True)
        staging = Path(tempfile.mkdtemp(prefix=f".{output.name}.", dir=output.parent))
        try:
            write_project(staging, args.id, args.name.strip(), args.version, args.shape)
            if output.exists():
                output.rmdir()
            staging.replace(output)
        except Exception:
            shutil.rmtree(staging, ignore_errors=True)
            raise
    except (OSError, ValueError) as error:
        print(f"error: {error}")
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
