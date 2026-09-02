# ADR-151: PCG Ownership, Authority, Tier and Lifecycle

- **Status**: Proposed
- **Date**: 2026-09-02
- **Supersedes**: None
- **Scope**: PCG subsystem ownership, evaluation purity, commit authority, deterministic execution classes, offline/preview/runtime/hybrid modes, headless/null composition, replacement, cancellation and shutdown
- **Issue**: [PCG-1.1](https://github.com/abdullahbodur/horo-engine/issues/2054)
- **Jira**: [HORO-2008](https://horo-engine.atlassian.net/browse/HORO-2008)
- **Related**: [ADR-008](008-error-model-exception-boundary-and-registry.md), [ADR-010](010-job-waiting-and-operation-store-ownership.md), [ADR-012](012-world-streaming-partition-authority-and-subsystem-boundaries.md), [ADR-018](018-command-registration-permissions-threading-and-packaged-build-policy.md), [ADR-028](028-renderer-capability-limits-and-product-profiles.md), [ADR-085](085-physics-shape-authoring-cook-and-runtime-boundary.md), [ADR-105](105-navigation-asset-and-scene-ownership-boundary.md), [ADR-137](137-terrain-foliage-ownership-data-tier-and-lifecycle.md), [ADR-150](150-pcg-graph-source-cooked-plan-cache-and-runtime-ownership.md)
- **Normative documents**: [Procedural Generation Architecture](../architecture/runtime/procedural-generation-architecture.md), [System Design](../architecture/foundation/system-design.md), [Scene Runtime](../architecture/runtime/scene-runtime.md), [World Streaming Architecture](../architecture/runtime/world-streaming-architecture.md), [Editor Document Model](../architecture/editor/editor-document-model.md)

## Context

Procedural generation crosses editor documents, cooked assets, spatial queries,
runtime evaluation, world streaming and several target subsystems. The existing PCG
architecture lists offline, runtime and hybrid modes plus hardware-oriented feature
tiers, but does not assign mutable state or authority at each stage. It also does not
say whether a headless server can execute PCG, what a null implementation means, or
which determinism class is eligible to affect authoritative state.

ADR-150 establishes source/cooked-plan/cache/intermediate ownership and prohibits node
side effects. A broader subsystem boundary is still required. Without it, PCG could
become a second owner of Scene entities, foliage, terrain, collision or navigation;
preview could leak into production state; and local client evaluation could be mistaken
for server authority. Capability or renderer tiers could silently change semantics,
while shutdown could destroy plan/module/input owners before asynchronous evaluations
and target candidates drain.

This decision defines the foundational PCG service, authority inputs, execution modes,
determinism classes and lifecycle. PCG-3.1 specializes spatial inputs and node-library
ownership, PCG-4.1 specializes pure evaluation/output commit, PCG-5.1 specializes
cross-system readiness, PCG-6.1 specializes editor preview/bake/undo, and PCG-7.1 owns
the final scale/trust and 1.0 versus post-1.0 feature boundary.

## Decision

### 1. PCG owns plans and evaluations, never feature truth

PCG is a backend-neutral domain service that consumes exact immutable cooked plans and
input snapshots and produces immutable candidate output descriptions. It owns:

- PCG graph/node semantic types and validation;
- cooked-plan decoding, validation and logical residency;
- evaluator registry/catalog generations and deterministic scheduling rules;
- admitted evaluation operations, task groups, scratch, intermediates and diagnostics;
- candidate output provenance and the PCG portion of readiness evidence; and
- PCG service generations, operation admission, cancellation and retirement.

PCG does not own editor source documents, durable asset bytes, scene entities,
streaming residency, terrain height, foliage instances, Physics bodies/shapes,
Navigation topology, Render resources, gameplay authority, save/network state or the
final commit boundary. It cannot infer ownership from a node type or retain a mutable
copy after another subsystem accepts an output.

An output candidate is evidence and proposed data, not authoritative state. Target
owners validate and prepare their own representations; a host-owned coordinator
commits the required aggregate at an owner-defined safe point.

### 2. Every mutable state has one owner

| State or lifecycle decision | Sole owner |
|---|---|
| Unsaved graph edits, revision, dirty/history/save/preview command state | PCG graph document service |
| Durable graph identity/source revision and cooked generation publication | Assets |
| Cooked graph semantics and plan encoding | PCG Model/Cook |
| Runtime plan root, evaluator catalog snapshot and evaluation operations | scene/world-scoped PCG Runtime |
| Spatial input source truth and snapshot publication | Source subsystem named by PCG-3.1 |
| Global cell demand, priority, reservation and residency barrier | World Streaming |
| Entity/component truth and structural publication | RuntimeScene |
| Terrain/foliage, Physics, Navigation and Render state | Each named target subsystem |
| Gameplay/server permission to request and accept semantic generation | Product gameplay authority |
| Save/restore and replication capture/apply orchestration | Runtime Save and NetworkRuntime |
| Aggregate generated-output commit/rollback | Host application transaction coordinator |

The application composition root installs typed PCG capabilities and passes explicit
owner references. Feature code does not discover services, choose a concrete evaluator,
inspect a service locator or infer host role from process name, render availability or
global flags. Internal descriptors remain inert metadata.

### 3. Requests carry authority; PCG never creates it

Every evaluation request names the exact requester capability, world/scene/cell and
plan generations, immutable input snapshot, expected target revisions, execution mode,
determinism requirement, seed domain, product/tier policy and finite limits. Admission
validates the complete tuple before reserving work.

Gameplay authority decides whether generation may affect canonical runtime state. In
standalone, the local product authority may issue and accept requests. In multiplayer,
the server authority alone commits replicated semantic outputs unless a later typed
contract explicitly grants a client-predicted cosmetic class. Possession, local input,
cell visibility, editor focus, node ownership or successful evaluation does not grant
commit authority.

PCG returns candidates with the captured authority epoch and expected target revisions.
The coordinator revalidates them immediately before preparation/commit. Revoked,
expired, stale or mismatched authority rejects the candidate without attempting to
reinterpret it as local-only output.

### 4. Evaluation is pure with respect to external owners

An admitted evaluation reads only:

- one immutable exact-generation `PCGRuntimePlan` lease;
- one immutable closed spatial/semantic input snapshot;
- validated copied exposed-input values;
- captured deterministic seed, numeric and ordering policies; and
- its immutable execution/tier/limit envelope.

It writes only operation-owned bounded scratch, intermediates, diagnostics and output
candidates. Workers cannot read a live editor document, ECS storage, streaming ledger,
native provider object or mutable gameplay singleton. Nodes cannot submit structural
commands, spawn objects, write terrain, install collision/navigation, allocate GPU
resources, publish events or invoke arbitrary application callbacks.

Time slicing and asynchronous scheduling may change when work completes, never the
canonical result or output order for a deterministic class. Cancellation is observed
at declared bounded checkpoints and produces no partially published result.

### 5. Execution modes change composition, not ownership

`PCGExecutionMode` is a closed typed policy selected by the host:

| Mode | Input scope | Result boundary | Authority rule |
|---|---|---|---|
| `OfflineBake` | Accepted document/source and authoring-world snapshot | Detached bake candidate, then explicit document/asset transaction | Editor command capability; never runtime authority |
| `EditorPreview` | One document revision and isolated preview-world snapshot | Replaceable preview overlay/session only | Cannot write document, production scene or durable asset |
| `Runtime` | Committed world/cell snapshot at a declared safe point | Target-owner aggregate runtime commit | Product/server authority required for semantic state |
| `Hybrid` | Separately identified baked base plus runtime overlay inputs | Independent base and overlay candidates with explicit composition | Runtime layer cannot rewrite baked/source truth |
| `ValidationOnly` | Any admitted immutable plan/input fixture | Diagnostics and cost/readiness evidence only | No output preparation or commit capability |

All modes consume the same cooked-plan semantics and pure evaluator boundary. A mode
does not silently select another graph, seed, node implementation, target fallback or
limit profile. Converting preview to bake or bake to source is a new explicit command
against the owning document/asset revision, not a flag on an evaluation result.

Hybrid output retains layer and provenance identity. The runtime overlay cannot delete
or mutate baked base data except through target-owner operations explicitly permitted
by the product contract. Regeneration/replacement identifies which layer it replaces.

### 6. Determinism is an explicit capability class

Every cooked plan and request declares one required determinism class:

| Class | Guarantee | Eligible use |
|---|---|---|
| `PortableDeterministic` | Canonical output is identical across every certified host/target for the declared semantic and numeric policy | Offline canonical bake, authoritative runtime generation, save/replication reconstruction |
| `ProfileDeterministic` | Canonical output is identical only within one exact certified platform/toolchain/capability profile | Local runtime or platform-scoped packaged bake when product policy pins that profile |
| `BestEffortPreview` | Invariants and bounds hold, but numeric/output equivalence is not promised | Isolated editor preview or explicitly cosmetic disposable output only |

`PortableDeterministic` requires versioned RNG streams derived from stable graph/node/
scope identities, canonical iteration/output order, specified numeric operations and
quantization, no wall-clock/global RNG/hash-order inputs, and deterministic reduction/
tie breaking. The exact semantic/numeric policy and node-library generations are part
of plan identity.

`ProfileDeterministic` records the exact compatibility fingerprint; it cannot load or
replicate as portable merely because two observed runs matched. `BestEffortPreview`
cannot be committed as authoritative, baked into canonical content, persisted for
gameplay reconstruction or used to validate a network peer. Product configuration may
require a stronger class than a node/plan provides; it never silently downgrades.

Determinism class is independent of workload/feature tier. A high-end GPU is not more
authoritative than a CPU, and lack of a renderer does not weaken a headless server's
required semantic guarantee.

### 7. Feature tiers are finite capability and limit profiles

PCG resolves the project/product request against an immutable effective tier before
plan load or evaluation admission. The tier declares:

- supported modes and determinism classes;
- supported node semantic/opcode families and output kinds;
- maximum graph nodes/edges/stages/dependency depth;
- point, attribute, intermediate, output and diagnostic limits;
- resident plan/input/scratch/candidate and old/new overlap bytes;
- work units, concurrency, time-slice/checkpoint and in-flight operation limits; and
- required target subsystem and headless capabilities.

Names such as `es3`, `dx11`, `dx12_vulkan` and `high_end` are product-profile labels,
not renderer APIs or authority tiers. The resolved PCG profile contains Horo-owned
capabilities and numeric limits. Renderer/native types do not enter the PCG API, and
PCG feature code cannot branch on backend names.

Unknown cost is not zero. Admission reserves complete worst-case costs, including
input snapshots, intermediates, output candidates, target preparation and replacement
overlap, against the correct owner ledgers. Exceeding a required limit fails before
mutation. An optional bounded fallback must be declared in the cooked plan and product
policy and returns an explicit selected-plan identity; ad hoc quality reduction is
forbidden.

### 8. Headless and null are first-class explicit compositions

A headless host may install the real deterministic PCG Runtime without a window,
Renderer or editor. It can load plans, capture non-render inputs, evaluate supported
nodes and commit to available authoritative target owners such as RuntimeScene,
Persistent World, Physics or Navigation. Plans requiring unavailable Render/editor
capabilities fail admission or use only an explicitly declared product fallback.

`PCGNull` is an explicit implementation of the same asynchronous admission/result and
lifecycle surface. It advertises no evaluation/output capability and returns typed
`Unavailable`/`UnsupportedCapability` results at the normal completion boundary. It
does not emit empty success, fabricate deterministic outputs, bypass target readiness
or mutate requested state. Validation-only tooling may use the real plan validator
without installing an evaluator.

Tests may compose a deterministic in-memory evaluator/target harness. It follows the
same reservations, generation fences, completion queues, commit timing and shutdown
protocol; it is not the production null implementation and cannot leak test shortcuts
into public contracts.

### 9. Runtime lifecycle is explicit and generation-fenced

The host lifecycle is:

```text
Uninstalled
  -> Installed (inert descriptors and typed capabilities validated)
  -> Activating (catalogs, limits and owner references captured)
  -> Active (plan load/evaluation admission open)
  -> Quiescing (new admission closed; work cancels/drains)
  -> Inactive (roots and operations retired)
  -> Uninstalled
```

Installation validates one complete candidate catalog and publishes it only through
the host composition root. It performs no source scan, asset load, worker start or
target mutation. Activation captures immutable evaluator/node catalogs and effective
profiles, creates owned queues/arenas and registers owner-safe completion endpoints.
Partial activation rolls back in reverse order.

Each activation receives a never-reused `PCGRuntimeGeneration`. Plan handles,
evaluation IDs, completion messages, input snapshots, target candidates and
observability records carry it. A completion from an old generation cannot publish
into a restarted scene/world/service.

### 10. Request and operation state transitions are total

An evaluation operation follows:

```text
Created -> Validating -> Reserved -> Scheduled -> Evaluating -> CandidateReady
   |          |            |           |             |              |
   +----------+------------+-----------+-------------+------------> Failed
                          \-------------+-------------+------------> Cancelling
                                                                    -> Cancelled
CandidateReady -> PreparingTargets -> ReadyToCommit -> Committed -> Retiring -> Retired
        |                  |                 |
        +------------------+-----------------+---------------------> RolledBack -> Retired
```

Transitions occur on the PCG/coordinator owner boundaries, not worker threads. Every
terminal result is exactly once and contains operation/generation identity, typed
status, bounded diagnostics, charged/peak cost and any retirement acknowledgement.
Duplicate completion is idempotently ignored; missing acknowledgement keeps owners and
leases alive.

Failure/cancellation before aggregate commit changes no authoritative target state.
After commit, correction is a new revisioned target-owner transaction; PCG cannot
rewind another subsystem by restoring its candidate or rerunning a graph.

### 11. Replacement and invalidation never patch live work

A newer graph, plan, node catalog, input source, tier profile or authority epoch creates
a new immutable generation/candidate. The host prepares it beside the old active root
and publishes a replacement only after all required validation/reservation/readiness
succeeds. PCG never edits a plan, catalog or input snapshot in place.

New requests see the new root after the safe-point swap. Existing evaluations retain
their exact plan/catalog/input/module leases and either finish under an explicitly
permitted fence or cancel and drain. Their candidates revalidate expected target and
authority revisions before preparation. Stale results cannot become a cache hit,
preview, runtime fallback or later commit merely because their stable graph ID matches.

Replacement failure preserves the previous active root and target state. Old roots
retire only after all evaluations, completion messages, candidates and target-owner
acknowledgements release them.

### 12. Shutdown is bounded, cooperative and ownership-safe

Shutdown closes new plan-load/evaluation/commit admission, invalidates the service
generation, requests cancellation for operation task groups and stops scheduling new
node work. It then drains owner completion queues and rolls back uncommitted target
candidates in reverse dependency order.

Workers are joined or cooperatively yielded without holding PCG, Scene, Streaming or
target-owner locks. Input/provider/node-module/catalog/plan leases remain until the
last reachable worker and completion drains. Target owners destroy prepared native
state and acknowledge retirement before PCG releases candidate provenance. Only then
does PCG release intermediates, plan roots, arenas, queues and installed catalogs.

Editor document shutdown additionally cancels preview/bake requests and detaches its
presentation after preview target retirement; it does not own runtime shutdown.
Headless and null compositions follow the same phase order. A deadline reports a typed
incomplete shutdown and retains reachable ownership; it never detaches workers, leaks
callbacks or force-frees borrowed storage.

### 13. Contract coverage is required before implementation is complete

Targeted automated tests must cover:

- ownership-table invariants and rejection of direct Scene/target mutation by nodes;
- offline bake, isolated preview, runtime, hybrid and validation-only result boundaries;
- standalone/server/client authority, stale authority epochs and unauthorized commit;
- every determinism class, canonical scheduling/reduction/RNG, profile mismatch and
  forbidden downgrade;
- tier/capability/limit resolution, complete reservation and explicit fallback only;
- real headless execution, null typed failure and deterministic test-harness timing;
- activation success/failure rollback and old-generation completion rejection;
- evaluation success, node failure, cancellation at every checkpoint and exactly-once
  terminal results;
- target preparation failure, aggregate rollback, post-commit correction boundary and
  zero partial authority mutation;
- plan/catalog/input/tier replacement with work in flight and exact lease retirement;
  and
- shutdown from every lifecycle phase with no worker, callback, target candidate,
  provider/module/input lease or owner registration surviving its legal owner.

Tests use bounded fault injection and deterministic virtual scheduling. They assert
typed identities, state transitions, revisions, charges and owner acknowledgements,
not wall-clock timing, native handles or diagnostic wording.

## Consequences

### Positive

- PCG cannot become a second owner of generated feature state or create authority from
  successful evaluation.
- Offline, preview, runtime, hybrid, headless and null behavior share one explicit
  typed lifecycle instead of mode-specific side channels.
- Determinism and workload capability are separate, reviewable dimensions.
- Replacement, cancellation and shutdown retain exact-generation ownership until all
  asynchronous and target-owner users drain.
- Server/headless use does not depend on a renderer or editor, while unsupported plans
  fail explicitly.

### Negative

- Hosts must compose a transaction coordinator and explicit capability/authority inputs
  around PCG instead of letting nodes call services directly.
- Portable determinism requires stricter numeric, ordering, RNG and node-certification
  contracts than best-effort generation.
- Complete reservations and old/new overlap accounting may reject or delay large work.
- Hybrid generation needs stable layer/provenance identity in each target subsystem.

## Rejected Alternatives

### Let PCG own generated entities and feature data

Rejected because it duplicates RuntimeScene and target-subsystem truth and makes save,
replication, streaming, rollback and native-resource lifetime ambiguous.

### Treat evaluation success as permission to commit

Rejected because authority and target revisions may change during asynchronous work;
only the owner/coordinator can validate and publish the aggregate.

### Derive determinism or authority from renderer/performance tier

Rejected because hardware class neither grants gameplay authority nor proves portable
numeric equivalence, and headless servers must remain first-class.

### Make preview, bake and runtime separate evaluator implementations

Rejected because their semantics would drift and preview success would not validate the
cooked runtime path. Modes differ at input scope and result authority, not graph meaning.

### Make null evaluation succeed with empty output

Rejected because absence would be indistinguishable from legitimate empty generation
and could erase or incorrectly commit target state.

### Force-cancel workers and free their inputs at shutdown

Rejected because cancellation is a request, not proof of completion; force release
would permit use-after-free across plan, module, provider and input-snapshot leases.
