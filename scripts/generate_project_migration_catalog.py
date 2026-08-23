#!/usr/bin/env python3
"""Validate convention-discovered project migrations and generate deterministic build inputs."""

from __future__ import annotations

import hashlib
import json
from pathlib import Path
import re
import sys

SEMVER = re.compile(r"^(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)$")
IDENTITY = re.compile(r"^[a-z][a-z0-9]*(?:[._-][a-z0-9]+)*$")
HASH = re.compile(r"^sha256:[0-9a-f]{64}$")


def fail(message: str) -> None:
    raise SystemExit(f"[project-migrations] {message}")


def version_key(text: str) -> tuple[int, int, int]:
    match = SEMVER.fullmatch(text)
    if match is None:
        fail(f"directory is not canonical stable SemVer: {text!r}")
    return tuple(int(match.group(index)) for index in range(1, 4))


def read_manifest(path: Path) -> dict:
    resolved = path.resolve()
    try:
        if resolved.stat().st_size > 64 * 1024:
            fail(f"manifest exceeds 64 KiB: {resolved}")
        value = json.loads(resolved.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        fail(f"cannot read manifest {resolved}: {error}")
    if not isinstance(value, dict):
        fail(f"manifest must be an object: {resolved}")
    return value


def symbol(version: str) -> str:
    return "R" + version.replace(".", "_")


def normalized_source_hash(directory: Path, manifest_name: str) -> str:
    sources = [directory / manifest_name, directory / "ProjectMigration.h", directory / "ProjectMigration.cpp"]
    stages = sorted((directory / "stages").rglob("*Stage.cpp")) if (directory / "stages").exists() else []
    sources.extend(stages)
    digest = hashlib.sha256()
    for path in sources:
        if not path.is_file():
            fail(f"required migration source is missing: {path}")
        relative = path.relative_to(directory).as_posix().encode("utf-8")
        if path.name == manifest_name:
            manifest = read_manifest(path)
            manifest.pop("definitionHash", None)
            content = json.dumps(manifest, ensure_ascii=False, sort_keys=True,
                                 separators=(",", ":")).encode("utf-8")
        else:
            content = path.read_bytes().replace(b"\r\n", b"\n")
        digest.update(len(relative).to_bytes(4, "big"))
        digest.update(relative)
        digest.update(len(content).to_bytes(8, "big"))
        digest.update(content)
    return "sha256:" + digest.hexdigest()


def _validate_storage_estimate(estimate: object, manifest_path: Path) -> dict:
    if not isinstance(estimate, dict) or set(estimate) != {
            "maximumOutputRatioPermille", "maximumAddedBytesPerDocument", "maximumFixedBytes"}:
        fail(f"manifest storageEstimate is invalid: {manifest_path}")
    for key in ("maximumOutputRatioPermille", "maximumAddedBytesPerDocument", "maximumFixedBytes"):
        val = estimate.get(key)
        if not isinstance(val, int) or val < 0:
            fail(f"manifest storageEstimate.{key} is invalid: {manifest_path}")
    if estimate["maximumOutputRatioPermille"] < 1000:
        fail(f"manifest storage estimate cannot shrink its admission bound: {manifest_path}")
    return estimate


def _validate_entry_baselines(manifest: dict, manifest_path: Path, target: str, source_from_path: str | None) -> tuple[str, str]:
    expected = {"id", "fromBaseline", "toBaseline", "sourceContract", "targetContract", "storageEstimate"}
    if not expected.issubset(manifest):
        fail(f"manifest lacks required fields: {manifest_path}")
    if not isinstance(manifest["id"], str) or IDENTITY.fullmatch(manifest["id"]) is None:
        fail(f"manifest has invalid stable id: {manifest_path}")
    source = manifest["fromBaseline"]
    declared_target = manifest["toBaseline"]
    if not isinstance(source, str) or not isinstance(declared_target, str):
        fail(f"manifest baseline fields must be strings: {manifest_path}")
    version_key(source)
    version_key(declared_target)
    if declared_target != target or (source_from_path is not None and source != source_from_path):
        fail(f"manifest source/target does not match its directory: {manifest_path}")
    if version_key(source) >= version_key(target):
        fail(f"migration edge is not forward-only: {manifest_path}")
    for key in ("sourceContract", "targetContract"):
        if not isinstance(manifest[key], str) or HASH.fullmatch(manifest[key]) is None:
            fail(f"manifest {key} is not canonical sha256: {manifest_path}")
    return source, target


def _validate_entry_sources(directory: Path, manifest: dict, manifest_path: Path, manifest_name: str) -> str:
    definition_hash = normalized_source_hash(directory, manifest_name)
    header = (directory / "ProjectMigration.h").read_text(encoding="utf-8")
    if "BuildProjectMigration" not in header:
        fail(f"standardized BuildProjectMigration factory is missing: {directory / 'ProjectMigration.h'}")
    declared_hash = manifest.get("definitionHash")
    if declared_hash is not None and declared_hash != definition_hash:
        fail(f"frozen definitionHash differs from normalized sources: {manifest_path}")
    return definition_hash


def validate_entry(directory: Path, kind: str, source_dir: str | None = None) -> dict:
    target = directory.name if kind == "sequential" else directory.parent.name
    manifest_name = "migration.horo.json" if kind == "sequential" else "checkpoint.horo.json"
    manifest_path = directory / manifest_name
    manifest = read_manifest(manifest_path)
    source, target = _validate_entry_baselines(manifest, manifest_path, target, source_dir)
    estimate = _validate_storage_estimate(manifest["storageEstimate"], manifest_path)
    definition_hash = _validate_entry_sources(directory, manifest, manifest_path, manifest_name)
    return {
        "directory": directory,
        "kind": kind,
        "id": manifest["id"],
        "source": source,
        "target": target,
        "hash": definition_hash,
        "storageEstimate": estimate,
        "namespace": symbol(target) if kind == "sequential" else f"{symbol(target)}::From{source.replace('.', '_')}",
    }



def discover(root: Path) -> list[dict]:
    entries: list[dict] = []
    definitions = root / "definitions"
    if definitions.exists():
        for target in sorted((item for item in definitions.iterdir() if item.is_dir()), key=lambda item: version_key(item.name)):
            entries.append(validate_entry(target, "sequential"))
    checkpoints = root / "checkpoints"
    if checkpoints.exists():
        for target in sorted((item for item in checkpoints.iterdir() if item.is_dir()), key=lambda item: version_key(item.name)):
            for source in sorted((item for item in target.iterdir() if item.is_dir()), key=lambda item: version_key(item.name)):
                entries.append(validate_entry(source, "checkpoint", source.name))
    identities: set[str] = set()
    edges: set[tuple[str, str, str]] = set()
    for entry in entries:
        if entry["id"] in identities:
            fail(f"duplicate migration identity: {entry['id']}")
        identities.add(entry["id"])
        edge = entry["kind"], entry["source"], entry["target"]
        if edge in edges:
            fail(f"duplicate migration edge: {edge}")
        edges.add(edge)
    return sorted(entries, key=lambda entry: (version_key(entry["target"]), version_key(entry["source"]), entry["kind"], entry["id"]))


def hash_initializer(value: str) -> str:
    return "{" + ",".join(f"0x{value[index:index + 2]}" for index in range(7, len(value), 2)) + "}"


def _build_support_code(current_release_path: Path | None, entries: list[dict]) -> str:
    if current_release_path is None:
        return """
Result<ProjectMigrationSupportDescriptor> BuildBuiltInProjectMigrationSupportDescriptor()
{
    return Result<ProjectMigrationSupportDescriptor>::Failure(
        MakeError(ErrorCodeDescriptor{.domain = ErrorDomainId{"horo.application.project"},
                                      .code = ErrorCode{"project.migration.support_unavailable"},
                                      .summary = "Generated migration support descriptor is unavailable."}));
}
"""
    release = read_manifest(current_release_path)
    required_release_fields = {"horoVersion", "contractBaseline", "persistentContract",
                               "minimumMigratableVersion", "migrationCheckpoints"}
    if not required_release_fields.issubset(release):
        fail(f"current release manifest lacks migration support fields: {current_release_path}")
    for key in ("horoVersion", "contractBaseline", "minimumMigratableVersion"):
        if not isinstance(release[key], str):
            fail(f"current release manifest {key} must be a string")
        version_key(release[key])
    if not isinstance(release["persistentContract"], str) or HASH.fullmatch(release["persistentContract"]) is None:
        fail("current release manifest persistentContract is invalid")
    if not isinstance(release["migrationCheckpoints"], list) or not all(
            isinstance(value, str) for value in release["migrationCheckpoints"]):
        fail("current release manifest migrationCheckpoints is invalid")
    target_entries = [entry for entry in entries if entry["kind"] == "sequential" and
                      entry["target"] == release["contractBaseline"]]
    if len(target_entries) != 1:
        fail("current contract baseline requires exactly one sequential target definition")
    target = target_entries[0]
    checkpoint_lines: list[str] = []
    for checkpoint_id in release["migrationCheckpoints"]:
        matches = [entry for entry in entries if entry["kind"] == "checkpoint" and entry["id"] == checkpoint_id]
        if len(matches) != 1:
            fail(f"declared checkpoint is missing or ambiguous: {checkpoint_id}")
        checkpoint = matches[0]
        checkpoint_lines.append(
            f'    support.checkpoints.push_back({{{{"{checkpoint["id"]}"}}, '
            f'{{ParseHoroVersion("{checkpoint["source"]}").Value()}}, '
            f'{{ParseHoroVersion("{checkpoint["target"]}").Value()}}}});')
    checkpoints = "\n".join(checkpoint_lines)
    return f"""
Result<ProjectMigrationSupportDescriptor> BuildBuiltInProjectMigrationSupportDescriptor()
{{
    auto target = ParseHoroVersion("{release['contractBaseline']}");
    auto minimum = ParseHoroVersion("{release['minimumMigratableVersion']}");
    auto contract = ParsePersistentContractHash("{release['persistentContract']}");
    if (target.HasError() || minimum.HasError() || contract.HasError())
        return Result<ProjectMigrationSupportDescriptor>::Failure(
            target.HasError() ? target.ErrorValue() : minimum.HasError() ? minimum.ErrorValue() : contract.ErrorValue());
    ProjectMigrationSupportDescriptor support{{
        .target = ContractBaselineVersion{{target.Value()}},
        .minimumMigratable = ContractBaselineVersion{{minimum.Value()}},
        .targetContract = contract.Value(),
        .targetValidator = Horo::ProjectMigrations::{target['namespace']}::BuildTargetValidator()
    }};
{checkpoints}
    return Result<ProjectMigrationSupportDescriptor>::Success(std::move(support));
}}
"""


def _build_sources_cmake(entries: list[dict]) -> str:
    sources: list[str] = []
    for entry in entries:
        sources.append((entry["directory"] / "ProjectMigration.cpp").as_posix())
        stages = entry["directory"] / "stages"
        if stages.exists():
            sources.extend(path.as_posix() for path in sorted(stages.rglob("*Stage.cpp")))
    return "set(HORO_PROJECT_MIGRATION_SOURCES\n" + "".join(f'    "{source}"\n' for source in sources) + ")\n"


def main() -> None:
    if len(sys.argv) not in (6, 7):
        fail("usage: generate_project_migration_catalog.py <root> <output.cpp> <sources.cmake> <hash.txt> <catalog.json> [current-release.json]")
    root, output_cpp, output_cmake, output_hash, output_catalog = map(Path, sys.argv[1:6])
    current_release_path = Path(sys.argv[6]) if len(sys.argv) == 7 else None
    entries = discover(root)
    canonical = json.dumps(
        [{key: entry[key] for key in ("kind", "id", "source", "target", "hash", "storageEstimate")} for entry in entries],
        sort_keys=True,
        separators=(",", ":"),
    ).encode("utf-8")
    set_hash = "sha256:" + hashlib.sha256(canonical).hexdigest()

    includes = "\n".join(
        f'#include "src/application/project_migrations/{entry["directory"].relative_to(root).as_posix()}/ProjectMigration.h"'
        for entry in entries
    )
    calls = "\n".join(
        f"    {{ auto built = Horo::ProjectMigrations::{entry['namespace']}::BuildProjectMigration(); "
        "if (built.HasError()) return Horo::Result<std::vector<Horo::Application::ProjectMigrationDefinition>>::Failure(built.ErrorValue()); "
        "auto definition = std::move(built).Value(); "
        f"definition.hash.bytes = {hash_initializer(entry['hash'])}; "
        f"definition.storageEstimate = {{{entry['storageEstimate']['maximumOutputRatioPermille']}U, "
        f"{entry['storageEstimate']['maximumAddedBytesPerDocument']}ULL, {entry['storageEstimate']['maximumFixedBytes']}ULL}}; "
        "definitions.push_back(std::move(definition)); }"
        for entry in entries
    )
    support = _build_support_code(current_release_path, entries)

    generated = f"""// AUTO-GENERATED by generate_project_migration_catalog.py. DO NOT EDIT.
#include \"Horo/Application/ProjectMigrationCatalog.h\"
{includes}

namespace Horo::Application
{{
Result<std::vector<ProjectMigrationDefinition>> BuildBuiltInProjectMigrationCatalog()
{{
    std::vector<ProjectMigrationDefinition> definitions;
    definitions.reserve({len(entries)});
{calls}
    return Result<std::vector<ProjectMigrationDefinition>>::Success(std::move(definitions));
}}
{support}
}} // namespace Horo::Application
"""
    cmake = _build_sources_cmake(entries)
    catalog_json = json.dumps(
        [{key: entry[key] for key in ("kind", "id", "source", "target", "hash", "storageEstimate")} for entry in entries],
        ensure_ascii=False,
        sort_keys=True,
        separators=(",", ":"),
    ) + "\n"
    for path, content in ((output_cpp, generated), (output_cmake, cmake),
                          (output_hash, set_hash + "\n"), (output_catalog, catalog_json)):
        resolved_path = path.resolve()
        resolved_path.parent.mkdir(parents=True, exist_ok=True)
        if not resolved_path.exists() or resolved_path.read_text(encoding="utf-8") != content:
            resolved_path.write_text(content, encoding="utf-8")


if __name__ == "__main__":
    main()

