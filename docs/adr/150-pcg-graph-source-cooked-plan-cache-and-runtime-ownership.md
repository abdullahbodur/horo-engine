# ADR-150: PCG Graph Source, Cooked Plan, Cache and Runtime Ownership

- **Status**: Proposed
- **Date**: 2026-09-02
- **Supersedes**: None
- **Scope**: PCG graph source authority, deterministic lowering, cooked-plan schema, cache identity, immutable runtime delivery, evaluation intermediates, publication, replacement, compatibility and destruction
- **Issue**: [PCG-2.1](https://github.com/abdullahbodur/horo-engine/issues/2052)
- **Jira**: [HORO-2006](https://horo-engine.atlassian.net/browse/HORO-2006)
- **Related**: [ADR-008](008-error-model-exception-boundary-and-registry.md), [ADR-010](010-job-waiting-and-operation-store-ownership.md), [ADR-017](017-prefab-role-ownership-and-capability-tiers.md), [ADR-028](028-renderer-capability-limits-and-product-profiles.md), [ADR-085](085-physics-shape-authoring-cook-and-runtime-boundary.md), [ADR-095](095-prefab-cook-boundary-and-artifact-model.md), [ADR-106](106-navigation-bake-ownership-transaction-and-cache.md), [ADR-126](126-vfx-graph-compilation-and-runtime-representation-convergence.md), [ADR-138](138-terrain-source-cooked-tile-cache-and-streaming-ownership.md)
- **Normative documents**: [Procedural Generation Architecture](../architecture/runtime/procedural-generation-architecture.md), [Asset Pipeline](../architecture/runtime/asset-pipeline.md), [Editor Document Model](../architecture/editor/editor-document-model.md), [Scene Runtime](../architecture/runtime/scene-runtime.md), [World Streaming Architecture](../architecture/runtime/world-streaming-architecture.md)

## Context

The PCG architecture describes authored node graphs, deterministic evaluation, offline
baking and runtime generation, but currently uses `PCGGraph` as both an authoring model
and an apparent runtime input. It also says generation nodes create scene objects as a
side effect. That leaves source authority, compilation, cache reuse, intermediate data,
runtime loading and generated-output commit open to incompatible implementations.

Without a stricter boundary, editor state or source nodes could ship into runtime,
cache entries could become an unofficial editable truth, and workers could mutate the
scene while evaluating a partially valid graph. Hot reload could mix a new plan with
old dependencies or release bytes while evaluations still borrow them. A cache hit
could also bypass semantic validation after a node schema or target profile changes.

The generic Asset Pipeline already owns stable asset identity, source snapshots, cook
orchestration, dependency-aware keys, cache storage, generation staging and atomic
publication. PCG must specialize graph semantics without creating another asset
registry, cache tree or publication pointer. This decision establishes that split and
the artifact lifecycle. PCG-2.2 owns the exact authored graph schema, while PCG-2.3
owns the concrete compiler and evaluator implementation.

## Decision

### 1. Authored source is the only editable truth

One versioned `PCGGraphSource` asset is the canonical authoring representation for a
graph. It contains stable graph/node/pin/edge/exposed-input identities, typed authored
values, dependency references, deterministic seed policy, declared output intents,
schema version and semantic annotations required by the cooker.

Editor-only layout, selection, camera, comments, collapsed groups, diagnostics and
preview state belong to the PCG document/presentation model. PCG-2.2 may choose which
portable layout fields travel beside source, but those fields cannot affect execution
identity unless explicitly promoted into the semantic schema.

Assets owns the durable `AssetId`, tracked source bytes, accepted source revision,
dependency records and atomic source publication. The PCG document owns only an
unsaved working candidate. Saving a valid source revision changes authoring truth;
compilation, cache insertion, preview, evaluation or generated-output commit does not.

Neither a cooked plan, cache entry, preview result nor live runtime instance is
editable source. Reverse-decompiling any of them to repair or overwrite graph source
is forbidden. If source is missing, the artifact remains usable only under its
declared packaged-runtime policy; it does not become a new authoring authority.

### 2. PCG Cook owns semantics; Assets owns the generic pipeline

The ownership split is:

| Responsibility | Authority |
|---|---|
| Durable graph identity, source bytes/revision and dependency records | Assets |
| Graph schema, node/pin/edge meaning and semantic validation | PCG Model |
| Deterministic lowering, optimization and cooked-plan encoding | PCG Cook |
| Cooker catalog, immutable source snapshot, scheduling, cache, staging and atomic generation publication | Assets Cook |
| Selected published generation and immutable artifact-byte leases | Assets provider/package system |
| Cooked-plan validation, decoded plan residency and evaluation operations | PCG Runtime |
| Evaluation scratch/intermediate storage | One PCG evaluation operation |
| Scene/output preparation and commit | Owning target subsystem plus host transaction coordinator |

PCG Model and PCG Cook are bounded contributions registered by the application host.
They do not create a second `AssetId`, dependency graph, scheduler, cache directory,
generation manifest or current-generation pointer. Assets treats PCG payloads as
typed contribution data and does not interpret node semantics or execute graph nodes.

Descriptor construction and registration are inert. They validate stable type,
schema and contribution identities but do not scan source, open a cache, compile a
graph, resolve a provider or mutate ambient state.

### 3. Cooking captures one immutable closed input snapshot

A cook request first resolves and pins an immutable input closure containing:

- the exact graph `AssetId`, accepted source revision, canonical semantic bytes and
  digest;
- every referenced asset/artifact identity, accepted revision, content digest and
  compatibility requirement;
- the exact immutable PCG node-library catalog generation and every referenced node
  definition/schema generation;
- source, semantic-model, compiler-algorithm, cooked-plan and diagnostics-map versions;
- selected target, product profile, tier, capability set and finite effective limits;
- project policy values explicitly declared as semantic cooker inputs; and
- deterministic seed-domain and numeric/ordering policy versions.

The closure is validated before lowering begins. Missing, duplicate, cyclic,
ambiguous, stale, unauthorized, unbounded or incompatible inputs fail with typed
diagnostics and publish nothing. Workers borrow only the captured immutable inputs for
the invocation and write only to operation-owned bounded output writers.

The semantic compiler validates stable identity uniqueness, pin types and cardinality,
edge legality, acyclicity, exposed-input schemas, dependency closure, output intent,
node capability/tier support and worst-case resource/work bounds. It then normalizes
defaults and produces a canonical topological schedule. Source container order, hash
iteration, filesystem order and worker completion order never decide plan order.

### 4. `CookedPCGPlan` is the only executable graph representation

PCG Cook emits one backend-neutral immutable root equivalent to:

```cpp
struct CookedPCGPlan {
    PCGGraphAssetId graph;
    PCGPlanSchemaVersion schema;
    PCGSemanticModelVersion semantics;
    PCGCompilerVersion compiler;
    PCGSourceContentIdentity source;
    PCGCookFingerprint fingerprint;
    PCGExposedInputSchema exposedInputs;
    BoundedArray<PCGCookedDependency> dependencies;
    BoundedArray<PCGCookedNode> nodes;
    BoundedArray<PCGCookedEdge> edges;
    BoundedArray<PCGEvaluationStage> stages;
    BoundedArray<PCGOutputDeclaration> outputs;
    PCGCapabilityRequirements capabilities;
    PCGCostEnvelope peakCosts;
    PCGDeterminismContract determinism;
};
```

Nodes and edges use canonical stable IDs plus validated dense plan-local indexes.
Stages declare dependency-safe evaluation order and scheduling constraints. Output
declarations describe typed candidates that an owning consumer may prepare; they are
not commands to mutate a scene. The plan records complete upper bounds for resident
bytes, evaluation scratch, points/attributes, work units, outputs and old/new overlap.

The plan contains no editor widgets/layout, source AST pointers, arbitrary callbacks,
service locators, native handles, mutable spans, cache paths, absolute source paths,
worker state or backend commands. A compiler IR and constant-folding scratch exist only
inside one cook operation and are destroyed after validated output emission.

Runtime consumes only a validated `CookedPCGPlan`. It never parses `PCGGraphSource`,
looks up editor documents, invokes a compiler/plugin, repairs a plan, or cooks a
missing variant. Offline editor bake also evaluates the cooked plan; it does not gain
a second source-interpreter path.

### 5. Cache identity closes over every byte-affecting input

PCG extends the generic Assets cook key with the canonical fingerprint of the complete
Section 3 closure. Two sources may share output only when their normalized semantic
model, locked dependency contents, node-library definitions, seed/numeric policies,
target/capability/limit profile and all byte-affecting schema/compiler versions match.

Display names, editor layout, selection, comments, diagnostics wording, timestamps,
machine identity, absolute paths, thread count, job scheduling and cache location are
excluded. Changing a semantic default, node implementation, referenced artifact,
determinism policy or effective limit changes the key even when source text does not.

The Assets cook cache stores immutable content-addressed outputs. It is disposable
derived storage, not an active generation and never source authority. Fresh output and
cache reuse pass the same requested-key, envelope, size, digest, dependency, PCG schema,
capability and cost validation before entering a generation candidate. A corrupt,
partial or mismatched entry is quarantined/evicted by Assets policy and recomputed; it
is never patched in place or accepted because its filename matches.

### 6. Publication is aggregate, immutable and generation-scoped

One successful cook produces a staged candidate generation containing the complete
plan, optional diagnostics/source map and every required PCG-owned auxiliary artifact.
The generation manifest binds exact identities, digests, sizes, dependencies, target,
capabilities and cost envelope. Missing or extra required artifacts reject the whole
candidate.

Assets atomically publishes the complete generation and advances its one current-
generation record. Failure, cancellation, stale source/dependency/catalog generation,
reservation denial or validation error leaves the previous published generation
unchanged. A generation directory appearing on disk is not publication.

The runtime provider returns an immutable exact-generation artifact lease. PCG Runtime
validates the generic envelope and PCG header, reserves decoded-plan residency, decodes
to a detached candidate, resolves dependency leases and publishes one immutable
`PCGRuntimePlan` root only after all required checks succeed. A plan-local index is
valid only with that root's generation.

### 7. Evaluation owns bounded ephemeral intermediates

Each admitted evaluation captures one immutable tuple of plan lease, exposed input
values, spatial-input snapshot identities, seed, execution mode, target/tier limits,
scope/cell identity and cancellation generation. It reserves the complete declared
scratch, intermediate and output-candidate envelope before work begins.

Intermediate point sets, masks, attributes, node outputs, temporary indexes and task
state belong exclusively to that evaluation operation. They are never stored in the
source asset, Assets cook cache, published plan, scene, global node cache or another
evaluation. An implementation may reuse operation-local arenas or immutable memoized
values only when their full semantic key and owner/lease/budget are explicit; such
storage remains discardable derived runtime data.

Workers read immutable plan/input views and write disjoint operation-owned storage.
They cannot access a live editor document, mutate RuntimeScene, call Render/Physics/
Navigation native APIs, advance World Streaming residency or publish outputs. Node
failure, cancellation, overflow or budget exhaustion returns a typed failed candidate
and no externally visible partial result.

### 8. Evaluation and generated-output commit are separate transactions

Evaluation produces one immutable `PCGEvaluationCandidate` containing typed output
intents, provenance, plan/input identities, stable output identities, complete costs
and bounded diagnostics. Generation nodes therefore produce values; they do not spawn
entities or edit subsystems as an evaluation side effect.

The host coordinator revalidates the candidate's scene/world/cell, plan, dependencies,
authority, expected revision, capability and reservation. Each target owner prepares
its own detached candidate through its normal contract: RuntimeScene owns entities,
Terrain/Foliage owns terrain data, Physics owns solver objects, Render owns GPU
resources and Navigation owns query topology. PCG owns none of those committed values.

Only an explicit aggregate commit publishes all required target-owner roots at a legal
safe point. Failure before commit retires candidates in reverse dependency order and
changes no owner. A product may define preview, offline bake or runtime commit policy,
but each uses this same evaluate/prepare/commit separation. PCG-4.1 specializes the
exact output transaction and authority rules.

### 9. Replacement preserves exact-generation leases

Publishing a new Assets generation does not mutate active PCG plans or evaluations.
The host may request replacement by pinning the new exact generation and preparing a
complete runtime-plan candidate beside the old root. It revalidates target support,
dependencies, budgets and any active-scope migration policy before one owner-boundary
swap publishes the new root.

New evaluations capture the new root after the swap. Already admitted evaluations
either complete against the old plan, or are cancelled and joined according to the
captured replacement policy; they never switch plan/node/dependency generations
mid-flight. Results from an old plan cannot commit after the coordinator's accepted
revision fence unless an explicit candidate compatibility rule proves the exact output
contract and target state still match.

Old plan roots, artifact bytes, node-library/module leases, dependencies, intermediates
and output candidates remain alive until their last reader, worker and target-owner
retirement acknowledgement drains. Asset cache eviction or module disable cannot
invalidate a borrowed pointer.

### 10. Compatibility and migration are explicit

Runtime validates the generic envelope, PCG plan schema, semantic-model version,
compiler contract, node opcode/schema set, capability/limit requirements, dependency
identities and determinism contract. Exact compatible identity is the fast path.

Authoring-source migration occurs in the editor/model boundary as a transactional
source revision and requires recook. Runtime never migrates source. A cooked-plan
migration is allowed only through a registered bounded pure adapter that declares its
source/target schemas, required node/opcode semantics, limits and loss policy. The
adapter produces a detached candidate that passes the same full validation; otherwise
the plan requires recook in an authoring host or fails packaged loading.

Unknown required nodes/opcodes, newer incompatible schema, missing dependencies,
unsupported tier/capabilities, excessive declared costs or changed deterministic
semantics are hard typed failures. Runtime cannot skip nodes, substitute defaults,
interpret an older source graph or silently select another plan/target.

### 11. Cancellation, destruction and shutdown follow ownership order

The ordered lifecycle is:

```text
source revision captured
  -> cook operation/IR/output writers
  -> staged generation
  -> atomically published immutable generation
  -> provider artifact lease
  -> decoded runtime-plan root
  -> evaluation inputs/intermediates
  -> output candidates
  -> target-owner prepared state
  -> aggregate commit or rollback
```

Destruction runs in reverse for the objects actually created. Cancelling evaluation
closes admission, signals its task group, joins/yields all workers, retires uncommitted
target candidates, releases intermediates/input snapshots, then releases plan and
dependency leases. Runtime-plan replacement retires old roots only after evaluations
drain. Provider bytes release only after decoded roots no longer borrow them.

Shutdown closes new cook/load/evaluate/commit admission first. It cancels operations,
joins workers without holding owner locks, rolls back uncommitted candidates, drains
old roots and dependency/module/provider leases, then destroys PCG Runtime. Assets may
retire cache/generation storage only after its leases acknowledge release. Deadlines
report typed incomplete shutdown; they do not detach workers or force-release memory
still reachable by them.

### 12. Contract tests protect every boundary

PCG-2.2 and PCG-2.3 implementation must add deterministic automated coverage for:

- source save versus cook/cache/runtime authority and attempted reverse publication;
- stable canonical lowering under reordered containers and worker schedules;
- cache-key sensitivity to every semantic/dependency/schema/target input and
  insensitivity to editor-only data;
- fresh cook and cache-hit equivalence, corrupt/partial cache rejection and atomic
  generation publication failure;
- malformed, cyclic, incompatible, oversized and unknown-node plan rejection;
- runtime source/compiler unavailability with valid cooked-plan loading;
- evaluation success, node failure, cancellation, budget denial and zero partial
  scene/output mutation;
- stale candidate rejection and aggregate output rollback;
- replacement with evaluations in flight, exact old/new lease retirement and stale
  completion fencing; and
- shutdown at each lifecycle phase with no callback, worker, module, provider or
  dependency lease surviving its owner.

Tests compare canonical plan bytes and typed results, not display text, cache paths,
native handles or timing-sensitive completion order. Fault injection covers every
allocation, validation, publication, preparation and owner acknowledgement boundary.

## Consequences

### Positive

- Graph source remains the only editable authority while cooked plans and caches are
  safely reproducible and disposable.
- Offline bake and runtime generation share one executable representation and cannot
  drift through separate interpreters.
- Evaluation is deterministic, bounded and incapable of partially mutating scenes or
  consumer subsystems.
- Atomic generation publication and exact-generation leases make cache reuse, hot
  reload and shutdown reviewable.
- Compatibility failures are explicit instead of being hidden by fallback compilation
  or node skipping.

### Negative

- PCG needs versioned source, semantic, plan and node-library schemas plus deterministic
  canonical encoding.
- Evaluation must reserve worst-case intermediate/output costs, which may reject work
  earlier than an opportunistic implementation.
- Preview, bake and runtime output paths require a transaction coordinator and target-
  owner preparation instead of direct spawning from nodes.
- Hot reload may temporarily retain two plan/dependency generations and in-flight
  intermediate storage.

## Rejected Alternatives

### Interpret authored graphs directly at runtime

Rejected because it ships editor/source semantics, requires runtime compiler/plugin
availability, weakens compatibility and creates a second execution path beside cook.

### Treat the latest cache entry as the active artifact

Rejected because cache entries are disposable and may be partial, stale or built for a
different dependency/target closure. Only a validated atomic generation publication
selects runtime content.

### Store cooked plans or evaluation results back into graph source

Rejected because derived data would become a competing truth, inflate source diffs and
make migration/cache invalidation ambiguous.

### Let generation nodes mutate the scene during evaluation

Rejected because failure and cancellation would expose partial state, bypass target-
owner invariants and make rollback depend on arbitrary node order.

### Replace plans in place

Rejected because evaluations would observe mixed node/dependency generations and cache
or module retirement could invalidate active readers.

### Use display names, file paths or node array positions as identity

Rejected because they are mutable, non-portable and cannot safely fence migration,
replacement or generated-output ownership.
