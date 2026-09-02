# ADR-164: Virtual Texturing Ownership, Product Scope and Capability Tier

- **Status**: Proposed
- **Date**: 2026-09-02
- **Supersedes**: None
- **Scope**: Virtual Texturing public/runtime ownership, Assets/Materials/World Streaming/Renderer/producer boundaries, typed composition, capability tiers, product scope, lifecycle, unsupported paths and migration
- **Issue**: [VTX-001.1](https://github.com/abdullahbodur/horo-engine/issues/2176)
- **Jira**: [HORO-2130](https://horo-engine.atlassian.net/browse/HORO-2130)
- **Parent**: [VTX-001](https://github.com/abdullahbodur/horo-engine/issues/2175)
- **Related**: [ADR-008](008-error-model-exception-boundary-and-registry.md), [ADR-010](010-job-waiting-and-operation-store-ownership.md), [ADR-012](012-world-streaming-partition-authority-and-subsystem-boundaries.md), [ADR-027](027-renderer-resource-identity-and-descriptors.md), [ADR-028](028-renderer-capability-limits-and-product-profiles.md), [ADR-034](034-gpu-memory-and-residency-ownership.md), [ADR-054](054-extension-and-package-authority-boundary.md), [ADR-137](137-terrain-foliage-ownership-data-tier-and-lifecycle.md)
- **Normative documents**: [Virtual Texturing Architecture](../architecture/runtime/virtual-texturing-architecture.md), [System Design](../architecture/foundation/system-design.md), [Asset Pipeline](../architecture/runtime/asset-pipeline.md), [Material and Shader Model](../architecture/runtime/material-and-shader-model.md), [World Streaming Architecture](../architecture/runtime/world-streaming-architecture.md), [Rendering Architecture](../architecture/runtime/rendering-architecture.md)

## Context

The current Virtual Texturing architecture combines a logical virtual-texture model,
CPU residency records, renderer resources, file-system cache layout, asynchronous work
and backend-named feature rows in one document. That description is useful as an
exploration, but it does not establish module ownership or an implementable contract.
It permits several incompatible implementations: VTX could allocate native textures,
Renderer could become logical page authority, Materials could retain residency state,
World Streaming could be bypassed by a second global scheduler, and runtime code could
construct build/cache paths and read loose files directly.

Virtual texturing crosses existing authorities. Assets owns stable asset identity,
cooked artifact publication and bounded byte access. Materials owns authored sampling
intent. World Streaming owns cell demand and aggregate admission. ADR-034 Renderer
owns GPU reservations, native resources and safe retirement. Producers such as Terrain
own source-domain meaning. VTX must coordinate those authorities without duplicating
their state or reversing dependency direction.

Product scope is also ambiguous. ADR-034 assigns RND-010.9 the future delivery of
capability-gated sparse backing and an admitted atlas path. A table keyed by `dx11`,
`dx12_vulkan` and `high_end` currently implies product support, fixed limits and
fallbacks that have not been implemented or qualified. Backend/API names are neither
product profiles nor evidence of effective capability. Horo 1.0 therefore cannot make
virtual texturing a required content path.

This ADR fixes ownership, composition, capability and lifecycle policy. VTX-001.2 and
later tickets will specify exact identities, descriptors, artifact schemas, request
queues, feedback, renderer realization and qualification without changing the
authorities below.

## Decision

### 1. VTX is an optional runtime vertical slice with a narrow public contract

Horo introduces these logical targets when implementation begins:

```text
HoroEngine::VirtualTexturingApi
HoroEngine::VirtualTexturingRuntime
```

`VirtualTexturingApi` owns backend-neutral fixed-schema values, strong identities and
generations, immutable descriptors/snapshots, capability requirements and narrow
command/query/result contracts. It depends only on Foundation and the narrow Horo
asset/render value types required by its declarations. It exposes no filesystem paths,
native GPU handles, backend enums, worker implementation, editor types or package
layout.

`VirtualTexturingRuntime` owns the live logical virtual-texture generations, page
demand, request coalescing, feature-local page selection and eviction policy,
residency intent, logical page-table revisions and bounded orchestration. It depends
on the API plus host-composed ports. It does not select a backend, open files, create
native GPU resources, own material definitions, admit world cells or create a process-
global scheduler.

Cook, renderer, material, world-streaming, producer and tooling integrations are
separate adapters that depend toward their owning public contracts. They may be
co-located initially, but co-location does not merge authority. Foundation and generic
Assets/Renderer targets do not depend on the feature runtime.

The application host is the composition root. It supplies validated asset/page-store,
job, clock, renderer, budget, producer and observability capabilities before activation.
Descriptors are inert metadata: construction and validation perform no I/O, job
submission, service discovery, resource allocation or global registration.

### 2. Every responsibility has one authority

| Responsibility | Authority | Deliberate non-owner |
|---|---|---|
| Virtual-texture IDs, page coordinates/generations, logical request state, page choice and residency intent | VTX | Renderer, Materials and producers do not mutate logical residency |
| Source asset identity, import settings, deterministic cook, immutable artifact publication and bounded byte leases | Assets/Pipeline plus the VTX cooker | VTX runtime does not discover files, build paths or recook content |
| Authored virtual-sampling intent, semantic slots and required/fallback material variants | Materials | Materials do not own page state, physical slots or feedback |
| Cell relevance, priority, aggregate CPU/GPU/staging reservation and cell activation/retirement barrier | World Streaming under ADR-012 | VTX does not run another world scheduler or evict a cell |
| VTX feature-local scheduling, coalescing, cancellation and page eviction within admitted reservations | VTX runtime | World Streaming and Renderer do not select logical pages |
| GPU allocation, physical cache/sparse resources, descriptors, uploads, mappings, graph passes and GPU-safe retirement | Renderer under ADR-027/034 | VTX never exposes or retains native resource/fence identity |
| Source-domain tile meaning and immutable producer payloads | Producer such as Terrain | Producers do not allocate VTX GPU resources or control global residency |
| Package membership, target variants and runtime mount policy | Packaging/Assets | VTX does not construct package/cache paths or scan loose files |
| UI, diagnostics and capture projection | Presentation/Observability adapters | Tools do not become the logical or GPU state authority |

An adapter acknowledgement is evidence owned by its producer, not permission to
commit another owner's state. VTX publishes a page as usable only after the required
asset byte lease, renderer mapping completion and matching VTX generation all succeed.
Renderer completion cannot publish a stale logical generation. A material sample or
feedback observation is demand evidence, not a residency command.

### 3. Cross-owner communication is typed, immutable and generation checked

The architecture admits contracts with these semantic roles; later tickets freeze
their exact C++ layout and numeric bounds:

```cpp
struct VirtualTextureDescriptor;
struct VirtualTextureGeneration;
struct VirtualPageId;
struct VirtualPageDemandBatch;
struct VirtualPageReadRequest;
struct VirtualPagePayloadLease;
struct VirtualPageRealizationPlan;
struct VirtualPageMappingCompletion;
struct VirtualTextureResidencySnapshot;
```

Every asynchronous request carries virtual-texture identity, owner generation,
operation identity, bounded cost and cancellation context. Every completion repeats
the matching identity and reports a typed result. Immutable payload leases remain
owned by Assets until the declared consumer completion; VTX records logical state but
does not steal storage ownership. Renderer receives finite realization plans and
returns backend-neutral mapping completions; it never receives permission to rewrite
VTX priority policy.

The steady-state flow is:

```text
material/producer/world demand snapshots
  -> VTX bounded demand merge and page plan
  -> Assets bounded page-byte request and immutable lease
  -> Renderer admitted upload/map operation
  -> generation-checked renderer completion
  -> VTX committed residency snapshot
  -> renderer/material binding projection
```

No step publishes a partially active generation. Cache/package keys include every
semantic cook input and target capability variant; paths and native resource names are
adapter-private implementation details rather than identity.

### 4. Capability tiers describe VTX behavior, not hardware rank or milestones

VTX has a closed, feature-local capability tier:

| Tier | Product behavior | Required contract |
|---|---|---|
| `Unavailable` | VTX assets are rejected; ordinary non-VTX material/content variants remain usable | No VTX runtime or renderer capability is required |
| `Atlas` | VTX uses an explicitly budgeted physical atlas and logical page table | Admitted atlas/page-table formats, bounded feedback/readback or producer demand, upload/mapping and shader variant |
| `Sparse` | VTX may use qualified native sparse/tiled residency | All `Atlas` semantic guarantees plus effective sparse binding, granularity, queue/synchronization and retirement support |

`Sparse` is an implementation capability, not a higher-quality promise. Content
declares `Required`, `OptionalWithFallback` or `Disabled` VTX policy separately from
the tier. `Required` fails activation when its exact format/geometry/operation/budget
requirements are unavailable. `OptionalWithFallback` selects a cooked non-VTX or
other explicitly named variant during admission. It never silently switches after
partial activation or substitutes an uncooked lower-quality asset.

Effective support is the intersection of compiled implementation, selected renderer
backend/device support, exact format/limit/operation support, admitted memory and
transfer budgets, shader/material variants, artifact availability and product policy.
An API family, device name, product-profile rank or extension bit alone cannot select a
tier. Sparse-to-atlas fallback is allowed only when the atlas path and its peak cost
were independently admitted.

Capability tiers are independent of roadmap milestones. They describe supported VTX
behavior; the Product Roadmap Model and issue metadata describe delivery timing.

### 5. Virtual texturing remains Post-1.0 and optional

Horo 1.0 does not require `VirtualTexturingApi`, `VirtualTexturingRuntime`, a VTX
cooker, sparse resources or atlas realization. The 1.0 baseline must render supported
projects through ordinary texture/material variants and must reject packages that mark
VTX-only content as required. Headless and dedicated-server compositions use
`Unavailable` unless a non-rendering producer/validation tool explicitly composes the
narrow API; they never initialize GPU residency as a side effect.

Post-1.0 may ship `Atlas`, `Sparse` or both after their respective implementation and
qualification gates pass. RND-010.9 remains the prerequisite for capability-gated
sparse backing and the admitted atlas path; this ADR does not claim that work is
implemented. VTX tickets may develop contracts and deterministic Null/test adapters
before product enablement, but no host advertises a runtime tier without complete
composition and qualification.

### 6. Lifecycle, threading and cancellation are explicit

The host validates an immutable VTX composition plan before activating a world or
material that requires it. Activation order is:

1. validate descriptors, artifact variants, material variants and effective tier;
2. reserve bounded CPU, I/O, transfer, GPU, request and completion capacity;
3. create the VTX logical generation and renderer resources without publication;
4. start bounded demand processing and publish readiness only after all required
   owners acknowledge the same generation.

VTX scheduling uses the injected ADR-010 job/executor contract. Workers prepare and
decode immutable payloads, but only VTX owner safe points commit logical state and only
Renderer safe points mutate GPU resources. Callbacks enqueue bounded completions; they
do not publish inline, block the frame thread or retain borrowed state beyond its lease.

Cancellation invalidates the operation/generation first, stops new admission and lets
already-submitted owner work reach a terminal completion. Stale completions release
their leases and reservations without publication. Replacement reserves old/new peak
overlap and publishes atomically; failure preserves the last valid generation when
policy permits. Shutdown drains cancellation and CPU ownership, then requests renderer
retirement. Physical slots/resources are not reused until every relevant GPU queue and
dependent lease proves retirement.

Cleanup capacity is reserved before producer work. Queue pressure cannot prevent
cancellation, lease release, renderer retirement or terminal result delivery. VTX may
evict disposable pages within its allowance, but cannot refund renderer heap capacity,
expand a World Streaming reservation or evict activation-critical world content on its
own.

### 7. Failures and unsupported paths are observable and fail closed

Public results distinguish at least unsupported composition/tier, missing artifact or
fallback, invalid descriptor, stale generation, cancellation, bounded queue/capacity,
asset I/O/integrity, decode, renderer admission, upload/map, device loss and retirement
stall. Later contracts assign stable typed codes and bounded diagnostic context.

The following paths are unsupported:

- direct filesystem access or runtime construction of build/cache paths;
- a VTX-owned process-global worker pool, scheduler, renderer or memory ledger;
- backend/API names or native handles in public descriptors and persisted assets;
- treating sparse support as implicit permission to allocate or map;
- runtime cooking, loose-file discovery or material shader generation on a page miss;
- silent required-to-optional, sparse-to-atlas or VTX-to-ordinary-texture fallback;
- frame-index-only slot reuse, blocking GPU waits in normal frames or unbounded queues;
- UI/debug commands mutating VTX/renderer state without an application operation.

Diagnostics expose bounded Horo identities, generations, counts, costs and typed
states. Asset paths, source content, native handles and raw page bytes follow their
own privacy and trust policy and are not included by default.

### 8. Migration replaces speculative policy instead of preserving two models

The Virtual Texturing Architecture becomes the narrative entry point and links to this
ADR for policy. Its illustrative monolithic structs, direct disk-cache tree,
backend-named tier table and hard-coded maximums are removed. They are not compatibility
contracts.

Downstream work proceeds in this order of responsibility, while native issue links own
the actual execution dependencies:

| Delivery area | Must preserve from this ADR |
|---|---|
| VTX-001 identities/descriptors | Strong generation-scoped identity, finite validation and no native/path types |
| VTX-002 artifacts/page store | Assets-owned immutable artifacts, page byte leases, target variants and no direct filesystem access |
| VTX-003 residency | Feature-local policy within ADR-034 and World Streaming reservations; bounded injected scheduling |
| VTX-004 feedback/prediction | Observations are demand evidence; finite readback and camera snapshots do not transfer authority |
| VTX-005 renderer/material | Renderer owns physical resources and safe mapping; Materials owns semantic sampling intent |
| VTX-006 producers/packaging | Typed immutable producer payloads; Packaging/Assets own paths, mounts and server variants |
| VTX-007 tooling/qualification | Read-only snapshots by default, explicit application commands and per-tier evidence |

No persisted format changes in this ADR. When exact schemas arrive, caches/artifacts
without the new schema/capability key are invalidated and recooked atomically. Runtime
must not guess old page geometry or reinterpret prototype `index.json` layouts.

## Consequences

Positive consequences:

- Logical page policy, GPU ownership, asset bytes, material semantics and world
  admission each have one authority.
- The feature can be omitted from 1.0, headless and unsupported products without
  contaminating generic renderer or asset contracts.
- Atlas and sparse paths share semantics while retaining independent admission and
  qualification.
- Later VTX tickets have explicit typed seams, lifecycle rules and unsupported paths.

Costs and trade-offs:

- Host composition and generation-checked adapters add more contracts than a
  singleton streamer that directly owns files and GPU resources.
- Optional fallback requires separately cooked/material-qualified variants and peak
  budget admission.
- Sparse capability cannot be advertised early from extension discovery alone;
  platform/backend qualification and RND-010.9 must finish first.
- Atomic replacement may temporarily retain two generations and therefore requires
  explicit overlap budgets.

## Rejected Alternatives

### Make Renderer own the complete VTX feature

Rejected because Renderer owns GPU realization, not asset/page meaning, producer
demand, material policy or logical residency. This would pull domain policy into every
backend and make headless validation depend on rendering.

### Make Assets own runtime residency and scheduling

Rejected because Assets owns artifacts and byte leases, not camera/world demand, GPU
mapping or frame-safe publication. A generic asset cache cannot become a hidden world
and GPU residency authority.

### Let World Streaming schedule every VTX page

Rejected because World Streaming admits cells and aggregate budgets; VTX page demand
is finer-grained and feature-specific. VTX consumes a bounded reservation and reports
readiness without creating a second cell state machine.

### Select support by backend name or product-profile rank

Rejected because API names do not prove formats, limits, synchronization, memory,
artifact or shader support. Effective capability and product content policy are
orthogonal inputs.

### Require virtual texturing for Horo 1.0

Rejected because the sparse/atlas residency foundation assigned to RND-010.9 is not
implemented or qualified. Ordinary texture variants keep 1.0 coherent; VTX remains a
post-1.0 opt-in capability.

### Keep direct cache paths and a VTX-global scheduler as temporary compatibility

Rejected because both create competing sources of truth that later migrations would
have to preserve. Adapters over Assets and the injected job contract provide an
incremental implementation seam without normalizing ambient state.
