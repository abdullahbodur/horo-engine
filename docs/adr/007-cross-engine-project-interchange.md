# ADR-007: Cross-Engine Project Interchange

- **Status**: Proposed
- **Date**: 2026-08-26
- **Supersedes**: None
- **Scope**: Import and export of foreign engine projects (Godot, Unity, Unreal) into and out of Horo
- **Issue**: [#2308](https://github.com/abdullahbodur/horo-engine/issues/2308) ([PEX-001.1])
- **Normative document**: [Cross-Engine Project Interchange](../architecture/foundation/cross-engine-interchange.md)

## Context

Horo needs to move projects between engines: game teams arrive with existing
Godot, Unity, or Unreal projects, and some teams must hand Horo-authored work
back to another toolchain. Project interchange is a product capability under
[PEX-001], but no architecture decision yet defines where interchange lives,
who owns mutation authority, how much fidelity is promised, or how the
interchange data itself versions.

Two adjacent systems already exist and must not be conflated:

- **Project versioning and migration** ([Project Versioning And Migration])
  upgrades Horo-authored projects between Horo Engine releases. It owns one
  semantic `HoroProjectVersion`, automatic migration chains, journaled
  publication, and recovery for Horo-owned durable data.
- **Asset import** (application use cases for importing assets) brings
  individual asset files into a Horo project's asset tree.

Interchange is neither of these. A foreign project is not a Horo project with
an older `HoroProjectVersion`, and importing one is not an asset-file copy.
The risk of leaving this undecided: adapters grow inside editor UI code or
inside the migration pipeline, foreign formats leak into the typed scene model,
and lossy conversion happens silently because no fidelity contract exists.

Interchange is also adversarial input by construction. Foreign project files
are untrusted documents parsed from disk; a malformed or malicious bundle must
never corrupt the destination project.

## Decision

**Cross-engine interchange is an application-owned, adapter-isolated,
source-read-only, transactional operation behind a versioned adapter contract.
It never reuses the migration pipeline, never mutates its source, and never
publishes partial results to the destination project.**

### Ownership and dependency direction

- Interchange orchestration lives in the application layer as typed use cases
  (`ProjectInterchangeOperations`), consumed identically by editor GUI, CLI,
  and MCP per [ADR-004] and the System Design application-layer rules.
- Per-engine adapters are isolated targets. An adapter depends on the
  versioned interchange model contract only — not on the migration pipeline,
  the editor, GUI, MCP, or another adapter. Dependency direction follows
  [System Design]; `HoroDependencyPolicy.cmake` registers each adapter's
  allowlist explicitly.
- Adapters never write to their source project. The source is opened
  read-only; all writes go through the destination publication authority.

### Transaction authority

The destination Horo project is the only publication authority. An import:

1. Probes the source read-only and produces a **preflight report**
   (fidelity classification, resource budget estimate, unsupported content).
2. Builds the result in an isolated staging area, outside the destination.
3. Validates staged output against Horo project invariants.
4. Publishes atomically into the destination using the same journaled,
   recoverable publication discipline as project migration, without sharing
   its implementation.
5. Produces a **loss report**: every approximated, stubbed, skipped, and
   unsupported item is recorded. Nothing is lost silently.

Failure, cancellation, or process termination at any step leaves the source
untouched and the destination without a partial import.

### Fidelity tiers

All adapters share one stable fidelity vocabulary. Every converted item lands
in exactly one tier:

| Tier | Meaning |
|---|---|
| `Preserved` | Converted with equivalent semantics in Horo. |
| `Approximated` | Semantics mapped to the nearest Horo equivalent; difference recorded. |
| `Stubbed` | Placeholder retained with reference to the original content. |
| `Skipped` | Recognized but intentionally not converted; recorded with reason. |
| `Unsupported` | Not recognized by the adapter; recorded. |

Fidelity tiers are part of the adapter contract: an adapter that cannot
classify an item cannot claim it. Gameplay-language translation (visual
scripting graphs, C#, GDScript, Blueprints) is **out of scope** — such content
is `Stubbed` or `Skipped`, never machine-translated.

### Version identities

Four independent versions exist and must not be collapsed:

1. **Source engine version** — detected during probe; each adapter declares
   the source-engine versions it supports.
2. **Adapter contract version** — the interface adapters implement;
   additive-only within a major line.
3. **Interchange model version** — the canonical in-memory/serialized model
   adapters produce and consume.
4. **Portable bundle version** — the on-disk export snapshot format.

A reader rejects unknown-future bundle versions explicitly rather than best-
effort parsing them. Export bundles declare required capabilities so old
readers can identify what they cannot represent.

### Parser trust boundaries

Foreign parsers run with bounded resources (input size, node count, depth,
time). Process isolation is mandatory for parsers that are not memory-safe by
construction; the normative document defines the exact criteria. A parser
failure yields a structured error and a failed preflight — never a
destination mutation.

### glTF reuse

glTF scene nodes and payloads may be reused as a *transport encoding* inside
adapters and export bundles where it fits, but glTF is **not** the canonical
project model. The canonical model remains Horo-owned; glTF is an exchange
detail adapters may adopt, not a schema interchange depends on.

### Feasibility before freezing the contract

Read-only feasibility spikes against representative Godot, Unity, and Unreal
projects precede freezing the adapter contract (PEX-001.2 onward). Spike
evidence identifies required adapter capabilities and engine-version risks;
the contract is not finalized on assumptions.

## Consequences

- Horo gains a safe path off other engines and a documented honesty policy
  about what converts and what does not.
- The migration pipeline keeps single responsibility over Horo-to-Horo
  upgrades; interchange adds a parallel publication discipline rather than
  overloading migration.
- Each new engine adapter is an isolated target behind one versioned
  contract; adding Godot, then Unity, then Unreal does not churn the core.
- Users see explicit loss reports instead of silent degradation; this costs
  adapter authors classification work on every converted item.
- Machine translation of gameplay logic stays permanently out of scope until
  a superseding ADR changes that, with its own trust and correctness story.

## Rejected Alternatives

- **Reuse the project migration pipeline for imports.** Rejected: migration
  assumes trusted Horo-authored data under one known version timeline;
  foreign input is untrusted, multi-versioned, and partially convertible.
  Sharing the pipeline would couple two different risk models.
- **Adapters living inside editor code.** Rejected: violates the CLI/Core/GUI
  boundary ([ADR-004]) — headless and MCP hosts need identical behavior, and
  editor composition is not an adapter host.
- **Making glTF the canonical interchange model.** Rejected: glTF cannot
  express materials, prefabs, input, settings, or graph semantics Horo owns;
  adopting it would either fork glTF semantics or lose Horo data. It remains
  an optional transport encoding.
- **Silent best-effort conversion with a log file.** Rejected: silent lossy
  conversion destroys user trust and is indistinguishable from data corruption
  downstream; loss must be first-class operation output.
- **Direct in-place import into the destination project.** Rejected: partial
  imports after mid-operation failure are exactly the class of damage the
  staging-and-publish model prevents.

[PEX-001]: https://github.com/abdullahbodur/horo-engine/issues/2307
[ADR-004]: 004-cli-core-gui-boundary.md
[System Design]: ../architecture/foundation/system-design.md
[Project Versioning And Migration]: ../architecture/foundation/project-versioning-and-migration.md
