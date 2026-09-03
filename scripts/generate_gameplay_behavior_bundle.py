#!/usr/bin/env python3
"""Generate one deterministic native gameplay descriptor bundle."""

from __future__ import annotations

import argparse
import hashlib
import json
import pathlib
import re
import sys


MAXIMUM_SOURCE_FILES = 4096
MAXIMUM_SOURCE_BYTES = 4 * 1024 * 1024
MAXIMUM_TOTAL_SOURCE_BYTES = 32 * 1024 * 1024
MODULE_ID = re.compile(r"[a-z][a-z0-9_]*(?:\.[a-z0-9_]+)+\Z")
TYPE_ID = re.compile(r"game\.[a-z0-9_]+(?:\.[a-z0-9_]+)+\Z")
TOKEN = re.compile(
    r"(?P<space>\s+)|(?P<line_comment>//[^\n]*)|(?P<block_comment>/\*.*?\*/)|"
    r"(?P<string>\"(?:\\.|[^\"\\])*\")|(?P<identifier>[A-Za-z_]\w*)|"
    r"(?P<punctuation>[(),])|(?P<other>.)",
    re.DOTALL,
)


def parse() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", required=True, type=pathlib.Path)
    parser.add_argument("--revision-output", required=True, type=pathlib.Path)
    parser.add_argument("--module-id", required=True)
    parser.add_argument("--fingerprint", required=True)
    parser.add_argument("sources", nargs="+", type=pathlib.Path)
    return parser.parse_args()


def significant_tokens(text: str) -> list[tuple[str, str]]:
    ignored = {"space", "line_comment", "block_comment"}
    return [(match.lastgroup or "other", match.group()) for match in TOKEN.finditer(text) if match.lastgroup not in ignored]


def annotations_in(text: str, source: pathlib.Path) -> list[tuple[str, str]]:
    tokens = significant_tokens(text)
    annotations: list[tuple[str, str]] = []
    expected = ("identifier", "punctuation", "identifier", "punctuation", "string", "punctuation")
    for index, token in enumerate(tokens):
        if token != ("identifier", "HORO_BEHAVIOR"):
            continue
        candidate = tokens[index : index + len(expected)]
        if len(candidate) != len(expected) or tuple(kind for kind, _ in candidate) != expected:
            raise ValueError(f"malformed HORO_BEHAVIOR annotation in {source}")
        _, opening, symbol, comma, encoded_type_id, closing = candidate
        if opening[1] != "(" or comma[1] != "," or closing[1] != ")":
            raise ValueError(f"malformed HORO_BEHAVIOR annotation in {source}")
        type_id = json.loads(encoded_type_id[1])
        annotations.append((symbol[1], type_id))
    return annotations


def read_sources(sources: list[pathlib.Path]) -> list[tuple[pathlib.Path, str, str]]:
    if len(sources) > MAXIMUM_SOURCE_FILES:
        raise ValueError("gameplay descriptor source count exceeds the supported limit")
    resolved = sorted(source.resolve() for source in sources)
    if len(set(resolved)) != len(resolved):
        raise ValueError("gameplay descriptor sources contain duplicate paths")
    records: list[tuple[pathlib.Path, str, str]] = []
    total_bytes = 0
    for source in resolved:
        if not source.is_file():
            raise ValueError(f"source path does not exist or is not a regular file: {source}")
        encoded = source.read_bytes()
        total_bytes += len(encoded)
        if len(encoded) > MAXIMUM_SOURCE_BYTES or total_bytes > MAXIMUM_TOTAL_SOURCE_BYTES:
            raise ValueError("gameplay descriptor source bytes exceed the supported limit")
        records.append((source, encoded.decode("utf-8"), hashlib.sha256(encoded).hexdigest()))
    return records


def validate_annotations(annotations: list[tuple[str, str, pathlib.Path]]) -> None:
    seen_symbols: set[str] = set()
    seen_ids: set[str] = set()
    for symbol, type_id, source in annotations:
        if symbol in seen_symbols:
            raise ValueError(f"duplicate native behavior symbol {symbol!r} in {source}")
        if type_id in seen_ids:
            raise ValueError(f"duplicate native behavior type ID {type_id!r} in {source}")
        if not TYPE_ID.fullmatch(type_id):
            raise ValueError(f"invalid native behavior type ID {type_id!r} in {source}")
        seen_symbols.add(symbol)
        seen_ids.add(type_id)


def descriptor_revision(module_id: str, annotations: list[tuple[str, str, pathlib.Path]], source_hashes: list[str]) -> int:
    identity = ["horo.generated-gameplay-descriptor.v1", module_id]
    identity.extend(f"{type_id}\0{symbol}" for symbol, type_id, _ in annotations)
    identity.extend(sorted(source_hashes))
    revision = int.from_bytes(hashlib.sha256("\n".join(identity).encode("utf-8")).digest()[:8], "big")
    return revision or 1


def write_atomic(path: pathlib.Path, content: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + ".tmp")
    temporary.write_text(content, encoding="utf-8")
    temporary.replace(path)


def generated_storage(annotations: list[tuple[str, str, pathlib.Path]]) -> str:
    declarations = "\n".join(
        f"Horo::Gameplay::BehaviorDescriptor HoroGeneratedBehaviorDescriptor_{symbol}();\n"
        f"Horo::Gameplay::BehaviorFactoryBinding HoroGeneratedBehaviorFactory_{symbol}();"
        for symbol, _, _ in annotations
    )
    descriptors = ",\n        ".join(f"HoroGeneratedBehaviorDescriptor_{symbol}()" for symbol, _, _ in annotations)
    factories = ",\n        ".join(
        "{.typeId = Horo::Gameplay::BehaviorTypeId::Parse(" + json.dumps(type_id) + ").Value(), "
        f".factory = HoroGeneratedBehaviorFactory_{symbol}()}}"
        for symbol, type_id, _ in annotations
    )
    count = len(annotations)
    return f'''// Generated by generate_gameplay_behavior_bundle.py. Do not edit.
#include "Horo/Gameplay/GameModule.h"
#include <array>

{declarations}

extern "C" HORO_GAME_EXPORT Horo::Gameplay::IGameModule *CreateGameModule() noexcept;
extern "C" HORO_GAME_EXPORT void DestroyGameModule(Horo::Gameplay::IGameModule *) noexcept;

namespace {{
const auto &Descriptors() {{
    static const std::array<Horo::Gameplay::BehaviorDescriptor, {count}> descriptors{{{{
        {descriptors}
    }}}};
    return descriptors;
}}

const auto &Factories() {{
    static const std::array<Horo::Gameplay::GeneratedBehaviorFactoryBinding, {count}> factories{{{{
        {factories}
    }}}};
    return factories;
}}
}}
'''


def generated_exports(module_id: str, fingerprint: str, revision: int, count: int) -> str:
    behavior_pointer = "Descriptors().data()" if count else "nullptr"
    factory_pointer = "Factories().data()" if count else "nullptr"
    return f'''

extern "C" HORO_GAME_EXPORT const Horo::Gameplay::GameModuleDescriptor *GetGameModuleDescriptor() noexcept {{
    static const Horo::Gameplay::GameModuleDescriptor descriptor{{
        .structSize = sizeof(Horo::Gameplay::GameModuleDescriptor),
        .sdkBoundaryVersion = Horo::Gameplay::GameplaySdkBoundaryVersion,
        .moduleId = {json.dumps(module_id)},
        .buildFingerprint = {json.dumps(fingerprint)},
    }};
    return &descriptor;
}}

extern "C" HORO_GAME_EXPORT const Horo::Gameplay::GeneratedGameplayDescriptorBundle *GetGameplayDescriptorBundle() noexcept {{
    static const Horo::Gameplay::GeneratedGameplayDescriptorBundle bundle{{
        .structSize = sizeof(Horo::Gameplay::GeneratedGameplayDescriptorBundle),
        .schemaVersion = Horo::Gameplay::GameplayDescriptorBundleSchemaVersion,
        .sdkBoundaryVersion = Horo::Gameplay::GameplaySdkBoundaryVersion,
        .moduleId = {json.dumps(module_id)},
        .buildFingerprint = {json.dumps(fingerprint)},
        .descriptorRevision = {revision}ULL,
        .behaviors = {behavior_pointer},
        .behaviorCount = {count},
        .nativeFactoryBindings = {factory_pointer},
        .nativeFactoryBindingCount = {count},
        .diagnostics = nullptr,
        .diagnosticCount = 0,
        .lifecycle = {{.create = &CreateGameModule, .destroy = &DestroyGameModule}},
    }};
    return &bundle;
}}
'''


def generated_source(
    module_id: str,
    fingerprint: str,
    revision: int,
    annotations: list[tuple[str, str, pathlib.Path]],
) -> str:
    return generated_storage(annotations) + generated_exports(module_id, fingerprint, revision, len(annotations))


def main() -> int:
    args = parse()
    if not MODULE_ID.fullmatch(args.module_id) or not args.fingerprint or len(args.fingerprint.encode("utf-8")) > 256:
        raise ValueError("module identity or build fingerprint is invalid")
    sources = read_sources(args.sources)
    annotations = [
        (symbol, type_id, source)
        for source, text, _ in sources
        for symbol, type_id in annotations_in(text, source)
    ]
    annotations.sort(key=lambda annotation: (annotation[1], annotation[0]))
    validate_annotations(annotations)
    revision = descriptor_revision(args.module_id, annotations, [record[2] for record in sources])
    write_atomic(args.output, generated_source(args.module_id, args.fingerprint, revision, annotations))
    write_atomic(args.revision_output, f"{revision}\n")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, UnicodeError, ValueError, json.JSONDecodeError) as error:
        print(f"gameplay descriptor generation failed: {error}", file=sys.stderr)
        raise SystemExit(1)
