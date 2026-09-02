# ADR-126: VFX Graph Compilation and Runtime Representation Convergence

- **Status**: Proposed
- **Date**: 2026-09-02
- **Supersedes**: None
- **Scope**: Stack/graph authoring convergence, canonical VFX compilation target, offline lowering, CPU/GPU kernel packages, cook validation, artifact identity/versioning, runtime loading and migration
- **Issue**: [VFX-005.1](https://github.com/abdullahbodur/horo-engine/issues/1753)
- **Jira**: [HORO-1710](https://horo-engine.atlassian.net/browse/HORO-1710)
- **Related**: [ADR-008](008-error-model-exception-boundary-and-registry.md), [ADR-011](011-vfx-effect-ownership-simulation-domain-and-renderer-boundary.md), [ADR-028](028-renderer-capability-limits-and-product-profiles.md), [ADR-035](035-shader-source-and-intermediate-representation.md), [ADR-123](123-vfx-cpu-stage-order-determinism-and-gameplay-coupling.md), [ADR-124](124-vfx-gpu-simulation-readback-and-compute-fallback.md), [ADR-125](125-vfx-transparency-sorting-and-pass-placement.md)
- **Normative documents**: [VFX and Particles Architecture](../architecture/runtime/vfx-and-particles-architecture.md), [Asset Pipeline](../architecture/runtime/asset-pipeline.md)

## Context

Horo's VFX design describes both a stack-oriented particle authoring surface and a
future node graph. It also sketches `ParticleSystemDescriptor`, CPU SoA execution,
GPU kernels and render batches, but does not name the one immutable cooked root that
all runtime instances consume. Without that convergence, stack and graph tooling could
create separate loaders, parameter schemas, simulation semantics or renderer seams.

The current text says graphs compile ahead of time and runtime does not interpret
arbitrary scripts per particle. That constraint needs an implementable boundary:
which compiler stages are editor/cook-only, which typed descriptor is the single
target, how CPU/GPU work is represented without shipping the authoring graph, and how
version/capability changes invalidate or migrate artifacts.

The generic Asset Pipeline already owns source snapshots, cook orchestration, cache
identity, target selection, immutable envelopes and atomic generation publication.
VFX must specialize its semantic payload without building a second cache/publisher or
letting editor/runtime code execute source graphs directly.

## Decision

### 1. `CompiledVfxEffectDescriptor` is the single runtime model

Both authoring paths compile to exactly one root runtime representation:

```cpp
struct CompiledVfxEffectDescriptor {
    VfxEffectAssetId asset;
    VfxArtifactSchemaVersion schemaVersion;
    VfxSemanticModelVersion semanticVersion;
    VfxParameterSchema parameterSchema;
    BoundedArray<ParticleSystemDescriptor> emitterUnits;
    BoundedArray<VfxCompiledEdge> asynchronousEdges;
    BoundedArray<VfxResourceRequirement> resources;
    VfxCapabilityRequirements capabilities;
    VfxFallbackSet fallbacks;
    VfxPeakCostEnvelope peakCosts;
    VfxCookFingerprint fingerprint;
};
```

`ParticleSystemDescriptor` remains the typed emitter-unit foundation nested in that
root. It contains normalized spawn/simulation/render intent and references immutable
compiled plans/kernels; it is not a second top-level asset or a graph-specific runtime.
Implementation may rename it to `CompiledVfxEmitterUnitDescriptor` during migration,
but only one of those names may remain in the installed/runtime schema.

`EffectSystem`, `SimulationDomainResolver`, CPU/GPU simulators, extraction and Renderer
consume views derived from `CompiledVfxEffectDescriptor`. No caller loads a parallel
`CompiledGraphEffect`, `StackParticleRuntime`, source-node tree or property bag.

### 2. Stack and graph are authoring frontends, not runtime types

The editor may persist two source document kinds:

- a stack document with ordered emitter modules and fixed connection points; and
- a graph document with typed nodes/ports and explicit supported dependency edges.

Their editors, layout, comments, selection and presentation metadata may differ.
They need not round-trip into one another. Each frontend validates its source schema,
resolves defaults and emits the same compiler-owned semantic input. `sourceKind` may
be retained as diagnostics/provenance but cannot change runtime loading, scheduling,
parameter binding, simulation, fallback, extraction or rendering behavior.

Semantically equivalent stack/graph sources under identical locked inputs must produce
the same canonical descriptor and kernel fingerprints. A frontend cannot add a private
runtime module, hidden default or special renderer callback to preserve authoring
convenience.

### 3. The compiler has one deterministic lowering pipeline

The VFX cooker executes these bounded phases against the generic cook operation's
immutable source/dependency/target snapshot:

```text
Parse and migrate authoring schema
    -> canonicalize typed modules/nodes, IDs, defaults and parameters
    -> validate types, topology, stages, authority and boundedness
    -> partition connected emitter simulation units and asynchronous edges
    -> lower ADR-123 CPU stage plans and semantic RNG/payload schemas
    -> lower GPU work/kernel requirements and ADR-124 readback/fallback envelopes
    -> lower ADR-125 render classes, sort requirements and resource layouts
    -> compute dependencies, capability requirements and peak costs
    -> emit one CompiledVfxEffectDescriptor plus target kernel packages/source map
    -> validate complete payload and return it to Asset Pipeline publication
```

A compiler-internal IR may exist only inside the bounded cook invocation. It is not an
asset, public API, cache authority or runtime representation. It cannot be retained by
a plugin after invocation or serialized as an alternate executable payload.

Canonical ordering uses stable semantic IDs, not editor node position, insertion
history, pointer order, unordered-container iteration, worker completion or module
registration order. Given the same complete cache inputs, output descriptor, kernel
packages, dependency list and diagnostics are byte-identical.

### 4. Runtime never interprets authoring graphs or arbitrary particle scripts

Packaged/runtime compositions load only a validated cooked artifact. They do not parse
stack/graph source, visit authoring nodes per particle, evaluate editor expressions,
JIT user source, execute arbitrary bytecode, invoke an authoring plugin or search for a
missing compiler. Cooked source text and editor metadata are not executable fallback.

CPU execution uses an immutable ADR-123 stage plan whose prevalidated typed kernel IDs
bind to installed runtime kernel implementations. Iterating that bounded plan is not
permission for an untyped script VM: kernels have declared stage reads/writes, schemas,
costs and versions, and cannot allocate or discover functions during a particle step.

GPU execution uses target kernel artifacts produced through ADR-035's shader/compiler
routes and referenced by logical `VfxKernelId`. Runtime/native realization permitted by
the selected cook target does not reconstruct the VFX graph or change semantic plans.
Missing CPU/GPU variants follow typed admission/fallback, never source compilation.

### 5. Node/module extension points terminate at cook and typed kernels

A VFX module/node provider contributes stable provider, node-type, schema and kernel
IDs through the host's validated catalog. The cook snapshot pins the provider version
and validates parameter/port schemas, allowed ADR-123 stages, domain support, resource
requirements, deterministic behavior class and finite cost bounds.

Trusted extension code may run as a bounded cooker contribution under the Asset
Pipeline contract. Runtime support is a separately installed typed kernel provider;
the cooked artifact references its stable ABI/schema identity. Neither contribution
may retain borrowed cook input, publish files, expose native renderer types, register
ambient callbacks or smuggle executable source/script into the descriptor.

An unavailable/mismatched provider fails cook or runtime admission with its exact
identity. Registration order cannot select a competing implementation. Packaged assets
include only dependencies reachable from the accepted compiled descriptor and target
kernel packages.

### 6. Cook validation is complete and rejects partial semantics

Before payload emission, the compiler validates at least:

- stable unique effect/emitter/module/node/port/channel/parameter identities;
- source schema migration, typed defaults/ranges, finite numeric values and bounded
  strings/arrays/maps;
- graph/stack topology, allowed cycles/delays, reachable outputs and deterministic
  canonical ordering;
- ADR-123 stage reads/writes, initialization, RNG channel stability, payload authority,
  occurrence bounds and CPU-mandatory behavior;
- unit partitioning, cross-domain asynchronous edges and rejection of synchronous
  authoritative GPU-to-CPU dependencies;
- required CPU/GPU kernels, target variants, capabilities and provider versions;
- ADR-124 readback schema, fallback compatibility and complete CPU/GPU/shared peak
  costs, including replacement/frames-in-flight overlap;
- ADR-125 material/blend/topology/shadow/sort compatibility and layout bounds;
- referenced materials, meshes, textures, fields, audio/event assets and package
  dependencies with their exact generations/digests; and
- checked count/stride/offset/byte/work products and artifact size limits.

Unknown nodes/fields, implicit lossy conversion, unbounded loops/recursion/events,
missing variants, conflicting writers and unsupported target requirements are errors.
The cooker never drops a required node, substitutes a default graph, clamps gameplay
semantics or emits a partially runnable descriptor to make the cook succeed.

Diagnostics retain source document kind, stable source node/module/port and property
identity through a bounded source map. Human-readable editor position is presentation
evidence only and never changes artifact semantics.

### 7. `VfxCookedArtifact` separates common semantics from target kernels

The VFX payload inside the generic Asset Pipeline envelope is:

```cpp
struct VfxCookedArtifactHeader {
    VfxArtifactSchemaVersion schemaVersion;
    VfxSemanticModelVersion semanticVersion;
    VfxCpuKernelAbiVersion cpuKernelAbi;
    VfxGpuKernelPackageVersion gpuKernelPackage;
    CookTargetId target;
    CapabilityFingerprint capabilityFingerprint;
    Digest descriptorDigest;
    Digest dependencyDigest;
};

struct VfxCookedArtifact {
    VfxCookedArtifactHeader header;
    CompiledVfxEffectDescriptor descriptor;
    VfxCpuKernelPackage cpuKernels;
    VfxGpuKernelPackage gpuKernels;
    OptionalVfxSourceMap sourceMap;
};
```

The descriptor is backend-neutral common semantic data. Target packages may contain
different qualified native/intermediate shader bytes, but they implement the same
logical kernel IDs, parameter/resource layouts, stage order and render outputs. A
backend-specific package cannot replace the common descriptor or select a different
fallback/pass policy.

The CPU package contains bounded stage plans, immutable parameter/layout data and
stable references to installed typed kernels; it is not arbitrary loadable machine
code or script bytecode. The GPU package follows ADR-035's admitted target artifact
rules and remains renderer-private at realization.

Headless target artifacts retain enough descriptor/CPU information to validate and run
required gameplay VFX without GPU dependencies; permitted visual-only units resolve to
Null according to ADR-011/124. They do not pretend to contain interactive GPU kernels.

### 8. Artifact identity includes every semantic input

The VFX cook/cache fingerprint extends the Asset Pipeline's complete `CacheKeyV1` with:

- authoring schema and canonical source digest;
- VFX compiler/lowering and semantic-model versions;
- provider/node/module schemas and runtime kernel ABI versions;
- ADR-owned stage/RNG/payload/readback/fallback/sort/layout schema versions;
- selected cook target and effective target capability/limit fingerprint;
- all referenced asset IDs, accepted revisions and content digests;
- project VFX policy/default inputs that are explicitly cooker-visible; and
- descriptor/kernel encoding and diagnostic source-map format versions.

Editor layout, selection, timestamps, absolute source paths, machine identity,
diagnostic wording and worker scheduling are excluded. A relevant input change creates
a new cache key/generation; the cooker never patches an existing immutable artifact.

### 9. Compatibility is explicit at envelope, payload and kernel layers

Asset Pipeline envelope compatibility remains authoritative. Within an accepted
envelope, VFX validates schema, semantic-model, CPU ABI, GPU package, target,
capability fingerprint, dependency and payload digests before decoding any array or
binding a kernel.

The runtime supports the current VFX payload schema and at least the two prior schemas
only through explicit tested readers/migrations that produce the current immutable
model without semantic loss. Older artifacts require recook; newer artifacts,
unsupported semantic changes, provider ABI mismatches or incompatible target packages
return `VfxArtifactVersionUnsupported`/typed dependency failure. Packaged runtime never
attempts repair from authoring source.

Schema evolution uses tagged/versioned fields with fixed-width encodings and checked
lengths. Removing/retyping a field, changing a default, RNG channel, stage meaning,
fallback envelope, layout or kernel ABI is a compatibility change and updates the
appropriate version/fingerprint. Unknown required fields are errors; unknown optional
fields may be ignored only when the schema explicitly declares forward-safe behavior.

### 10. Publication and hot reload remain atomic and lease-safe

VFX cooker output returns through host-owned bounded writers. Asset Pipeline verifies
the generic envelope plus VFX payload/digests and atomically publishes the complete
generation; VFX code cannot choose output paths, publish a cache entry or replace
`current.json`.

Runtime resolves one immutable artifact lease, validates it fully, builds an admission
plan and only then creates an effect instance. Failure creates no half-bound kernels,
resource reservation or active emitter. Active instances pin their artifact/provider/
resource generations through stop and retirement.

Hot reload prepares and validates a new artifact/admission generation separately.
Existing instances either finish on the old lease or restart at an authored safe point;
their live particle buffers are never reinterpreted under a new descriptor/layout.
Failed reload retains the last good generation and reports the exact validation/cook/
admission cause. Old and new peak overlap remains charged until final retirement.

### 11. Qualification proves convergence and runtime isolation

Required implementation evidence includes:

- equivalent stack/graph fixtures producing byte-identical canonical descriptor and
  kernel fingerprints while authoring-only layout/comments do not change output;
- proof that all runtime entry points accept only `CompiledVfxEffectDescriptor` views
  and no source kind selects a loader, executor, parameter or renderer path;
- packaged/headless scans with no source graph/stack parser, authoring node classes,
  script/JIT compiler or editor plugin dependency reachable from VFX execution;
- deterministic fresh/cached/multi-worker cook output and diagnostics under randomized
  registration/insertion/completion order;
- every validation category at exact bounds and one beyond, with source-mapped errors
  and zero partial artifact publication;
- current, two previous, too-old, newer, corrupted, truncated, oversized, wrong-target,
  wrong-capability, provider/kernel ABI and dependency-digest cases;
- missing CPU/GPU variants using ADR-011/124 fallback rather than authoring-source
  compilation or required-node deletion;
- atomic generation publication, failed cook/reload last-good retention, active old
  leases, new admission and final retirement accounting; and
- common semantic fixtures across every shipped target/backend while target packages
  remain private implementations of the same logical IDs/layouts.

## Consequences

### Positive

- Stack and graph authoring share one loader, runtime, parameter model and renderer seam.
- Runtime cost is bounded compiled kernel execution, not per-particle graph/script interpretation.
- Cook validation and fingerprints expose semantic/version drift before activation.
- Target-specific kernels remain compatible implementations of one common descriptor.
- Atomic publication/hot reload preserves last-good state and live-instance lifetimes.

### Costs

- The VFX compiler needs canonical IDs/order, a semantic lowering pipeline and source maps.
- Descriptor, kernel and provider schemas require deliberate version/compatibility tests.
- Semantically equivalent authoring paths must maintain shared golden fixtures.
- Unsupported/old artifacts may require recook instead of runtime best-effort repair.

## Rejected Alternatives

### Maintain separate stack and graph runtime descriptors

Rejected because loaders, defaults, simulation/fallback and rendering semantics would
drift. Both frontends terminate at `CompiledVfxEffectDescriptor`.

### Ship the authoring graph and interpret it per particle

Rejected because traversal, dynamic typing, allocation and arbitrary scripts violate
bounded frame execution, packaged isolation and deterministic stage authority.

### Treat compiler IR as a stable public/runtime asset

Rejected because it creates a second executable representation and freezes compiler
internals. IR remains invocation-local; only the compiled descriptor/packages persist.

### Compile missing source variants on packaged runtime startup

Rejected because packaged runtime lacks authoring trust/tools and would produce
machine-dependent latency/behavior. Missing variants return typed fallback/failure.

### Let target GPU packages carry their own fallback or pass policy

Rejected because native target artifacts implement kernels; common Horo descriptor and
frontend contracts own domain, fallback, material, sort and pass semantics.

### Mutate active particle storage in place during hot reload

Rejected because layout/kernel/schema changes would reinterpret live memory. New
artifacts are separately admitted and instances finish old or restart safely.
