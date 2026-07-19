#!/usr/bin/env python3
"""Validate committed release contracts and generate the built-in compatibility catalog."""

from __future__ import annotations

from functools import cmp_to_key
import hashlib
import json
from pathlib import Path
import re
import sys

SEMVER = re.compile(
    r"^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)"
    r"(?:-([0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*))?$"
)


def fail(message: str) -> None:
    raise SystemExit(f"[project-compatibility] {message}")


def parse_version(text: str) -> tuple[int, int, int, tuple[str, ...]]:
    match = SEMVER.fullmatch(text)
    if match is None or len(text.encode("utf-8")) > 64:
        fail(f"release directory is not canonical SemVer: {text!r}")
    prerelease = tuple(match.group(4).split(".")) if match.group(4) else ()
    for identifier in prerelease:
        if identifier.isdigit() and len(identifier) > 1 and identifier[0] == "0":
            fail(f"release has a leading-zero prerelease identifier: {text!r}")
    return int(match.group(1)), int(match.group(2)), int(match.group(3)), prerelease


def compare_version(left: tuple[int, int, int, tuple[str, ...]],
                    right: tuple[int, int, int, tuple[str, ...]]) -> int:
    if left[:3] != right[:3]:
        return -1 if left[:3] < right[:3] else 1
    left_pre, right_pre = left[3], right[3]
    if not left_pre or not right_pre:
        return (not left_pre) - (not right_pre)
    for left_id, right_id in zip(left_pre, right_pre):
        if left_id == right_id:
            continue
        left_numeric, right_numeric = left_id.isdigit(), right_id.isdigit()
        if left_numeric != right_numeric:
            return -1 if left_numeric else 1
        if left_numeric:
            return -1 if int(left_id) < int(right_id) else 1
        return -1 if left_id < right_id else 1
    return (len(left_pre) > len(right_pre)) - (len(left_pre) < len(right_pre))


def read_json(path: Path, label: str) -> dict:
    try:
        document = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        fail(f"cannot read {label} {path}: {error}")
    if not isinstance(document, dict):
        fail(f"{label} must be a JSON object: {path}")
    return document


def canonical_hash(document: dict, omit_release_marker: bool = False) -> str:
    payload = dict(document)
    if omit_release_marker:
        payload.pop("horoVersion", None)
    canonical = json.dumps(payload, ensure_ascii=False, sort_keys=True, separators=(",", ":")).encode("utf-8")
    return f"sha256:{hashlib.sha256(canonical).hexdigest()}"


def main() -> None:
    if len(sys.argv) not in (4, 5, 6):
        fail("usage: generate_project_compatibility.py <releases-root> <engine-version> <output.h> [migration-set-hash] [migration-catalog.json]")

    releases_root = Path(sys.argv[1])
    engine_version = sys.argv[2]
    output = Path(sys.argv[3])
    current_parsed = parse_version(engine_version)
    definitions_hash = (sys.argv[4] if len(sys.argv) >= 5
                        else f"sha256:{hashlib.sha256(b'[]').hexdigest()}")
    if re.fullmatch(r"sha256:[0-9a-f]{64}", definitions_hash) is None:
        fail("migration definition-set hash is not canonical SHA-256")
    migration_catalog: list[dict] = []
    if len(sys.argv) == 6:
        try:
            migration_catalog = json.loads(Path(sys.argv[5]).read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError) as error:
            fail(f"cannot read generated migration catalog: {error}")
        if not isinstance(migration_catalog, list):
            fail("generated migration catalog must be an array")

    records: list[dict] = []
    for directory in releases_root.iterdir():
        if not directory.is_dir():
            continue
        parsed = parse_version(directory.name)
        if compare_version(parsed, current_parsed) > 0:
            continue
        manifest = read_json(directory / "release.json", "release manifest")
        contract = read_json(directory / "project-contract.json", "project contract")
        recovery_contract = read_json(directory / "migration-recovery-contract.json", "migration recovery contract")
        if manifest.get("horoVersion") != directory.name or contract.get("horoVersion") != directory.name:
            fail(f"release directory, manifest, and contract versions differ in {directory}")
        baseline = manifest.get("contractBaseline")
        if not isinstance(baseline, str):
            fail(f"release manifest lacks canonical contractBaseline: {directory / 'release.json'}")
        records.append({
            "release": directory.name,
            "parsed": parsed,
            "baseline": baseline,
            "baselineParsed": parse_version(baseline),
            "contract": canonical_hash(contract, omit_release_marker=True),
            "recoveryContract": canonical_hash(recovery_contract),
            "manifest": manifest,
            "definitions": manifest.get(
                "migrationDefinitions", definitions_hash if directory.name == engine_version
                else f"sha256:{hashlib.sha256(b'[]').hexdigest()}"),
        })

    records.sort(key=cmp_to_key(lambda left, right: compare_version(left["parsed"], right["parsed"])))
    by_release = {record["release"]: record for record in records}
    if engine_version not in by_release:
        fail(f"no committed release decision exists for CMake PROJECT_VERSION {engine_version}")

    for index, record in enumerate(records):
        baseline = by_release.get(record["baseline"])
        if baseline is None or compare_version(record["baselineParsed"], record["parsed"]) > 0:
            fail(f"release {record['release']} references a missing or future baseline {record['baseline']}")
        if baseline["baseline"] != baseline["release"] or baseline["contract"] != record["contract"]:
            fail(f"release {record['release']} does not match baseline contract {record['baseline']}")
        for prior in records[:index]:
            if not record["parsed"][3] and not prior["parsed"][3] and record["parsed"][:2] == prior["parsed"][:2]:
                if (prior["baseline"] != record["baseline"] or
                        prior["contract"] != record["contract"] or
                        prior["definitions"] != record["definitions"]):
                    fail(f"patch release {record['release']} changes the frozen release-line contract")
        decision = {
            "contractBaseline": record["baseline"],
            "migrationDefinitions": record["definitions"],
            "persistentContract": record["contract"],
            "release": record["release"],
        }
        record["decisionHash"] = canonical_hash(decision)
        frozen_contract = record["manifest"].get("persistentContract")
        frozen_recovery_contract = record["manifest"].get("migrationRecoveryContract")
        supported_recovery_contracts = record["manifest"].get("supportedMigrationRecoveryContracts")
        frozen_definitions = record["manifest"].get("migrationDefinitions")
        frozen_decision = record["manifest"].get("decisionHash")
        if frozen_contract is not None and frozen_contract != record["contract"]:
            fail(f"release {record['release']} persistentContract differs from frozen descriptor hash")
        if frozen_recovery_contract != record["recoveryContract"]:
            fail(f"release {record['release']} migrationRecoveryContract differs from frozen descriptor hash")
        if (not isinstance(supported_recovery_contracts, list) or
                not all(isinstance(value, dict) and
                        re.fullmatch(r"sha256:[0-9a-f]{64}", value.get("contract", "")) and
                        value.get("reader") == "journal-v1"
                        for value in supported_recovery_contracts) or
                record["recoveryContract"] not in
                [value["contract"] for value in supported_recovery_contracts]):
            fail(f"release {record['release']} has an invalid supported recovery-contract set")
        if record["release"] == engine_version and frozen_definitions is not None and frozen_definitions != definitions_hash:
            fail(f"release {record['release']} migrationDefinitions differs from generated definition set")
        if frozen_decision is not None and frozen_decision != record["decisionHash"]:
            fail(f"release {record['release']} decisionHash differs from generated release decision")

    current_record = by_release[engine_version]
    current_index = records.index(current_record)
    minimum_migratable = current_record["manifest"].get("minimumMigratableVersion")
    if minimum_migratable is not None:
        minimum_parsed = parse_version(minimum_migratable)
        if compare_version(minimum_parsed, current_record["parsed"]) > 0:
            fail(f"release {engine_version} minimumMigratableVersion is newer than the release")
    frozen_checkpoints = current_record["manifest"].get("migrationCheckpoints")
    if frozen_checkpoints is not None:
        if not isinstance(frozen_checkpoints, list) or not all(isinstance(value, str) for value in frozen_checkpoints):
            fail(f"release {engine_version} migrationCheckpoints must be a string array")
        generated_checkpoints = sorted(
            entry["id"] for entry in migration_catalog
            if isinstance(entry, dict) and entry.get("kind") == "checkpoint" and
            entry.get("target") == engine_version and isinstance(entry.get("id"), str))
        if sorted(frozen_checkpoints) != generated_checkpoints:
            fail(f"release {engine_version} checkpoint declarations differ from the generated catalog")
    if current_index > 0 and current_record["baseline"] == engine_version:
        previous = records[current_index - 1]
        if previous["contract"] != current_record["contract"]:
            incoming = [entry for entry in migration_catalog
                        if isinstance(entry, dict) and entry.get("target") == engine_version]
            if not incoming:
                fail(f"release {engine_version} changes the persistent contract without an incoming migration definition")

    current = by_release[engine_version]
    supported_recovery_entries = "\n".join(
        f'    {{"{value["contract"]}", "{value["reader"]}"}},'
        for value in current["manifest"]["supportedMigrationRecoveryContracts"]
    )
    entries = "\n".join(
        "    {\"%s\", \"%s\", \"%s\", \"%s\", %s}," % (
            record["release"], record["baseline"], record["contract"], record["decisionHash"],
            "true" if record["release"] == record["baseline"] else "false")
        for record in records
    )
    generated = (
        "// AUTO-GENERATED by generate_project_compatibility.py. DO NOT EDIT.\n"
        "#pragma once\n\n"
        "#include <cstddef>\n\n"
        "namespace Horo::Generated\n"
        "{\n"
        "struct ProjectCompatibilityDecision\n"
        "{\n"
        "    const char* release;\n"
        "    const char* contractBaseline;\n"
        "    const char* persistentContract;\n"
        "    const char* decisionHash;\n"
        "    bool establishesBaseline;\n"
        "};\n\n"
        f'inline constexpr char kHoroReleaseVersion[] = "{engine_version}";\n'
        f'inline constexpr char kHoroPersistentContract[] = "{current["contract"]}";\n'
        f'inline constexpr char kHoroCompatibilityDecision[] = "{current["decisionHash"]}";\n'
        f'inline constexpr char kHoroMigrationRecoveryContract[] = "{current["recoveryContract"]}";\n'
        "struct MigrationRecoveryReaderDescriptor\n"
        "{\n"
        "    const char* contract;\n"
        "    const char* reader;\n"
        "};\n"
        "inline constexpr MigrationRecoveryReaderDescriptor kHoroSupportedMigrationRecoveryContracts[] = {\n"
        f"{supported_recovery_entries}\n"
        "};\n"
        "inline constexpr std::size_t kHoroSupportedMigrationRecoveryContractCount =\n"
        "    sizeof(kHoroSupportedMigrationRecoveryContracts) /\n"
        "    sizeof(kHoroSupportedMigrationRecoveryContracts[0]);\n"
        "inline constexpr ProjectCompatibilityDecision kHoroReleaseDecisions[] = {\n"
        f"{entries}\n"
        "};\n"
        "inline constexpr std::size_t kHoroReleaseDecisionCount =\n"
        "    sizeof(kHoroReleaseDecisions) / sizeof(kHoroReleaseDecisions[0]);\n"
        "} // namespace Horo::Generated\n"
    )
    output.parent.mkdir(parents=True, exist_ok=True)
    if not output.exists() or output.read_text(encoding="utf-8") != generated:
        output.write_text(generated, encoding="utf-8")


if __name__ == "__main__":
    main()
