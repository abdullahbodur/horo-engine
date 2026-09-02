# ADR-168: VTX GPU Page Table, Physical Cache, Shader and Material Ownership

- **Status**: Proposed
- **Date**: 2026-09-02
- **Supersedes**: None
- **Scope**: VTX logical mappings, Renderer page-table and physical-cache realization, sparse versus atlas paths, shader sampling contract, material binding, frame publication, device loss and migration
- **Issue**: [VTX-005.1](https://github.com/abdullahbodur/horo-engine/issues/2214)
- **Jira**: [HORO-2168](https://horo-engine.atlassian.net/browse/HORO-2168)
- **Parent**: [VTX-005](https://github.com/abdullahbodur/horo-engine/issues/2213)
- **Related**: [ADR-027](027-renderer-resource-identity-and-descriptors.md), [ADR-028](028-renderer-capability-limits-and-product-profiles.md), [ADR-034](034-gpu-memory-and-residency-ownership.md), [ADR-035](035-shader-source-and-intermediate-representation.md), [ADR-036](036-raster-render-path-and-quality-architecture.md), [ADR-164](164-virtual-texturing-ownership-product-scope-and-capability-tier.md), [ADR-165](165-virtual-texture-source-cooked-artifact-page-store-and-cache-ownership.md), [ADR-166](166-vtx-feature-local-residency-and-eviction-within-global-reservations.md), [ADR-167](167-vtx-feedback-readback-prediction-and-camera-data-ownership.md)
- **Normative documents**: [Virtual Texturing Architecture](../architecture/runtime/virtual-texturing-architecture.md), [Rendering Architecture](../architecture/runtime/rendering-architecture.md), [Material and Shader Model](../architecture/runtime/material-and-shader-model.md)

## Context

ADR-164 assigns logical page-table intent to VTX and physical atlas/sparse resources to
Renderer. The exact integration boundary is still ambiguous. A naive implementation
could put `TextureHandle` values in VTX state, let Renderer choose logical fallback or
eviction, store physical slot/descriptor indexes in Material instances, or compile a
new shader when a virtual page first appears.

Page visibility also spans multiple asynchronous owners. Asset bytes must be decoded,
uploaded, mapped and made visible to a frame without exposing a page-table entry before
its physical content is usable. Unmapping must stop future sampling before a physical
slot is reused, while previous frames may still read the old mapping. Sparse binding
and atlas indirection express this differently but must preserve one Horo semantic
contract.

RND-010.9 remains the generic capability-gated sparse-resource and admitted-atlas
foundation. VTX specializes logical page identity, mapping intent and sampling behavior
on top of it; it does not redefine Renderer allocation, binding or synchronization.

This ADR fixes ownership, realization and lifecycle. VTX-005.2 and later tickets freeze
exact capability tables, formats, page-table encodings, shaders, update algorithms and
tests without changing these authorities.

## Decision

### 1. Logical, material, shader and physical responsibilities stay separate

| Responsibility | Authority | Deliberate non-owner |
|---|---|---|
| Virtual page identity/generation, desired logical mapping revision and residency policy | VTX | Renderer cannot choose page priority, pin or victim |
| Semantic virtual-texture slot, sampling intent and required/fallback material variant | Materials | Material assets/instances hold no physical slot or native descriptor |
| Canonical backend-neutral sampling contract and offline variant/permutation inputs | Material/Shader model under ADR-035 | VTX does not compile runtime shaders; backends do not invent semantics |
| Cost/admission, page-table/physical-cache resources, uploads, mappings, descriptors, graph passes and GPU retirement | Renderer under ADR-027/034 | VTX owns no native image, heap, descriptor, queue or fence |
| Native sparse/atlas translation and synchronization | Selected Renderer backend | Public VTX/Material contracts contain no API types |
| Host selection of effective `Unavailable`/`Atlas`/`Sparse` plan | Application composition under ADR-164/028 | Neither VTX nor backend silently switches plan |

The composition root validates one immutable realization and material-variant plan
before VTX activation. Descriptor construction is inert and cannot allocate resources,
compile shaders, select a backend or mutate a material.

### 2. VTX publishes immutable logical mapping intent

VTX produces a finite plan equivalent in semantics to:

```cpp
struct VtxLogicalMappingPlan {
    VirtualTextureId texture;
    VirtualTextureGeneration generation;
    VtxMappingRevision expectedRevision;
    VtxMappingRevision candidateRevision;
    BoundedArray<VtxMapPageIntent> maps;
    BoundedArray<VtxUnmapPageIntent> unmaps;
    VtxFallbackPageSet fallbackPages;
    VtxMappingCost declaredCost;
};
```

Map intent identifies a logical page/content generation and an immutable decoded/upload
payload lease from ADR-165. Unmap intent identifies the exact currently published
logical mapping generation. The plan contains no physical cache coordinate, native
resource, descriptor index, GPU address, barrier, command list or fence.

Plans are canonical, duplicate-free and bounded. `expectedRevision` makes application
idempotent and rejects stale/reordered plans. A candidate revision does not become
visible merely because Renderer accepted work. VTX commits it only after a matching
Renderer completion is published at the declared safe point.

### 3. Renderer realizes the plan through generation-checked resources

Renderer translates one admitted logical plan into its private resources and graph
work. It owns:

- page-table image/buffer generations and update/patch storage;
- physical atlas images or sparse/tiled resource backing and private slot maps;
- upload/staging resources and format/layout conversion explicitly allowed by plan;
- descriptors/bind groups, samplers and pipeline/shader module instances;
- barriers, queue ownership transfers, sparse binding operations and submissions; and
- frames-in-flight tracking, retirement and device-loss reconstruction state.

Renderer validates logical identity, expected revision, content format/geometry,
declared cost, reservation, capability and resource generations before scheduling. A
physical slot assignment is private ephemeral realization state and is never returned
as stable page identity.

The backend returns a bounded `VtxMappingCompletion` with Horo texture/generation,
operation, candidate revision, per-intent outcome, effective realization mode and
Renderer resource generation. It exposes no native handles. Partial native success is
retained/rolled back inside Renderer; VTX receives no partially published revision.

### 4. Atlas and Sparse implement one sampling contract

`Atlas` realizes logical mappings through an admitted physical texture atlas plus a
Renderer-owned indirection/page-table resource. `Sparse` realizes them through qualified
native sparse/tiled backing and any Horo metadata needed to preserve the same semantics.
Both paths provide identical engine-facing rules for:

- logical page/mip/border and coordinate interpretation;
- unmapped-page result and declared fallback/mip-tail behavior;
- mapping revision visibility and generation validation;
- filtering/derivative/seam contract required by the material variant;
- bounded feedback identity under ADR-167; and
- upload-before-map, unmap-before-reuse and GPU-safe retirement.

Sparse is not permission to expose native tiles or skip a logical revision. Atlas is
not a silent fallback: it has its own format, page-table, shader, descriptor, memory,
upload and peak-overlap requirements admitted before activation. If the selected path
fails after activation, recovery/replacement uses a newly validated plan; no frame-hot
backend switch occurs.

### 5. Mapping publication is atomic from frame consumers' perspective

For every map, Renderer completes content upload/transition before a page-table or
sparse mapping that references it can become visible. For every unmap, future frame
snapshots stop observing the mapping before its backing is eligible for reuse. Exact
implementation may use double-buffered tables, versioned patches or another qualified
technique, but must publish one complete `VtxMappingRevision` at a Renderer safe point.

A render frame captures one immutable VTX binding snapshot containing the logical VTX
generation, mapping revision, Renderer resource generation and backend-neutral binding
handle. Every pass for that view uses the same snapshot. It cannot mix a new table with
old cache content or observe mid-frame patch progress.

Old table/cache/descriptor generations remain retained for their last submitted frame,
queue use, feedback pass and readback dependency. Frame index modulo, CPU release or
new-revision publication alone cannot authorize reuse. Stalled retirement remains
charged and observable.

### 6. Materials own semantic references, not resident state

Authored/cooked Materials declare typed virtual-texture slots with semantic role,
logical `AssetId`/binding reference, sampling intent, required capability and named
compatible fallback variant. Slots never serialize a physical page, atlas coordinate,
descriptor index, backend format enum, resource handle or mapping revision.

Material Cook and ADR-035 produce every permitted shader/material variant offline from
versioned semantic inputs. The VTX sampling contract is a registered, reflected shader
interface with bounded slot counts and exact resource/constant requirements. Runtime
does not concatenate source, compile a missing permutation or reinterpret an ordinary
texture shader after a page miss.

At activation/extraction, a host/Renderer adapter resolves the material's logical VTX
reference to a generation-checked backend-neutral binding snapshot. Material instances
may retain that snapshot lease for a frame/operation but cannot update page tables,
select physical slots or outlive the owning generations. Missing/stale required
bindings fail the material/scene candidate; optional fallback was selected and admitted
before publication.

### 7. Missing pages have declared shader semantics

The canonical sampling contract distinguishes a resident mapping, declared fallback/
mip-tail mapping and invalid/unavailable binding. Exact encoded bits and filtering
algorithm are downstream decisions, but shader behavior must be deterministic for one
captured revision and cannot sample uninitialized backing.

A page miss may produce bounded feedback evidence and sample the admitted fallback
page/variant. It cannot allocate, block, compile, change material state or map a page
from shader execution. Required fallback pages/tails are pinned and budgeted under
ADR-166; inability to maintain them is a typed composition/runtime failure, not an
undefined texture read.

### 8. Capability admission uses exact operations and limits

Effective realization is the intersection of compiled frontend/backend implementation,
device operations, exact image/buffer formats, sparse granularity when applicable,
descriptor/binding limits, shader features, filtering, feedback support, queue/sync
behavior, material variants, artifact encodings and all ADR-166/034 budgets.

An API name, extension bit, product profile rank or successful resource creation alone
does not advertise support. Sparse granularity must be compatible with the cooked VTX
geometry or an explicitly cooked conversion path. Atlas and Sparse each undergo their
own admission and qualification. `Unavailable` creates none of these resources.

### 9. Failure, replacement and device loss preserve generations

Typed results distinguish unsupported realization/format/geometry/shader variant,
budget/reservation denial, stale mapping/resource generation, invalid plan, upload,
mapping, descriptor/pipeline, device loss, cancellation and retirement stall.

Failure before mapping-revision publication leaves the previous active revision intact
when its device generation remains valid. Cancellation prevents candidate publication
but retains payload, staging, mapping and resource leases until Renderer reports their
terminal state. Replacement admits old/new peak resources, prepares a detached binding
generation and atomically swaps at safe points.

Device loss immediately invalidates the Renderer resource generation and therefore all
bindings, without changing logical VTX/artifact identity. Recovery follows ADR-027:
the host admits a compatible new realization, VTX reissues finite mapping intent from
its valid logical residency/artifact leases, and Materials resolve new binding
snapshots. Old native handles are never serialized, compared or restored.

### 10. Diagnostics and qualification stay backend neutral

Diagnostics expose safe VTX/mapping/resource generations, realization mode, finite
operation counts/costs, material slot/variant identity and typed stage/result. Physical
atlas coordinates, sparse tile handles, descriptor/GPU addresses, pointers and native
barrier structures remain private. Debug UI submits application operations and consumes
snapshots; it does not patch a live page table.

Qualification covers Atlas and Sparse independently: exact-cap and format/granularity
rejection; upload-before-map; atomic multi-page revision; partial native failure;
unmap-before-reuse across every queue/frame; required tail behavior; descriptor
exhaustion; stale/reordered/duplicate plans; material fallback admission; replacement
overlap; device loss/reconstruction; full queues with progressing cleanup; and no
same-frame GPU wait or runtime shader compilation.

### 11. Migration removes physical state from VTX and Materials

Prototype VTX structs containing `TextureHandle`, CPU/GPU page-table mirrors or
physical page indexes are not compatibility contracts. Logical residency migrates to
generation/revision intent; Renderer privately owns realization. Material fields that
store descriptor/atlas indexes migrate to logical typed slots and cooked variant
requirements. Backends delete VTX policy branches and implement only the admitted Horo
realization/sampling contract.

No persisted prototype GPU layout is migrated. Exact VTX artifact/material/shader
schema versions invalidate and recook affected data when downstream formats land.

## Consequences

Positive consequences:

- VTX controls logical residency without depending on native renderer types.
- Atlas and Sparse remain interchangeable only where they prove the same semantics.
- Materials are stable across mapping revisions and backend/device replacement.
- Atomic frame snapshots prevent half-updated table/cache combinations.

Costs and trade-offs:

- Mapping operations correlate VTX, artifact, reservation and Renderer generations.
- GPU-safe unmap/reuse can retain old page tables, descriptors and backing for several
  frames and must be budgeted.
- Both Atlas and Sparse need independently cooked shaders/artifacts and qualification.
- Exact encoding/algorithm choices remain downstream work.

## Rejected Alternatives

### Put renderer handles and physical slots in VTX state

Rejected because it couples logical identity/lifetime to one device generation and lets
VTX bypass Renderer allocation, synchronization and reconstruction.

### Store atlas/descriptor indexes in Material assets or instances

Rejected because indexes are ephemeral Renderer state. Materials own semantic slots and
variant intent; frame binding snapshots own resolved leases.

### Treat Sparse as the semantic model and Atlas as a reduced fallback

Rejected because native sparse APIs differ and cannot define portable page/fallback/
revision behavior. Both realize one Horo contract and are admitted independently.

### Patch visible page tables immediately as uploads complete

Rejected because a frame could observe partial revisions or mappings before content is
usable. Publication is complete and safe-point atomic.

### Compile a VTX shader variant on the first page miss

Rejected because runtime misses cannot create shader authority, unbounded stalls or an
uncooked fallback. All variants are produced and admitted before activation.
