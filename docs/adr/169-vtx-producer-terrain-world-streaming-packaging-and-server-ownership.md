# ADR-169: VTX Producer, Terrain, World Streaming, Packaging and Server Ownership

- **Status**: Proposed
- **Date**: 2026-09-02
- **Supersedes**: None
- **Scope**: Generic and Terrain virtual-page producers, immutable input/hint boundaries, invalidation, World Streaming readiness, package reachability, fallback closure and headless/dedicated-server composition
- **Issue**: [VTX-006.1](https://github.com/abdullahbodur/horo-engine/issues/2224)
- **Jira**: [HORO-2178](https://horo-engine.atlassian.net/browse/HORO-2178)
- **Parent**: [VTX-006](https://github.com/abdullahbodur/horo-engine/issues/2223)
- **Related**: [ADR-012](012-world-streaming-partition-authority-and-subsystem-boundaries.md), [ADR-028](028-renderer-capability-limits-and-product-profiles.md), [ADR-057](057-package-manifest-v1-typed-model.md), [ADR-060](060-release-domain-model-and-state-machine.md), [ADR-137](137-terrain-foliage-ownership-data-tier-and-lifecycle.md), [ADR-138](138-terrain-source-cooked-tile-cache-and-streaming-ownership.md), [ADR-164](164-virtual-texturing-ownership-product-scope-and-capability-tier.md), [ADR-165](165-virtual-texture-source-cooked-artifact-page-store-and-cache-ownership.md), [ADR-166](166-vtx-feature-local-residency-and-eviction-within-global-reservations.md), [ADR-167](167-vtx-feedback-readback-prediction-and-camera-data-ownership.md), [ADR-168](168-vtx-gpu-page-table-physical-cache-shader-and-material-ownership.md)
- **Normative documents**: [Virtual Texturing Architecture](../architecture/runtime/virtual-texturing-architecture.md), [Terrain and Foliage Architecture](../architecture/runtime/terrain-and-foliage-architecture.md), [World Streaming Architecture](../architecture/runtime/world-streaming-architecture.md), [Asset Pipeline](../architecture/runtime/asset-pipeline.md), [Release Architecture](../architecture/release/release.md)

## Context

The VTX foundation now separates Assets artifacts, VTX residency and Renderer
realization. It still needs a producer boundary. Generic textures/materials and Terrain
can both supply virtual-page source and demand hints, but they have different canonical
truth and invalidation rules. A producer callback that hands mutable pixels, paths or
live Terrain objects to VTX would transfer ownership implicitly and make deterministic
cook, cancellation and replacement impossible.

World Streaming integration is also easy to invert: Terrain/VTX providers might start
their own cell work from camera observations, or VTX might treat page readiness as cell
authority. Packaging could include every VTX cache entry, omit a required ordinary
fallback, or place client-only GPU artifacts in dedicated-server products.

This ADR fixes producer contribution, invalidation, streaming, package reachability and
server rules. VTX-006.2 and later tickets freeze exact producer APIs/adapters and tests
without changing the domain authorities below.

## Decision

### 1. Producers contribute immutable inputs; they do not transfer canonical truth

| Responsibility | Authority | Deliberate non-owner |
|---|---|---|
| Generic texture/material source identity, dependencies and authored sampling/fallback intent | Assets and Materials | VTX producer does not rewrite source/material assets |
| Terrain source, dataset/tile/foliage semantics, runtime generation and cell contribution | Terrain under ADR-137/138 | VTX does not become Terrain height/layer/tile authority |
| Deterministic virtual-page derivation and VTX artifact schema | VTX Cook under ADR-165 | Producer does not publish packs or cache generations |
| Runtime page residency/prediction | VTX under ADR-166/167 | Producer hints do not load, pin or evict directly |
| Cell demand/reservation/readiness barrier | World Streaming under ADR-012 | VTX/Terrain do not create a global cell scheduler |
| Product reachability, target variant closure, package assembly and verification | Release/Assets | VTX runtime does not select package contents |
| Effective VTX/fallback activation | Application host | Release, producer and backend do not silently select policy |

Producer descriptors are inert catalog metadata. Registration validates stable producer,
input/output schema and capability identities but performs no source scan, cook, Terrain
query, page generation, scheduler registration or global mutation.

### 2. The producer contract is snapshot/result based

A cook contribution has semantics equivalent to:

```cpp
struct VtxProducerInputSnapshot {
    VtxProducerId producer;
    VtxProducerSchemaVersion schema;
    AssetGenerationId sourceGeneration;
    VtxProducerContentRevision content;
    BoundedArray<ArtifactDependency> dependencies;
    VtxSourceRegionSet regions;
    VtxProducerRequirements requirements;
};

struct VtxProducerResult {
    VtxProducerOperationId operation;
    VtxProducerContentRevision content;
    BoundedArray<VtxCanonicalPageInput> pages;
    VtxProducerDigest semanticDigest;
    BoundedDiagnostics diagnostics;
};
```

Exact API/layout is downstream work. The snapshot owns or leases immutable bounded data
for one operation and records every semantic dependency/generation. Results are detached,
canonical, deterministic and host-validated before VTX Cook can consume them. They
contain no source paths, editor/Terrain pointers, mutable material state, native GPU
objects, world reservations or publication handles.

Workers cannot retain borrowed views after completion. Cancellation invalidates result
publication but retains source/module leases until work terminates. Reordered or
duplicate completion is matched by operation/content generation and cannot replace a
newer candidate.

### 3. Generic texture/material production preserves existing owners

The generic producer consumes an exact Assets source generation plus normalized import
settings and Material semantic requirements. It validates dimensions/channels/color/
alpha/addressing and emits canonical page inputs for ADR-165 cook. Assets remains the
source/dependency/publication authority; Materials remains slot, sampling and fallback
authority.

One source may feed ordinary texture and VTX variants. They are separately keyed
artifacts linked by declared product compatibility, not two mutable views of one runtime
object. VTX Cook cannot infer missing Material intent from filename, shader name or
backend. Material changes that affect sampling, borders, encoding or fallback change the
dependency fingerprint and recook the VTX generation.

### 4. Terrain is an adapter and dependency, not VTX source state

The Terrain adapter captures one immutable canonical Terrain source/cooked dataset or
runtime hint snapshot from the Terrain owner. It may project declared layer/material
regions and stable Terrain tile/LOD provenance into VTX producer inputs. VTX never reads
Terrain internal arrays, watches Terrain files, holds tile pointers or edits Terrain
source/runtime state.

Cooked Terrain-derived VTX artifacts record exact Terrain dataset/content/tile and
material dependency generations/digests. A terrain sculpt/paint/reimport or dependency
change first commits through Terrain/Assets, then invalidates affected VTX cook keys.
Invalidation identifies a bounded typed region/dependency closure; it is not permission
to patch a published VTX pack or active mapping in place.

Runtime Terrain may publish bounded visibility/coverage hints under ADR-167 and supply
required page sets/readiness expectations through its World Streaming provider adapter.
Hints remain evidence. Terrain cannot allocate VTX/Renderer budget, choose page victims
or mark a cell ready because its own tile is ready.

### 5. Invalidation creates candidates and never mutates active generations

Assets dependency changes, producer revisions and Terrain/material invalidations enter
an application-owned recook/replacement operation. The operation captures exact old/new
revisions, computes the deterministic affected closure and either reuses byte-identical
verified pages/packs or produces a complete new ADR-165 generation.

Publishing a new Assets generation does not mutate VTX runtime. The host pins and
validates it, admits replacement overlap, prepares required logical/Renderer state and
atomically swaps under ADR-164/168. Failure, cancellation, newer supersession or stale
producer input preserves the last valid source, cooked and active generations.

No producer callback writes active decoded pages, page-table entries, package manifests
or cache files. Runtime page misses never invoke a source producer or cooker.

### 6. World Streaming owns aggregate commands and barriers

For a streamed cell, World Streaming captures required/optional VTX content identity,
page/readiness requirements, criticality and a generation-scoped reservation in the
provider stage request. VTX validates it and performs feature-local page work inside the
slice. Terrain and other producers may contribute hints but do not initiate a second
cell load.

VTX reports prepared/failed/retiring evidence for the exact cell and VTX generations.
World Streaming alone commits the aggregate cell when every required provider is ready,
selects an admitted fallback, or begins eviction. `Resident` VTX pages do not imply an
active cell; cell eviction does not permit force-freeing shared/pinned pages before all
consumer leases and Renderer work retire.

Shared pages use the ADR-166 one-charge/multiple-consumer model. Neither Terrain nor VTX
may hide cross-cell charges or transfer reservation ownership independently.

### 7. Fallback responsibility is end-to-end and explicit

Fallback is not owned by one runtime module:

| Stage | Required responsibility |
|---|---|
| Authoring/Materials | Declare VTX requirement and exact compatible ordinary/VTX alternative |
| Producer/VTX Cook | Validate semantic compatibility and emit separately keyed variants/costs |
| Assets dependency graph | Retain exact variant identities and transitive closure |
| Release | Include every variant permitted by the selected product fallback plan |
| Host admission | Select one supported variant before scene/material publication |
| VTX runtime | Execute only the selected VTX plan; report failure without inventing another |

Sparse-to-Atlas and VTX-to-ordinary-texture paths are distinct choices. A producer cannot
label arbitrary lower-resolution output as compatible; Material/content validation owns
semantic equivalence requirements. Runtime does not recook, download or discover a
fallback after failure.

### 8. Release packages only the selected reachable closure

Release captures an immutable product/target/profile plan and walks the Assets dependency
graph from selected scene/world/material roots. For a VTX-enabled client it includes the
exact published VTX root manifest/page packs, compatible Material/shader variants,
producer dependencies needed at runtime and every admitted fallback. Source assets,
editor state, cook cache, staging outputs and producer implementation state are excluded.

World cell blocks reference exact VTX artifact/generation requirements; they do not embed
a competing page manifest. Release may co-locate packs into package chunks while
preserving artifact/range/digest identity. Package verification replays schema,
reachability, required/fallback completeness, target/tier and integrity checks before
signing/publication.

Unused variants and unrelated cache pages are not packaged by directory scanning. A
required VTX root without its complete selected pack/material/shader closure fails the
release candidate. A fallback declared in policy but absent from the package also fails.

### 9. Dedicated/headless products exclude GPU VTX by default

`game-dedicated-server` and ordinary headless runtime plans resolve VTX realization to
`Unavailable`. They do not compose VTX runtime residency, Renderer page tables, feedback,
GPU material variants or client-only page packs, and those artifacts are not reachable
from the server package merely because a shared source/material has VTX variants.

Server-required Terrain collision/navigation/gameplay data remains owned and packaged by
those domains; VTX visual pages are not canonical Terrain or gameplay state. A listen
server includes client VTX artifacts only for its explicitly composed local graphical
client role, with client budgets/lifetime isolated from authoritative server simulation.

Cook, package-validation or dedicated VTX artifact-test tools may explicitly compose
VTX Model/Cook/validation contracts without GPU residency. Their artifacts remain
operation outputs, not evidence that the server runtime supports VTX. Future nonvisual
server use requires a new product capability decision; it cannot be inferred from
headless mode.

### 10. Lifecycle, errors and diagnostics retain provenance

Producer module/source/dependency leases outlive every running cook task and detached
result validation. Runtime hint leases outlive ingestion but do not transfer producer
state. Shutdown closes new captures, cancels/yields owned operations, rejects late
generations and releases dependencies only after workers/results retire.

Typed failures distinguish producer/schema unsupported, invalid/missing/stale input,
dependency cycle/mismatch, nondeterministic output, invalidation overflow, VTX cook/
publication failure, cell reservation/readiness, package reachability/fallback missing,
server-incompatible artifact and retirement stall.

Diagnostics expose safe producer/source/content/artifact/cell generations, bounded
affected counts/costs, package reachability reason and normalized causes. They do not
dump source/page bytes, paths, Terrain internals, package secrets or native handles.

Qualification covers generic/Terrain deterministic production, malformed/oversized
snapshots, reordered dependencies/jobs, stale invalidation, partial recook/reuse, runtime
miss never cooking, cell readiness/eviction/shared pages, required fallback closure,
package corruption/missing packs, dedicated-server exclusion, listen-server isolation,
cancellation/replacement and module/source lease retirement.

### 11. Migration removes direct callbacks and implicit package inclusion

Prototype producers that return mutable pixel pointers, paths, live Terrain objects or
write page files migrate to snapshot/result contributions inside Assets cook operations.
File watchers invalidate Assets/producer revisions, not active VTX pages. World Streaming
adapters replace direct camera-to-page scheduling.

Release manifests stop including VTX content via cache-directory patterns. They use typed
dependency reachability and explicit product fallback plans. Server profiles remove VTX
visual artifacts unless a future decision adds a concrete server capability.

## Consequences

Positive consequences:

- Generic and Terrain producers integrate without surrendering canonical source state.
- Invalidation remains deterministic, atomic and generation safe.
- World Streaming retains global scheduling and cell barrier ownership.
- Client packages contain complete VTX/fallback closures while servers avoid irrelevant
  GPU content and runtime work.

Costs and trade-offs:

- Producer snapshots/results and dependency closure add explicit schema/provenance data.
- Partial Terrain/material changes still publish a complete coherent VTX generation.
- Fallback variants increase cook/package size when product policy permits them.
- Listen servers need explicit separation of graphical client and server artifact/budget
  scopes.

## Rejected Alternatives

### Let producers push mutable pages directly into VTX

Rejected because it transfers lifetime implicitly, bypasses deterministic cook/
publication and permits stale callbacks to mutate active generations.

### Make Terrain VTX pages the canonical Terrain representation

Rejected because Terrain source/runtime/collision/navigation semantics must survive
without VTX or Renderer. VTX pages are derived visual artifacts.

### Let VTX decide cell readiness or global scheduling

Rejected because World Streaming owns aggregate demand, reservations and activation/
eviction barriers. VTX reports feature evidence only.

### Package every VTX cache entry to avoid reachability errors

Rejected because cache contents are derived, may include stale/untrusted targets and do
not define product closure. Release walks typed published dependencies.

### Include VTX artifacts in all dedicated-server packages

Rejected because visual VTX state is not authoritative gameplay/Terrain data and would
add unsupported bytes, attack surface and runtime ambiguity.

### Choose or generate fallback at runtime

Rejected because compatibility, shader/artifact availability and peak cost must be
cooked, packaged and admitted before publication.
