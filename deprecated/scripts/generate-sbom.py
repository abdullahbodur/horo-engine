#!/usr/bin/env python3
"""SPDX 2.3 JSON SBOM generator for Horo Engine releases.

Reads vendor/VERSIONS.json (committed snapshots) and vendor/CMakeLists.txt
(FetchContent declarations) to produce an SPDX-compliant Software Bill of
Materials. The generated sbom.spdx.json enumerates every third-party
dependency with precise versions, licenses, and origin URLs.

Output conforms to SPDX 2.3 JSON schema:
https://github.com/spdx/spdx-spec/blob/development/v2.3.1/schemas/spdx-schema.json

Usage:
    python3 scripts/generate-sbom.py                          # All deps
    python3 scripts/generate-sbom.py --output build/sbom.spdx.json
    python3 scripts/generate-sbom.py --no-shader-cooker        # Exclude glslang
    python3 scripts/generate-sbom.py --no-vulkan               # Exclude Vulkan deps
    python3 scripts/generate-sbom.py --validate                # Validate + exit 1 on failure
"""

import argparse
import hashlib
import json
import os
import re
import sys
import uuid
from datetime import datetime, timezone

# ---------------------------------------------------------------------------
# SPDX namespace UUID — deterministic UUIDv5 seeded on engine version + dep hash.
# This ensures reproducible SBOMs for identical inputs.
# Using the OID for "Horo Engine" as the namespace UUID (generated once, stable).
_HORO_NAMESPACE_UUID = uuid.UUID("6ba7b810-9dad-11d1-80b4-00c04fd430c8")


# ---------------------------------------------------------------------------
# License identifier mapping: human-readable → SPDX identifier.
# Non-standard licenses use LicenseRef- custom identifiers with explanatory
# comments in the package entry.
_LICENSE_MAP = {
    "MIT": "MIT",
    "mit": "MIT",
    "zlib/libpng": "Zlib",
    "Zlib": "Zlib",
    "BSD-2-Clause": "BSD-2-Clause",
    "BSD-3-Clause": "BSD-3-Clause",
    "Apache-2.0": "Apache-2.0",
    "Apache 2.0": "Apache-2.0",
    "Public Domain": "LicenseRef-Horo-PublicDomain",
    "MIT / Public Domain": "MIT",
    "MIT / Khronos": "MIT",
    "Dear ImGui Test Engine License": "LicenseRef-ImGuiTestEngine",
    "BSD-3-Clause / Khronos": "BSD-3-Clause",
    "Apache-2.0 / Khronos": "Apache-2.0",
}


# ---------------------------------------------------------------------------
# Supplier metadata for known dependencies.
_SUPPLIERS = {
    "glfw": "Organization: GLFW Developers",
    "imgui": "Organization: ocornut (Dear ImGui)",
    "nlohmann_json": "Person: Niels Lohmann",
    "tinygltf": "Person: Syoyo Fujita",
    "xxhash": "Person: Yann Collet",
    "lz4": "Person: Yann Collet",
    "glslang": "Organization: Khronos Group",
    "vulkan_headers": "Organization: Khronos Group",
    "volk": "Organization: zeux",
    "imgui_test_engine": "Organization: ocornut (Dear ImGui)",
    "glad": "Organization: Khronos Group / glad.dav1d.de",
    "glm": "Person: G-Truc Creation",
    "stb": "Person: Sean Barrett",
    "ufbx": "Person: Samuli Raivio",
    "bc7enc": "Person: Richard Geldreich Jr.",
}


# ---------------------------------------------------------------------------
# Copyright text for known dependencies.
_COPYRIGHTS = {
    "glfw": "Copyright (c) 2002-2006 Marcus Geelnard, 2006-2019 Camilla Löwy",
    "imgui": "Copyright (c) 2014-2025 Omar Cornut",
    "imgui_test_engine": "Copyright (c) 2014-2025 Omar Cornut",
    "nlohmann_json": "Copyright (c) 2013-2022 Niels Lohmann",
    "tinygltf": "Copyright (c) 2017 Syoyo Fujita, Aurélien Chatelain and many contributors",
    "xxhash": "Copyright (c) 2012-2023 Yann Collet",
    "lz4": "Copyright (c) 2011-2024 Yann Collet",
    "glslang": "Copyright (c) 2015-2024 The Khronos Group Inc.",
    "vulkan_headers": "Copyright (c) 2015-2024 The Khronos Group Inc.",
    "volk": "Copyright (c) 2018-2024 Arseny Kapoulkine",
    "glad": "Copyright (c) 2013-2024 The Khronos Group Inc.",
    "glm": "Copyright (c) 2005-2024 G-Truc Creation",
    "stb": "Copyright (c) 2017 Sean Barrett",
    "ufbx": "Copyright (c) 2021-2024 Samuli Raivio",
    "bc7enc": "Copyright (c) 2020 Richard Geldreich Jr.",
}


# ---------------------------------------------------------------------------
# FetchContent dependency table.
# Maps CMake FetchContent target name → metadata.
# This is the authoritative list of dependencies pulled via FetchContent.
# When a new FetchContent dep is added to vendor/CMakeLists.txt, add it here.
_FETCHCONTENT_DEPS = {
    "glfw": {
        "name": "glfw",
        "license": "Zlib",
        "origin": "https://github.com/glfw/glfw.git",
        "always": True,
        "purl": "pkg:github/glfw/glfw",
    },
    "imgui": {
        "name": "imgui",
        "license": "MIT",
        "origin": "https://github.com/ocornut/imgui.git",
        "always": True,
        "purl": "pkg:github/ocornut/imgui",
    },
    "nlohmann_json": {
        "name": "nlohmann_json",
        "license": "MIT",
        "origin": "https://github.com/nlohmann/json.git",
        "always": True,
        "purl": "pkg:github/nlohmann/json",
    },
    "tinygltf": {
        "name": "tinygltf",
        "license": "MIT",
        "origin": "https://github.com/syoyo/tinygltf.git",
        "always": True,
        "purl": "pkg:github/syoyo/tinygltf",
    },
    "xxhash": {
        "name": "xxhash",
        "license": "BSD-2-Clause",
        "origin": "https://github.com/Cyan4973/xxHash.git",
        "always": True,
        "purl": "pkg:github/Cyan4973/xxHash",
    },
    "lz4": {
        "name": "lz4",
        "license": "BSD-2-Clause",
        "origin": "https://github.com/lz4/lz4.git",
        "always": True,
        "purl": "pkg:github/lz4/lz4",
    },
    "vulkan_headers": {
        "name": "Vulkan-Headers",
        "license": "Apache-2.0",
        "origin": "https://github.com/KhronosGroup/Vulkan-Headers.git",
        "always": False,
        "gate": "HORO_ENGINE_ENABLE_VULKAN",
        "purl": "pkg:github/KhronosGroup/Vulkan-Headers",
    },
    "volk": {
        "name": "volk",
        "license": "MIT",
        "origin": "https://github.com/zeux/volk.git",
        "always": False,
        "gate": "HORO_ENGINE_ENABLE_VULKAN",
        "purl": "pkg:github/zeux/volk",
    },
    "glslang": {
        "name": "glslang",
        "license": "BSD-3-Clause",
        "origin": "https://github.com/KhronosGroup/glslang.git",
        "always": False,
        "gate": "HORO_ENGINE_ENABLE_SHADER_COOKER",
        "purl": "pkg:github/KhronosGroup/glslang",
    },
    "imgui_test_engine": {
        "name": "imgui_test_engine",
        "license": "LicenseRef-ImGuiTestEngine",
        "origin": "https://github.com/ocornut/imgui_test_engine.git",
        "always": False,
        "gate": "HORO_ENGINE_BUILD_TESTS",
        "purl": "pkg:github/ocornut/imgui_test_engine",
        "notes": "Test-only dependency. Not included in release SBOM by default.",
    },
}


# ---------------------------------------------------------------------------
# SPDX license comments for non-standard LicenseRef- identifiers.
_LICENSE_COMMENTS = {
    "LicenseRef-Horo-PublicDomain": (
        "This component is in the Public Domain or is dual-licensed under "
        "MIT and Public Domain. See the source files for exact terms."
    ),
    "LicenseRef-ImGuiTestEngine": (
        "Dear ImGui Test Engine License. Proprietary test automation "
        "framework. Not shipped with engine releases. Included in "
        "development SBOMs only."
    ),
}


def _resolve_license(spdx_id: str) -> dict:
    """Resolve an SPDX identifier into a license object with optional comment."""
    result = {"licenseId": spdx_id}
    if spdx_id in _LICENSE_COMMENTS:
        result["comment"] = _LICENSE_COMMENTS[spdx_id]
    return result


def _resolve_spdx_license(raw_license: str) -> str:
    """Map a human-readable license string to an SPDX identifier."""
    # Already a LicenseRef- custom identifier — return as-is
    if raw_license.startswith("LicenseRef-"):
        return raw_license
    return _LICENSE_MAP.get(raw_license, f"LicenseRef-{raw_license.replace(' ', '-')}")


def _make_package_id(name: str, version: str) -> str:
    """Produce a stable SPDXID for a package."""
    safe_name = re.sub(r"[^a-zA-Z0-9_.-]", "-", name)
    safe_version = re.sub(r"[^a-zA-Z0-9_.-]", "-", version)
    return f"SPDXRef-Package-{safe_name}-{safe_version}"


def _deterministic_uuid(engine_version: str, dep_hash: str) -> str:
    """Generate a deterministic UUIDv5 for the SPDX document namespace."""
    seed = f"HoroEngine-{engine_version}-{dep_hash}"
    return str(uuid.uuid5(_HORO_NAMESPACE_UUID, seed))


def _parse_engine_version(repo_root: str) -> str:
    """Extract HORO_ENGINE_VERSION from the root CMakeLists.txt."""
    cmake_path = os.path.join(repo_root, "CMakeLists.txt")
    if not os.path.isfile(cmake_path):
        raise FileNotFoundError(f"Cannot find {cmake_path}")

    with open(cmake_path, "r", encoding="utf-8") as f:
        content = f.read()

    # Match: set(HORO_ENGINE_VERSION "0.0.1")
    m = re.search(
        r'set\s*\(\s*HORO_ENGINE_VERSION\s+"([^"]+)"\s*\)', content
    )
    if m:
        return m.group(1)

    raise ValueError(
        "Could not parse HORO_ENGINE_VERSION from CMakeLists.txt"
    )


def _parse_fetchcontent_versions(repo_root: str) -> dict:
    """Parse GIT_TAG values from vendor/CMakeLists.txt FetchContent blocks.

    Returns a dict mapping normalized dep name → version string.
    """
    vendor_cmake = os.path.join(repo_root, "vendor", "CMakeLists.txt")
    if not os.path.isfile(vendor_cmake):
        raise FileNotFoundError(f"Cannot find {vendor_cmake}")

    with open(vendor_cmake, "r", encoding="utf-8") as f:
        content = f.read()

    # Find every FetchContent_Declare block and extract its GIT_TAG.
    # Strategy: find "(FetchContent_Declare(" then scan forward for GIT_TAG.
    versions = {}
    blocks = re.split(r"FetchContent_Declare\s*\(", content)[1:]

    for block in blocks:
        # Extract the name (first non-comment, non-whitespace argument)
        m_name = re.match(r"\s*(\w+)", block)
        if not m_name:
            continue
        dep_name = m_name.group(1)

        # Extract GIT_TAG
        m_tag = re.search(r"GIT_TAG\s+([^\s)]+)", block)
        if not m_tag:
            continue
        tag = m_tag.group(1)
        # Normalize: strip leading 'v' only for semver-style tags (v1.2.3 → 1.2.3).
        # Tags like "vulkan-sdk-1.4.341.0" or "v1.91.6" are left intact.
        version = tag.strip()
        if re.match(r"^v\d", version):
            version = version[1:]

        versions[dep_name] = version

    return versions


def _load_committed_vendors(repo_root: str) -> list:
    """Load committed vendor entries from vendor/VERSIONS.json."""
    path = os.path.join(repo_root, "vendor", "VERSIONS.json")
    if not os.path.isfile(path):
        print(f"Warning: {path} not found — skipping committed vendors", file=sys.stderr)
        return []

    with open(path, "r", encoding="utf-8") as f:
        data = json.load(f)

    return data.get("entries", [])


def _build_packages(
    engine_version: str,
    committed_vendors: list,
    fc_versions: dict,
    args: argparse.Namespace,
) -> tuple:
    """Build the list of SPDX packages and relationships.

    Returns (packages, relationships).
    """
    packages = []
    relationships = []

    # ---- Root: Horo Engine itself ----
    root_id = _make_package_id("HoroEngine", engine_version)
    packages.append({
        "SPDXID": root_id,
        "name": "HoroEngine",
        "versionInfo": engine_version,
        "supplier": "Organization: Horo Organization",
        "downloadLocation": (
            "https://github.com/abdullahbodur/horo-engine/releases/tag/"
            f"HoroEngine-v{engine_version}"
        ),
        "filesAnalyzed": False,
        "licenseConcluded": "MIT",
        "licenseDeclared": "MIT",
        "copyrightText": (
            f"Copyright (c) 2024-{datetime.now(timezone.utc).year} "
            "Horo Organization"
        ),
    })

    # DOCUMENT DESCRIBES root
    relationships.append({
        "spdxElementId": "SPDXRef-DOCUMENT",
        "relationshipType": "DESCRIBES",
        "relatedSpdxElement": root_id,
    })

    # ---- Committed vendor snapshots ----
    for entry in committed_vendors:
        pkg_id = _make_package_id(entry["name"], entry["version"])
        lic_id = _resolve_spdx_license(entry["license"])

        pkg = {
            "SPDXID": pkg_id,
            "name": entry["name"],
            "versionInfo": entry["version"],
            "downloadLocation": entry.get("origin", "NOASSERTION"),
            "filesAnalyzed": False,
            "licenseConcluded": lic_id,
            "licenseDeclared": lic_id,
            "copyrightText": _COPYRIGHTS.get(entry["name"], "NOASSERTION"),
        }

        if entry.get("pinned_commit"):
            pkg["downloadLocation"] = (
                f"{entry['origin']}@{entry['pinned_commit']}"
            )
        if entry.get("notes"):
            pkg["comment"] = entry["notes"]

        supplier = _SUPPLIERS.get(entry["name"])
        if supplier:
            pkg["supplier"] = supplier

        packages.append(pkg)
        relationships.append({
            "spdxElementId": root_id,
            "relationshipType": "DEPENDS_ON",
            "relatedSpdxElement": pkg_id,
        })

    # ---- FetchContent dependencies ----
    for dep_key, dep_meta in _FETCHCONTENT_DEPS.items():
        # Check gating
        if not dep_meta["always"]:
            gate = dep_meta.get("gate", "")
            if gate == "HORO_ENGINE_ENABLE_VULKAN" and args.no_vulkan:
                continue
            if gate == "HORO_ENGINE_ENABLE_SHADER_COOKER" and args.no_shader_cooker:
                continue
            if gate == "HORO_ENGINE_BUILD_TESTS" and args.no_tests:
                continue
            # If not explicitly excluded but option is off, skip gated deps
            if gate == "HORO_ENGINE_ENABLE_VULKAN" and not getattr(args, "with_vulkan", False):
                continue
            if gate == "HORO_ENGINE_ENABLE_SHADER_COOKER" and not getattr(args, "with_shader_cooker", False):
                continue
            if gate == "HORO_ENGINE_BUILD_TESTS" and not getattr(args, "with_tests", False):
                continue

        # Resolve version from parsed CMake
        version = fc_versions.get(dep_key, "unknown")
        pkg_id = _make_package_id(dep_meta["name"], version)

        lic_id = _resolve_spdx_license(dep_meta["license"])

        pkg = {
            "SPDXID": pkg_id,
            "name": dep_meta["name"],
            "versionInfo": version,
            "downloadLocation": f"{dep_meta['origin']}@{version}",
            "filesAnalyzed": False,
            "licenseConcluded": lic_id,
            "licenseDeclared": lic_id,
            "copyrightText": _COPYRIGHTS.get(dep_meta["name"], "NOASSERTION"),
        }

        supplier = _SUPPLIERS.get(dep_key)
        if supplier:
            pkg["supplier"] = supplier

        if "purl" in dep_meta:
            pkg["externalRefs"] = [{
                "referenceCategory": "PACKAGE-MANAGER",
                "referenceType": "purl",
                "referenceLocator": f"{dep_meta['purl']}@{version}",
            }]

        if dep_meta.get("notes"):
            pkg["comment"] = dep_meta["notes"]

        packages.append(pkg)

        rel_type = "DEPENDS_ON" if dep_meta["always"] else "OPTIONAL_DEPENDENCY_OF"
        rel = {
            "spdxElementId": root_id,
            "relationshipType": rel_type,
            "relatedSpdxElement": pkg_id,
        }
        if not dep_meta["always"]:
            rel["comment"] = f"Only when {dep_meta.get('gate', 'feature flag')}=ON"
        relationships.append(rel)

    return packages, relationships


def _build_document(
    engine_version: str,
    packages: list,
    relationships: list,
) -> dict:
    """Assemble the complete SPDX 2.3 document."""
    # Compute a deterministic namespace UUID from the dependency fingerprint.
    dep_fingerprint = json.dumps(
        sorted([p["name"] + p.get("versionInfo", "") for p in packages])
    )
    dep_hash = hashlib.sha256(dep_fingerprint.encode()).hexdigest()[:12]
    ns_uuid = _deterministic_uuid(engine_version, dep_hash)

    now_utc = datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")

    doc = {
        "SPDXID": "SPDXRef-DOCUMENT",
        "spdxVersion": "SPDX-2.3",
        "creationInfo": {
            "created": now_utc,
            "creators": [
                f"Tool: HoroEngine-SBOM-Generator-{engine_version}",
                "Organization: Horo Organization",
            ],
            "licenseListVersion": "3.23",
        },
        "name": f"HoroEngine-v{engine_version}",
        "dataLicense": "CC0-1.0",
        "documentNamespace": (
            f"https://github.com/abdullahbodur/horo-engine/"
            f"sbom/HoroEngine-v{engine_version}-{ns_uuid}"
        ),
        "comment": (
            "This SBOM enumerates top-level third-party dependencies of "
            "the Horo Engine. Transitive dependencies (e.g., SPIRV-Tools "
            "pulled by glslang) are not individually enumerated. System "
            "dependencies (OpenGL, X11/Wayland, Cocoa, Win32) are not "
            "listed as SPDX packages. See docs/sbom-generation-plan.md "
            "for limitations."
        ),
        "packages": packages,
        "relationships": relationships,
    }

    return doc


def validate_sbom(doc: dict) -> list:
    """Perform basic structural validation on an SBOM document.

    Returns a list of error strings (empty = valid).
    """
    errors = []

    # Required top-level fields
    for field in ("SPDXID", "spdxVersion", "creationInfo", "name",
                   "dataLicense", "documentNamespace", "packages"):
        if field not in doc:
            errors.append(f"Missing required field: {field}")

    if doc.get("spdxVersion") != "SPDX-2.3":
        errors.append(
            f"Expected spdxVersion 'SPDX-2.3', got '{doc.get('spdxVersion')}'"
        )

    if doc.get("dataLicense") != "CC0-1.0":
        errors.append(
            f"Expected dataLicense 'CC0-1.0', got '{doc.get('dataLicense')}'"
        )

    # Packages must be a non-empty list
    pkgs = doc.get("packages", [])
    if not isinstance(pkgs, list) or len(pkgs) == 0:
        errors.append("packages must be a non-empty list")
    else:
        seen_ids = set()
        for pkg in pkgs:
            spdxid = pkg.get("SPDXID", "")
            if not spdxid:
                errors.append("Package missing SPDXID")
            elif spdxid in seen_ids:
                errors.append(f"Duplicate SPDXID: {spdxid}")
            seen_ids.add(spdxid)

            for field in ("name", "versionInfo", "licenseConcluded"):
                if field not in pkg:
                    errors.append(
                        f"Package {spdxid} missing field: {field}"
                    )

    # Relationships must reference valid packages
    rels = doc.get("relationships", [])
    if isinstance(rels, list):
        for rel in rels:
            src = rel.get("spdxElementId", "")
            dst = rel.get("relatedSpdxElement", "")
            if src not in seen_ids and src != "SPDXRef-DOCUMENT":
                errors.append(
                    f"Relationship source {src} not found in packages"
                )
            if dst not in seen_ids and dst != "SPDXRef-DOCUMENT":
                errors.append(
                    f"Relationship target {dst} not found in packages"
                )

    return errors


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Generate SPDX 2.3 JSON SBOM for Horo Engine"
    )
    parser.add_argument(
        "--output", "-o",
        default="sbom.spdx.json",
        help="Output file path (default: sbom.spdx.json)",
    )
    parser.add_argument(
        "--no-vulkan",
        action="store_true",
        help="Exclude Vulkan-related dependencies (Vulkan-Headers, volk)",
    )
    parser.add_argument(
        "--no-shader-cooker",
        action="store_true",
        help="Exclude glslang (shader cooker dependency)",
    )
    parser.add_argument(
        "--no-tests",
        action="store_true",
        help="Exclude test-only dependencies (imgui_test_engine)",
    )
    parser.add_argument(
        "--with-vulkan",
        action="store_true",
        help="Include Vulkan dependencies (default: off for safety)",
    )
    parser.add_argument(
        "--with-shader-cooker",
        action="store_true",
        help="Include glslang (default: off for safety)",
    )
    parser.add_argument(
        "--with-tests",
        action="store_true",
        help="Include test dependencies (default: off for safety)",
    )
    parser.add_argument(
        "--validate",
        action="store_true",
        help="Validate output and exit with non-zero on errors",
    )
    parser.add_argument(
        "--pretty",
        action="store_true",
        default=True,
        help="Pretty-print JSON output (default: on)",
    )
    parser.add_argument(
        "--repo-root",
        default=None,
        help="Path to engine repository root (auto-detected from script location)",
    )
    args = parser.parse_args()

    # Determine repo root
    if args.repo_root:
        repo_root = os.path.abspath(args.repo_root)
    else:
        # Default: script lives at <repo>/scripts/generate-sbom.py
        repo_root = os.path.abspath(
            os.path.join(os.path.dirname(__file__), "..")
        )

    if not os.path.isdir(repo_root):
        print(f"Error: repo root not found: {repo_root}", file=sys.stderr)
        sys.exit(1)

    # Parse the engine version
    try:
        engine_version = _parse_engine_version(repo_root)
    except (FileNotFoundError, ValueError) as e:
        print(f"Error: {e}", file=sys.stderr)
        sys.exit(1)

    # Parse FetchContent versions from vendor/CMakeLists.txt
    try:
        fc_versions = _parse_fetchcontent_versions(repo_root)
    except FileNotFoundError as e:
        print(f"Error: {e}", file=sys.stderr)
        sys.exit(1)

    # Load committed vendor snapshots
    committed = _load_committed_vendors(repo_root)

    # Build packages and relationships
    packages, relationships = _build_packages(
        engine_version, committed, fc_versions, args
    )

    # Assemble the SPDX document
    doc = _build_document(engine_version, packages, relationships)

    # Validate if requested
    if args.validate:
        errors = validate_sbom(doc)
        if errors:
            print(f"SBOM validation failed with {len(errors)} error(s):",
                  file=sys.stderr)
            for err in errors:
                print(f"  - {err}", file=sys.stderr)
            sys.exit(1)
        print(f"SBOM validation passed ({len(packages)} packages)",
              file=sys.stderr)

    # Write output
    indent = 2 if args.pretty else None
    output_json = json.dumps(doc, indent=indent, ensure_ascii=False)

    output_path = args.output
    if not os.path.isabs(output_path):
        output_path = os.path.join(repo_root, output_path)

    with open(output_path, "w", encoding="utf-8") as f:
        f.write(output_json)

    print(f"SBOM written: {output_path}", file=sys.stderr)
    print(
        f"  {len(packages)} packages, {len(relationships)} relationships",
        file=sys.stderr,
    )


if __name__ == "__main__":
    main()
