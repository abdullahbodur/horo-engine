# Cross-Engine Project Interchange

## Status

Proposed contract for [PEX-001]. Architecture decision: [ADR-007 — Cross-Engine
Project Interchange](../../adr/007-cross-engine-project-interchange.md). This
document becomes normative only when ADR-007 is accepted after the required
Godot, Unity, and Unreal feasibility evidence is reviewed. No interchange
adapter, model type, or operation exists yet.

## Purpose

Define how Horo Engine imports projects authored in other engines (Godot,
Unity, Unreal) and exports portable snapshots readable outside Horo, without
compromising destination-project integrity or pretending fidelity that does
not exist.

This document is deliberately separate from
[Project Versioning And Migration](./project-versioning-and-migration.md):

| | Project migration | Project interchange |
|---|---|---|
| Input | Horo project, older `HoroProjectVersion` | Foreign project or bundle |
| Trust | Trusted Horo-authored data | Untrusted external documents |
| Transformation | Deterministic chain of Horo-owned migrations | Per-engine adapter conversion |
| Loss model | Lossless by contract | Classified fidelity tiers + loss report |
| Version identity | One `HoroProjectVersion` timeline | Four independent versions (below) |

The two systems may share infrastructure idioms (journaled publication,
staging, recovery) but never implementation ownership. An import must not
enter the `ProjectLoading` migration path; an export is not a release build.

## Core Decisions

- Interchange orchestration is application-layer use cases
  (`ProjectInterchangeOperations`). Editor GUI, CLI commands, and MCP tools are
  thin clients over the same operations; none implements its own conversion.
- Source projects are opened read-only. No adapter, probe, or import step ever
  writes to the source.
- The destination Horo project is the sole publication authority. Imports
  stage into isolated scratch space, validate, then publish atomically with a
  durable journal; failure, cancellation, and process termination leave no
  partial state in either project.
- Every operation produces typed reports:
  - **Preflight report** — before any staging: detected source engine and
    version, per-item fidelity classification, resource budget estimate,
    blocking problems.
  - **Loss report** — after completion: every `Approximated`, `Stubbed`,
    `Skipped`, and `Unsupported` item with reason and source reference.
- Fidelity tiers are stable shared vocabulary; see
  [Fidelity Classification](#fidelity-classification). Adapters classify
  every converted item; unclassifiable items are `Unsupported`.
- Gameplay-language content (GDScript, C#, visual scripting, Blueprints) is
  never machine-translated. It is `Stubbed` (placeholder + reference to the
  original source text/asset) or `Skipped`.
- glTF may be used as a transport encoding inside bundles where suitable.
  glTF is not the canonical interchange model and no Horo contract depends on
  glTF semantics.

## Operation Lifecycle

An interchange operation is a long-running application operation with
progress reporting and cancellation, per the System Design application-layer
contract.

```text
probe ──> preflight ──> approve(optional) ──> stage ──> validate ──> publish ──> loss report
  │            │                              │           │            │
  └────────────┴──── any failure/cancel: no-op, structured error ──────┘
```

1. **Probe** (read-only): identify source engine, version, and top-level
   shape. Cheap enough to run on selection; no destination required.
2. **Preflight**: full read-only analysis producing the preflight report and
   resource budget. Import may be rejected here without touching anything.
3. **Stage**: adapters convert into an isolated staging area owned by the
   operation, outside both source and destination trees.
4. **Validate**: staged output is checked against Horo project invariants
   (scene model, asset index, metadata) exactly as a natively authored
   project would be. Staged output failing validation fails the operation.
5. **Publish**: atomic, journaled publication into the destination using the
   same durability discipline as project-open migration publication. The
   journal supports recovery after process termination.
6. **Loss report**: emitted as operation output, not a log side effect.

Export reverses direction: read the Horo project (read-only), convert into a
versioned portable bundle in staging, validate the bundle against its declared
reader contract, then publish the single bundle artifact.

## Fidelity Classification

Every source item maps to exactly one tier:

| Tier | Contract |
|---|---|
| `Preserved` | Equivalent semantics exist in Horo and were produced. Round-trip through export/import preserves meaning. |
| `Approximated` | Nearest Horo equivalent produced; the delta from source semantics is recorded in the loss report. |
| `Stubbed` | Placeholder produced that keeps identity and a resolvable reference to original content; original semantics absent. |
| `Skipped` | Recognized as convertible-in-principle but intentionally not converted (policy, budget, or scope); recorded with reason code. |
| `Unsupported` | Not recognized by the adapter; recorded with source location. |

Rules:

- Tier names and semantics are defined once, here, and are identical across
  all adapters and hosts. Adapters do not invent tiers.
- A `Preserved` claim is testable: parity coverage in the adapter's contract
  tests must exercise each preserved feature class.
- Approximation reasons use typed reason codes owned by the interchange
  model, not free-form strings.
- Silent downgrade is forbidden: an item recorded `Preserved` in preflight
  but degraded during staging is an adapter defect, not acceptable output.

## Version Identities

Four independent version identities exist:

1. **Source engine version** — probed from the foreign project. Each adapter
   declares supported source-version ranges; probing outside them fails the
   preflight with a structured "unsupported source version" result.
2. **Adapter contract version** — the interface adapters implement. Additive
   within a major line; breaking changes bump the major. An adapter may be
   upgraded in place when the release supports only the new major. If two
   contract majors must ship concurrently, each major uses a distinct,
   versioned adapter target so its dependency allowlist and compatibility
   claims remain unambiguous.
3. **Interchange model version** — the canonical typed model adapters produce
   and hosts consume. Independent of any engine's versioning.
4. **Portable bundle version** — the on-disk export snapshot format, with its
   own compatibility rules:

- Additive changes within a major line: old readers skip unknown optional
  members explicitly (never guess).
- Breaking changes: major bump; old readers reject with a clear
  version-mismatch error naming the minimum reader version.
- Unknown-future versions: readers reject explicitly. Best-effort parsing of
  unrecognized major versions is prohibited.
- Bundles declare required capabilities so readers can report "this bundle
  needs capability X" instead of failing mid-import.

Support windows: each release manifest declares which bundle major versions
it reads. Every declared major has either a bundled reader or an explicit,
deterministic converter to a supported major. A bundle older than the support
window is rejected without mutation and reports the oldest readable major and
the required converter, when one exists. Support does not silently extend
forever, mirroring `minimumMigratableVersion` policy from project migration.

## Parser Trust Boundary

Foreign project files are untrusted input:

- All parsing runs under bounded budgets: total input size, entity/node
  counts, nesting depth, decompression ratio, and wall-clock time. Exceeding
  a budget aborts parsing with a structured error.
- Native-code parsers for third-party formats live only inside their owning
  adapter target and are never exposed through public headers.
- Process isolation is **mandatory** for parsers that are not memory-safe by
  construction: native/third-party parsers, and any format whose parser reads
  untrusted length, offset, or count fields (binary formats in particular).
  In-process parsing is permitted only for bounded pure-data text formats
  validated against a strict schema before use; the schema-validation tokenizer
  and decoder must themselves be memory-safe and enforce the same input, depth,
  and time budgets. The parent treats an isolated parser crash as a failed probe
  with diagnostics, not a host fault.
- No interchange step executes embedded logic from the source project:
  scripts, shaders, and plugins are data to be classified, never run.
- Paths extracted from foreign projects are treated as hostile: normalized,
  containment-checked against the staging area, and rejected on escape.
  Non-ASCII names, spaces, and platform-specific path forms must survive
  round-trip through staging.

Resource-budget policy: probes and preflights declare estimated memory and
time; operations exceeding their declared budget fail cleanly rather than
growing unbounded. Budgets scale with project size but always have a ceiling
the user can observe before approval.

## Dependency Direction

```text
gui / cli / mcp ─────────────────────> application
                                        (ProjectInterchangeOperations)
application ────────────────────────> interchange model + adapter registry
adapter-godot / adapter-unity /
adapter-unreal ─────────────────────> interchange model (only)
interchange model ──────────────────> foundation (errors, config, jobs)
```

- Adapters depend on the interchange model contract only. An adapter never
  imports another adapter, the editor, GUI, MCP, the migration pipeline, or a
  concrete runtime/backend.
- Application owns adapter discovery/selection; feature code never selects a
  concrete adapter directly.
- The interchange model depends on foundation contracts (typed errors,
  configuration, job system) and nothing above it.
- Every adapter target registers its first-party dependency allowlist in
  `cmake/HoroDependencyPolicy.cmake`; unregistered edges stop configuration.
- Public surface stays narrow: interchange types enter `include/Horo/` only
  when a second consumer outside the owning target needs them, following the
  header ownership registry.

## Error Handling

Interchange errors follow
[Error And Diagnostics](./error-and-diagnostics.md): module-owned named error
descriptors (`InterchangeErrors`), typed results, no stringly-typed codes.
Error translation happens only at host boundaries (GUI message, CLI exit
code, MCP tool result); intermediate layers preserve error information.

Cancellation at any phase is a normal path: the operation ends in a defined
cancelled terminal state with whatever diagnostic context accumulated, and
staging scratch space is reclaimed.

## Testing Posture

- Adapter contract tests run against fixture foreign projects committed per
  adapter (small, license-clean fixtures; representative structures, not
  real commercial projects).
- Fidelity-tier claims are covered per feature class; a tier without a test
  cannot be claimed.
- Trust-boundary tests: truncated inputs, depth bombs, path escapes,
  non-ASCII/space paths, duplicate keys, oversized entries, version-skewed
  bundles, unknown-future bundle versions.
- Parser-budget enforcement tests: each declared budget (input size, entity
  count, nesting depth, decompression ratio, wall-clock time) is proven to
  abort with a structured error rather than grow unbounded.
- Publication tests: cancellation at every phase, injected publish failure,
  process-termination recovery, and idempotent retry.
- Host-parity tests: the same operation via editor GUI driver, CLI, and MCP
  produces equivalent reports and destination state.

## Related Documents

- [ADR-007: Cross-Engine Project Interchange](../../adr/007-cross-engine-project-interchange.md)
- [ADR-004: CLI / Core / GUI Boundary](../../adr/004-cli-core-gui-boundary.md)
- [System Design](./system-design.md)
- [Project Versioning And Migration](./project-versioning-and-migration.md)
- [Error And Diagnostics](./error-and-diagnostics.md)
- [Concurrency And Job System](./concurrency-and-jobs.md)
