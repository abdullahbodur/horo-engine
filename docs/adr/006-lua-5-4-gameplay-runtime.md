# ADR-006: Lua 5.4 Gameplay Runtime

- **Status**: Accepted
- **Date**: 2026-08-02
- **Supersedes**: None
- **Scope**: Project-authored gameplay scripting

## Context

Horo needs a portable scripting runtime for object-attached gameplay behaviors.
Native C++ and scripts must use one behavior descriptor, lifecycle, event,
serialization, and scene-mutation contract. The scripting implementation must
not expose unrestricted filesystem, process, operating-system, or native-library
access, and a script failure must remain contained to its owning play session or
behavior instance.

The runtime is frame-hot during fixed simulation, so embedding must have explicit
memory and instruction budgets and must not require editor, renderer, or platform
types in the public gameplay API.

## Decision

Horo pins Lua 5.4.8 from the official source release and embeds it through the
official C API behind the private `HoroGameplayLua` adapter.

- Lua is not exposed from `HoroEngine::GameplayApi`.
- Every behavior instance owns a scene-scoped Lua state with bounded memory and
  callback instruction budgets.
- Only the base, table, string, math, and UTF-8 libraries are opened. Filesystem,
  process, OS, package/native loading, and debug APIs are unavailable.
- A `.horo_script.meta` sidecar owns the stable `BehaviorTypeId`. Source-level
  identity is an assertion, not persistence identity.
- Lua behavior descriptors enter the same frozen `BehaviorRegistry` as native
  behavior descriptors.
- Lua callbacks receive the same `BehaviorContext`, tick-assigned input,
  deferred transform/scene mutation, and next-tick event delivery semantics as
  native behaviors.
- Candidate reload source is parsed and descriptor-validated before the active
  program revision changes. Schema-incompatible candidates are rejected without
  replacing the active program.

The dependency is fetched with a fixed SHA-256 digest. Updating Lua requires a
new dependency digest, sandbox regression pass, and compatibility review.

## Consequences

Lua behavior code remains portable and backend-neutral, and native and script
behaviors can share scene persistence and editor attachment UI. Horo owns the
cost of maintaining the sandbox adapter, deterministic callback boundary, and
schema migration policy. Lua bytecode is not a stable persistent or package
identity; source or target-specific cooked artifacts must be validated against
the Horo SDK and target manifest.

Other scripting languages may be added later only behind the same gameplay
contracts. They do not change scene persistence identity or lifecycle ordering.
