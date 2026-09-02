# ADR-144: Destruction Ownership, Authority, State and Runtime Geometry Boundary

- **Status**: Proposed
- **Date**: 2026-09-02
- **Supersedes**: None
- **Scope**: Destruction module and world ownership, canonical semantic state, command authority, fixed-tick transition order, cooked chunk activation, cross-system publication, persistence/replication, provider-neutral tiers, runtime geometry exclusion, cancellation, replacement and shutdown
- **Issue**: [DFR-001.1](https://github.com/abdullahbodur/horo-engine/issues/1994)
- **Jira**: [HORO-1948](https://horo-engine.atlassian.net/browse/HORO-1948)
- **Related**: [ADR-008](008-error-model-exception-boundary-and-registry.md), [ADR-010](010-job-waiting-and-operation-store-ownership.md), [ADR-012](012-world-streaming-partition-authority-and-subsystem-boundaries.md), [ADR-016](016-navigation-target-ownership-and-dependency-boundary.md), [ADR-017](017-prefab-role-ownership-and-capability-tiers.md), [ADR-023](023-world-index-and-cell-format-architecture-decision.md), [ADR-027](027-renderer-resource-identity-and-descriptors.md), [ADR-054](054-extension-and-package-authority-boundary.md), [ADR-085](085-physics-shape-authoring-cook-and-runtime-boundary.md), [ADR-087](087-scene-to-physics-ownership-and-conversion.md), [ADR-099](099-replication-ownership-authority-and-compatibility.md), [ADR-114](114-canonical-runtime-world-persistence-boundary.md)
- **Normative documents**: [Destruction and Fracture Architecture](../architecture/runtime/destruction-and-fracture-architecture.md), [Scene Runtime](../architecture/runtime/scene-runtime.md), [Physics Architecture](../architecture/runtime/physics-architecture.md), [Asset Pipeline](../architecture/runtime/asset-pipeline.md), [Networking Architecture](../architecture/runtime/networking-architecture.md), [Save Game and Persistence](../architecture/runtime/save-game-and-persistence.md)

## Context

The current Destruction document describes an ECS component with mutable health/state,
pre-fractured meshes, runtime fracture, Physics chunks, VFX debris and network events.
It does not assign the canonical state machine to one runtime owner or distinguish a
gameplay damage decision from a Physics contact, a renderer visibility change, a Scene
structural batch or a replicated command.

It also uses backend-named feature tiers and calls “runtime fracture” both activation of
offline chunks and generation of new geometry. Those are different capabilities.
Selecting pre-cooked chunks is bounded runtime state mutation; cutting an arbitrary mesh
requires topology generation, interior surfacing, collision cooking, material mapping,
budgeting and cross-client compatibility. Treating them as one 1.0 feature would permit
unbounded work or a silent fallback from missing cooked content.

Cross-system publication must be coherent. Hiding the intact render object before the
required chunk bodies and render objects are prepared creates a partially destroyed
world. Conversely, Physics cannot create destruction state by directly deleting a body,
and VFX/Audio cannot be the success signal merely because a cosmetic effect played.

This ADR establishes the ownership and capability baseline. DFR-001.2 owns concrete
stable identities, DFR-001.3 owns descriptor/tier limits, and later DFR decisions own
asset cooking, Physics, rendering, persistence, networking and diagnostics details.

## Decision

### 1. Destruction is a runtime domain with narrow module boundaries

`HoroEngine::DestructionApi` owns backend-neutral public identities, revisions,
descriptors, commands, results and immutable query snapshots. It depends only on
approved lower-level Horo contracts. `HoroEngine::DestructionRuntime` owns live
world-scoped semantic state, command admission, transition planning, bounded queues,
snapshot publication and retirement.

Destruction Cook owns semantic validation and transformation from authored fracture
intent to neutral cooked fracture graphs/chunk payload descriptors. Assets owns source
identity, dependency scheduling, cache/package storage and atomic artifact publication.
Hosts compose Scene, Destruction, Physics, Render, World Streaming, Network, Runtime
Save, Audio and VFX through explicit ports. No public API contains Jolt, graphics API,
audio middleware, transport, ImGui or filesystem-native types.

Module descriptors are inert metadata. Construction/validation cannot discover a
service, register a global, create a world, load an asset, allocate native resources or
invoke lifecycle callbacks.

### 2. Every mutable state has exactly one authority

| State or decision | Authority |
|---|---|
| Authored destructible/fracture source and editor history | Owning Scene/asset document |
| Source identity, cook scheduling, cache/package publication | Assets |
| Fracture schema, chunk graph semantics and deterministic cook | Destruction Cook |
| Live health, semantic phase, broken/support sets, command/revision history and canonical events | Scene-scoped `DestructionWorld` |
| Entity/component lifetime and aggregate scene activation root | RuntimeScene |
| Gameplay permission, damage meaning and command issuance | Product gameplay authority |
| Body/shape/constraint state, contacts and Physics safe point | Physics |
| Render objects, visibility, materials, GPU resources and fences | Render |
| Cell demand, reservations and eviction barrier | World Streaming |
| Durable destruction state capture/dormancy | Runtime Save/Persistent World |
| Replication capture, transport and remote apply delivery | NetworkRuntime |
| Debris/impact presentation | VFX, Audio and Decal owners |

The component contains stable binding and authored policy, not a second mutable state
machine:

```cpp
struct DestructibleSceneBinding {
    DestructibleId destructible;
    AssetId fractureAsset;
    DestructionPolicyId policy;
    DestructionFeatureRequirements requiredFeatures;
};
```

RuntimeScene maps the binding to one generation-checked `DestructionHandle`. Gameplay,
Physics, Network, Renderer and effects query immutable state or submit typed commands;
they never mutate a component field or chunk bitset directly.

### 3. Semantic state and runtime lifecycle are separate

Canonical semantic state is equivalent to:

```cpp
enum class DestructionSemanticPhase : uint8_t {
    Intact,
    Damaged,
    Fractured
};

struct DestructionStateSnapshot {
    DestructibleId destructible;
    DestructionGeneration generation;
    DestructionStateRevision revision;
    DestructionSemanticPhase phase;
    DestructionHealth health;
    DestructionChunkSet broken;
    DestructionSupportState support;
};
```

`Damaged` preserves non-terminal health/support changes. `Fractured` means at least one
cooked chunk transition has committed; it does not imply every chunk is active or that
the whole object is destroyed. Exact broken, supported, detached, dormant and removed
chunk membership uses stable IDs and bounded sets, not enum inference.

The world/object lifecycle is independently `Absent`, `Preparing`, `Prepared`, `Active`,
`Replacing`, `Suspended`, `Retiring` or `Failed`. Preparing is not Damaged; Retiring is
not Destroyed. Content, semantic state, active representation, capability and
persistence revisions advance independently.

### 4. All mutations are authority- and revision-checked commands

```cpp
struct DestructionCommandHeader {
    DestructibleId destructible;
    DestructionGeneration generation;
    DestructionStateRevision expectedRevision;
    DestructionCommandId command;
    SimulationTick tick;
    GameplayAuthorityGrant authority;
    DestructionCommandLimits limits;
};
```

Typed kinds include admitted damage, radial damage, explicit fracture, repair/reset
where policy permits, support change and durable-state restore. Commands carry finite
Horo-space values, stable source/instigator identity when required and explicit
criticality. Display names, entity slots, native body IDs and network connection IDs are
not authority.

Gameplay is the normal command issuer. A Physics contact is immutable evidence with
world/body/subshape/tick/generation identity; a gameplay/domain policy may translate it
to a command. Physics callbacks do not mutate Destruction. A script uses the same
capability. Network delivery contains a validated server-authoritative command/state
apply envelope; packet arrival or client prediction does not grant authority.

The Destruction owner validates capability, authority, generation, expected revision,
finite values, duplicate command ID, cooked graph membership and all count/byte/work
limits before mutation. One accepted command yields one atomic semantic revision and
canonical event batch. Invalid, unauthorized, stale or over-budget work changes nothing.

### 5. Fixed-tick ordering prevents callbacks from becoming authority

One simulation tick orders destruction as follows:

1. NetworkPoll and gameplay enqueue bounded normalized commands for the target tick.
2. Before Physics, Destruction admits commands not dependent on this tick's contacts and
   prepares any required cooked transition candidate.
3. Physics applies previously committed body/shape changes, steps once and publishes
   bounded generation-tagged contact evidence.
4. Destruction consumes eligible contact evidence after Physics, evaluates canonical
   policy in stable order and stages resulting semantic/representation transitions.
5. At the next legal Scene/Physics/Render safe-point bundle, required chunk entities,
   bodies and render objects publish privately, then RuntimeScene commits one aggregate
   root with the semantic revision.
6. Only after aggregate commit are canonical destruction events captured for gameplay,
   Network and Runtime Save; VFX/Audio/Decal receive bounded presentation requests.

A contact-triggered fracture therefore cannot add a body inside a Physics callback or
make a half-published result visible in the same step. Immediate cosmetic feedback may
be explicitly predictive and disposable; it is never canonical success.

### 6. Core 1.0 activates only pre-cooked geometry

The core path consumes an immutable validated fracture artifact containing stable chunk
graph identity, hierarchy/support relations, canonical chunk/interior geometry,
solver-neutral convex inputs and references to separately Physics-cooked shape artifacts,
bounds and finite activation costs. Exact schemas and limits belong to DFR-001.2/.3 and
the asset-cook decision in ADR-145.

At runtime Destruction may select a bounded subset of existing chunks, update semantic
membership, and coordinate activation/retirement of their pre-cooked representations.
It may not in core 1.0:

- cut, boolean, voxelize, remesh or triangulate an arbitrary mesh;
- generate Voronoi cells, interior faces, UVs, normals or material slots;
- decompose convex collision, cook a triangle mesh or derive mass properties;
- infer fracture from a Render mesh/LOD or Physics native shape;
- change topology/connectivity or synthesize missing chunks; or
- write generated geometry into source, cache or package paths.

Missing/incompatible/corrupt cooked geometry fails preparation or required activation.
It never falls back to runtime cutting, an intact box collider, visual-only fracture,
another asset/provider/backend or silent no-destruction behavior.

### 7. Runtime geometry generation is a post-1.0 capability

A future runtime-geometry capability requires its own typed request/result, source
eligibility, topology and material schema, deterministic/cross-network policy, CPU/GPU/
scratch/resident budgets, task ownership, cancellation, cache/persistence rules,
Physics cook boundary and backend qualification. It produces a detached candidate and
still uses aggregate publication.

It is not implied by a feature tier, damage type, renderer/Physics backend, high-end
device, editor availability or missing fracture asset. Products that require it fail
capability resolution until an explicitly qualified implementation exists. Core 1.0
never queues runtime mesh cutting “for later” while reporting fracture success.

### 8. Provider-neutral tiers describe pre-cooked capability

```cpp
enum class DestructionFeatureTier : uint8_t {
    Baseline,
    Standard,
    High
};
```

`Baseline` provides bounded one-level pre-cooked fracture and optional cosmetic effects.
`Standard` may admit larger graphs, staged activation and richer pre-cooked support
rules. `High` may admit pre-cooked hierarchical fracture/collapse and larger qualified
budgets. Exact chunk/depth/event/active-body/byte/work limits belong to DFR-001.3.

Tier names do not encode APIs/platforms and grant nothing. Resolution intersects the
product request, cooked variants, DestructionRuntime, World Streaming, Physics, Render,
host mode and budget capabilities into an immutable plan. Required unsatisfied content
fails. Optional fallback follows only an ordered product declaration and records its
reason. Runtime geometry generation is an independent post-1.0 capability, not a fourth
tier or automatic High behavior.

### 9. Fracture publication is detached and aggregate

The owner builds a transition plan with exact old/new semantic revision, affected chunk
closure, required consumers, entity/body/render counts, staging/resident/retirement
bytes and event capacity. World Streaming/host reserves the complete peak before work.
Workers may validate/decode immutable artifacts but never mutate the active world.

Physics and Render prepare native candidates under their own thread/safe-point rules.
VFX/Audio/Decal are post-commit optional presentation unless the product explicitly
defines another required participant. RuntimeScene exposes new semantic state, intact/
chunk visibility, entities and required bodies only after all required candidates are
Prepared and privately published under one activation ticket.

Failure/cancellation before aggregate commit destroys only candidates and preserves the
old state. Replacement retains old/new artifacts, bodies, render resources, snapshots
and reservations until readers/fences acknowledge release. Post-commit consumer loss is
Suspended/Failed/recovery according to policy, never retroactive rollback or fabricated
success.

### 10. Chunk lifetime and cleanup are explicit policy

Chunks are classified as gameplay-authoritative, durable dormant or cosmetic before
activation. Gameplay-authoritative chunks remain in canonical state and cannot be
deleted because they sleep, leave the camera or exceed a render budget. Physics sleep
is solver state, not destruction cleanup permission.

Durable dormancy serializes the canonical semantic delta through Runtime Save/Persistent
World before the last live representation retires. Cosmetic debris is VFX-owned with an
explicit finite lifetime/pool policy and never appears in canonical chunk sets. Capacity
exhaustion rejects, defers or uses a predeclared eligible replacement policy; it never
silently removes authoritative/durable chunks or another owner's state.

### 11. Persistence and replication preserve semantic ownership

ADR-114 Runtime Save registers one Destruction canonical adapter. It captures stable
destructible/chunk identities, content/state revisions, health, broken/support/dormant
sets and policy-required event progress. It excludes native bodies/shapes, render
resources, VFX particles, audio voices, contacts, jobs, caches and pointers. Restore
prepares against exact compatible cooked content and aggregate Scene/Physics/Render
activation; it never writes source documents.

Authority server worlds commit canonical commands/state. NetworkRuntime captures bounded
versioned state/deltas and delivers validated apply commands; I/O threads never mutate
Destruction. Late join receives a snapshot plus ordered later deltas. The blanket rule
“replicate only Intact/Damaged/Destroyed” is rejected because chunk/support identity may
be gameplay relevant.

Chunk motion is explicitly classified. Gameplay-authoritative chunk bodies use the
existing Physics/network transform/state contract; Destruction does not invent a second
transform stream. Cosmetic chunks may simulate locally and are excluded from gameplay,
persistence and canonical hashes. Clients cannot promote cosmetic evidence to authority.

### 12. Errors, observability and shutdown preserve ownership

Stable results include `WrongWorld`, `Unauthorized`, `StaleGeneration`, `StaleRevision`,
`DuplicateCommand`, `InvalidDamage`, `CookedContentMissing`, `CookedContentIncompatible`,
`RuntimeGeometryUnsupported`, `CapabilityUnsatisfied`, `BudgetDenied`, `PrepareFailed`,
`Cancelled`, `RetirementStalled` and `ShutdownIncomplete`.

Metrics are bounded and low-cardinality: commands/results, active objects/chunks,
candidate/retiring counts, resident/staging bytes, transition latency, budget denial,
stale work and retirement depth. Object/chunk/event IDs, coordinates, asset paths and
native handles are not dimensions. Detailed graphs/state require an explicit bounded
diagnostic snapshot.

Shutdown closes command/snapshot admission, cancels owned task groups, invalidates
candidates, detaches Scene/streaming admission, requests exact-generation consumer
retirement, drains canonical persistence/network handoffs where policy requires, and
releases artifacts/modules only after acknowledgements. It is idempotent after partial
initialization. A deadline reports retained ownership and cannot force-free referenced
geometry, bodies, GPU work or snapshots.

## Consequences

- One scene-scoped Destruction owner commits every canonical semantic transition;
  components, callbacks, network and presentation systems no longer compete for state.
- Semantic phase, chunk membership, lifecycle, content, representation and persistence
  revisions are explicit rather than collapsed into one enum.
- Core 1.0 has a bounded portable implementation path: activate offline-cooked chunks
  and never generate topology/collision at runtime.
- Hierarchical pre-cooked fracture can scale through provider-neutral tiers without
  making runtime mesh cutting a hidden High feature.
- Required render/Physics state and canonical events become visible atomically through
  the aggregate Scene boundary.
- Existing prototypes with mutable component health/state, callback mutation, backend-
  named tiers or runtime cutting fallback require migration rather than shims.

Required coverage includes every legal/illegal semantic and lifecycle transition;
authority/revision/duplicate checks; deterministic contact-command ordering; exact
cooked chunk closure; proof that core paths invoke no topology/collision cook; required
consumer failure and rollback; old/new replacement/retirement; capacity and cleanup
classification; save/restore and late join; authoritative versus cosmetic motion;
headless/Null/all interactive backends; and cancellation/shutdown at every stage.

## Rejected Alternatives

### Store mutable canonical health/state in the ECS component

Rejected because gameplay, Physics, Network and Scene code could write it without one
revision, authority check, event order or aggregate publication boundary.

### Let Physics contacts fracture objects directly

Rejected because callbacks run under Physics ownership and cannot perform structural or
cross-system mutation. Contacts are typed evidence consumed after the step.

### Generate missing fracture geometry at runtime in 1.0

Rejected because mesh/topology/interior/collision generation is unbounded and has no
qualified deterministic, network, memory or cancellation contract. Missing cook fails.

### Name tiers after graphics APIs or device classes

Rejected because destruction semantics span Scene, Physics, Assets and gameplay; a
renderer name neither grants capability nor defines portable limits.

### Hide the intact mesh before required chunks are prepared

Rejected because observers would see partial destruction without required collision or
render state. Publication is aggregate and activation-ticket scoped.

### Delete sleeping or distant authoritative chunks automatically

Rejected because sleep/visibility are not persistence or gameplay authority. Cleanup is
classified, budgeted and lossless for durable state.

### Replicate only the three phase names

Rejected because broken/support membership and authoritative chunk motion may affect
gameplay. Replication captures canonical semantic state and delegates motion correctly.
