# ADR-165: Virtual Texture Source, Cooked Artifact, Page Store and Cache Ownership

- **Status**: Proposed
- **Date**: 2026-09-02
- **Supersedes**: None
- **Scope**: Virtual-texture source/import ownership, deterministic cook identity, immutable generation manifests and page packs, page-store reads, cache classes, integrity, publication, replacement, packaging and migration
- **Issue**: [VTX-002.1](https://github.com/abdullahbodur/horo-engine/issues/2185)
- **Jira**: [HORO-2139](https://horo-engine.atlassian.net/browse/HORO-2139)
- **Parent**: [VTX-002](https://github.com/abdullahbodur/horo-engine/issues/2184)
- **Related**: [ADR-008](008-error-model-exception-boundary-and-registry.md), [ADR-010](010-job-waiting-and-operation-store-ownership.md), [ADR-027](027-renderer-resource-identity-and-descriptors.md), [ADR-034](034-gpu-memory-and-residency-ownership.md), [ADR-035](035-shader-source-and-intermediate-representation.md), [ADR-057](057-package-manifest-v1-typed-model.md), [ADR-138](138-terrain-source-cooked-tile-cache-and-streaming-ownership.md), [ADR-164](164-virtual-texturing-ownership-product-scope-and-capability-tier.md)
- **Normative documents**: [Asset Pipeline](../architecture/runtime/asset-pipeline.md), [Virtual Texturing Architecture](../architecture/runtime/virtual-texturing-architecture.md)

## Context

ADR-164 assigns logical page identity and residency intent to VTX while Assets owns
source identity, deterministic cook, immutable publication and byte leases. The
remaining artifact boundary is not yet implementable. The old VTX description showed
one mutable file per page beneath a runtime-constructed build/cache path plus an
`index.json`. It did not distinguish source truth, derived cook cache, selected
published generation, packaged page storage, provider caches, decoded VTX pages or GPU
residency.

One-file-per-page storage would couple logical page identity to filesystem layout,
create very large directory/file counts, make aggregate publication non-atomic and
invite runtime directory scanning. A single monolithic blob would solve file count but
force large reads and make localized corruption/retry expensive. Neither approach by
itself defines complete cook identity, exact-generation reads, integrity boundaries or
lease lifetime.

Virtual-texture artifacts must support independently requested logical pages without
making each page a mutable asset. They must work identically from local cache, package,
archive, memory/test and future remote transports. Cache hits and fresh cook output
must pass the same validation, while corruption, cancellation or replacement must not
alter the last valid published or active generation.

This ADR defines those ownership and aggregation rules. VTX-002.2 through VTX-002.9
will freeze source schemas, exact IDs, binary layouts, algorithms, queue bounds and
tests without introducing a second store or publication authority.

## Decision

### 1. Assets owns storage and publication; VTX owns page semantics

| Responsibility | Authority | Deliberate non-owner |
|---|---|---|
| Tracked source `AssetId`, source/sidecar byte snapshots, path trust and immutable input borrows | Assets/Application import operation | VTX import/cook does not retain paths or mutable source buffers |
| External image/container decode and canonical VTX source/settings validation | VTX Import/Model contribution | Generic Assets does not interpret page, channel, color or wrap semantics |
| Generic cooker registry, dependency scheduling, cache keys/store, operation staging and atomic generation publication | Assets Cook | VTX does not create a scheduler, cache root or current-generation pointer |
| Page geometry, mip/tail derivation, border/addressing/color semantics, page ordering and VTX artifact schema | VTX Cook contribution | Assets does not choose page layout or reinterpret payloads |
| Selected immutable generation, package membership and artifact byte/range leases | Assets provider/package system | VTX runtime does not select files, archives or transport |
| Manifest semantic validation, bounded page requests, decoded neutral page cache and logical residency | VTX runtime | Assets provider does not choose pages or publish residency |
| Physical atlas/sparse resources, uploads, mappings and GPU retirement | Renderer under ADR-027/034 | VTX artifact/cache identity is not a GPU resource identity |

VTX contributions use host-owned importer/cooker catalogs, bounded borrowed input views
and host-owned output writers. Registration is inert: it validates stable contribution,
schema and dependency identities but performs no source scan, cook, cache access or
global mutation. No generic Assets target depends on VTX implementation targets.

### 2. External inputs and canonical authored source are distinct

External images, UDIM sets, layer groups and future provider formats are untrusted
import inputs. Import captures exact bytes and explicit options into a detached
candidate, validates finite dimensions/layers/channels/mips, checked decoded sizes,
color/alpha semantics and dependency closure, then publishes one canonical authored
VTX source revision through Assets.

Filenames, directory enumeration, locale, codec defaults and external timestamps do
not determine page ordering or semantic settings. Multi-file inputs are sorted by
canonical typed coordinate/role, and every member has a stable dependency identity and
digest. Missing, duplicate, overlapping or changing members fail the candidate.

The authored source remains mutable only through editor/application asset operations.
Reimport compares expected revisions and cannot overwrite edits silently. It stores
portable semantic source/settings and provenance, not runtime page packs, absolute
paths, cache locations, provider handles or GPU state.

### 3. One immutable generation manifest indexes bounded page-pack artifacts

VTX Cook emits a root `CookedVirtualTextureManifest` plus a canonically ordered set of
immutable page-pack artifacts. Their exact wire layouts are deferred, but the semantic
contract is:

```cpp
struct CookedVirtualTextureManifest {
    VirtualTextureId texture;
    VirtualTextureContentRevision content;
    VirtualTextureCookSchemaVersion schema;
    VirtualTextureCapabilityTier tier;
    VirtualTextureDescriptor descriptor;
    BoundedArray<VirtualPagePackEntry> packs;
    VirtualTextureDependencyFingerprint dependencies;
    ArtifactDigest rootDigest;
};

struct VirtualPagePackEntry {
    VirtualPagePackId pack;
    ArtifactId artifact;
    BoundedArray<VirtualPageRangeEntry> pages;
    ByteCount storedBytes;
    ByteCount decodedPeakBytes;
    ArtifactDigest digest;
};

struct VirtualPageRangeEntry {
    VirtualPageId page;
    ByteOffset offset;
    ByteCount storedBytes;
    ByteCount decodedBytes;
    ArtifactDigest digest;
};
```

A logical page is the smallest independently requested and integrity-verified VTX
content unit. A page pack is the smallest generic Assets artifact/range-addressable
storage unit. Pages do not receive independent source `AssetId`s, sidecars,
`current.json` pointers or mutable files. The manifest, not directory contents,
defines complete membership and canonical order.

Packs bound file/object count while permitting finite range reads and localized retry.
Pack grouping is a deterministic cook-policy input based on typed page coordinates,
mip/tail policy and declared bounds. Completion order, worker count and storage path do
not influence grouping. Mip-tail data may use a dedicated pack but remains manifest-
declared and versioned rather than hidden trailer policy.

Payloads contain VTX-neutral compressed/encoded page content and required semantic
metadata. They contain no source/editor state, absolute paths, runtime cache slots,
world reservations, renderer handles, native format enums, commands or fences.

### 4. Cook identity closes over every byte- or meaning-affecting input

The VTX dependency-aware cook key extends the Assets key with:

- exact canonical source identity, revision and digest;
- canonically ordered input-member and dependency artifact identities/digests;
- import settings and their schema, including color/alpha/channel/addressing intent;
- virtual dimensions, page/border/mip/tail geometry and edge-padding policy;
- target format/encoding/compression and exact VTX capability requirements;
- pack aggregation/order policy and all finite effective limits;
- source, manifest, pack, page payload, cooker and algorithm versions; and
- generic Assets target, envelope and toolchain identity.

Paths, timestamps, editor layout, job order, thread count, active provider/cache state,
GPU device identity and native runtime handles are excluded unless an explicit target
artifact format semantically depends on a typed qualified capability fingerprint.

Same complete inputs produce byte-identical manifest/pack/page bytes and diagnostic
ordering for qualified tools. Any semantic input change changes the key. Fresh output
and cache reuse pass identical envelope, requested-key, manifest, size, digest, bounds
and VTX semantic validation before generation assembly.

### 5. Integrity is layered and validated before allocation or publication

Assets validates generic artifact envelopes, requested cache key, declared sizes and
cryptographic digests. VTX then validates the root manifest and page semantics:

- supported schema/tier/format and exact descriptor compatibility;
- checked counts, dimensions, offsets, lengths, alignment and decoded peak sizes;
- canonical unique pack/page IDs and complete required page/mip/tail coverage;
- sorted non-overlapping ranges wholly inside the declared pack;
- pack digest plus per-page digest after the specified bounded decode;
- dependency fingerprint and artifact-generation membership; and
- no unknown required flags, trailing semantic data or ambiguous aliases.

Validation occurs before corresponding allocation/read work whenever metadata allows.
Decompression has declared input, output, ratio, time/work and scratch bounds; a
compressed payload cannot allocate from an untrusted header. A verified pack does not
waive per-page bounds/digest checks, and a page digest does not authorize bytes from a
different generation.

Partial pack availability, one corrupt page, wrong target/tier, truncated data,
duplicate ranges, arithmetic overflow or digest mismatch fails the affected candidate
with a typed result. It never causes directory repair, source import, runtime cooking or
publication of the remaining pages as a complete generation.

### 6. Runtime reads use an exact-generation page-store port

The host provides a narrow Assets-owned page-store/range-read capability with semantics
equivalent to:

```cpp
struct VirtualPageReadRequest {
    AssetGenerationId generation;
    ArtifactId packArtifact;
    VirtualPageId page;
    ByteRange verifiedRange;
    ArtifactDigest expectedPackDigest;
    ArtifactDigest expectedPageDigest;
    ByteCount maximumResultBytes;
    AssetReadOperationId operation;
};

class IVirtualPageStore {
public:
    virtual VirtualPageReadHandle ReadPage(VirtualPageReadRequest request) = 0;
};
```

This is a semantic sketch; later API work fixes ABI/layout and may expose a generic
Assets range-reader instead of a VTX-named interface. In either case, the provider pins
the exact published/package generation and returns an immutable bounded byte lease plus
typed completion. It owns filesystem/archive/network/memory access, range coalescing,
transport retries and provider byte caches.

VTX verifies the request/completion generation, operation and page identity before
decode and publication. It cannot pass a path, ask for “current”, enumerate a pack,
mutate provider bytes or retain a borrowed span after its lease. Providers cannot infer
page priority, choose a fallback artifact or publish VTX residency.

### 7. Every cache name identifies one owner and lifetime

| Cache/store | Owner | Contents | Eviction effect |
|---|---|---|---|
| Authored source storage | Assets/editor document | Mutable versioned source truth | Asset transaction only; not a cache |
| Cook cache | Assets | Immutable content-addressed manifest/pack candidates | Never changes a published/active generation |
| Published generation | Assets | Verified root plus exact artifact closure | Replaced only by atomic generation publication |
| Package/archive store | Assets/Release | Immutable selected generation artifacts | Package/mount lifecycle only |
| Provider byte/range cache | Assets provider | Immutable pack/range bytes under leases | Only after provider leases release |
| Decoded neutral page cache | VTX runtime | Decoded pages for one VTX generation | VTX may evict disposable pages inside its reservation |
| Physical page cache | Renderer | Atlas/sparse resources and mappings | Renderer retirement; never by Assets cache policy |

The same bytes may have related accounting projections, but every allocation has one
charge and release authority. Evicting a cook/provider cache entry does not unpublish a
generation or unmap a resident GPU page. VTX eviction does not delete artifact bytes or
refund a Renderer heap. A new published generation does not invalidate leases held by
the active old VTX generation.

### 8. Publication, loading and replacement are generation atomic

Cook workers write operation-owned staged manifest/pack candidates. Assets verifies
the complete closure, obtains its publication lock and publishes one immutable
generation atomically. Cancellation, failure, stale source, lock timeout or process
interruption leaves the previous published generation selected and staged data
recoverable/quarantined under generic Assets policy.

Runtime pins one exact generation, validates its root before requesting pages and keeps
the manifest lease until every page request, decoded cache entry and dependent renderer
operation releases that generation. It never repeatedly follows `current.json`, mixes
packs from two generations or revalidates a page by matching coordinates alone.

Hot reload prepares a detached VTX generation. Unchanged packs/pages may be shared only
when complete artifact/page identity and semantic compatibility match. Publication of
the Assets generation does not mutate active VTX state; ADR-164 replacement admission
prepares required pages/resources and atomically publishes the new logical root. Failure
preserves the last valid active generation. Old leases retire after their last CPU/GPU
consumer, not when a filesystem pointer changes.

### 9. Packaging preserves the manifest closure and runtime has no build paths

Packaging includes the selected root manifest and exact pack/dependency artifacts for
the declared product/world chunks. Source inputs, editor state, cook-cache entries,
staging directories and mutable page side files are excluded. Package tables may
physically coalesce artifacts, but retain exact artifact/range/digest identity exposed
through the same provider contract.

Runtime receives artifact identities and leases from the mounted Assets generation. It
does not construct `<project>/build/...`, cache roots, pack filenames or page paths.
Local development, packaged archive, in-memory tests and future remote stores must be
semantically interchangeable at the page-store port.

### 10. Errors, diagnostics and qualification are typed and bounded

Stable result families distinguish invalid/untrusted source, unsupported source/schema/
tier/format, dependency missing/stale, cook nondeterminism, cache wrong-key/corrupt,
manifest invalid, pack/page missing/corrupt/oversized, provider capacity/transport,
stale generation, cancellation and retirement stall. Later tickets assign exact codes.

Diagnostics expose bounded Horo asset/generation/pack/page identities, stage, declared
and actual byte counts, cache class/hit result and normalized cause. They do not expose
absolute paths, source/page bytes, credentials, provider URLs, native handles or
unbounded third-party messages.

Required evidence includes deterministic fresh/cache output; reordered inputs and job
completion; malformed dimensions/counts/ranges/offset arithmetic; compression bombs;
wrong key/generation/target/tier; missing/duplicate/overlapping pages; partial/corrupt
packs; cancellation at every stage; concurrent publication; interrupted staging;
package/local/memory provider parity; hot replacement; held old leases; and cleanup
when normal request queues are full.

### 11. Migration removes mutable per-page layouts

The prototype per-page directory and `index.json` tree is not a supported persisted
format. No compatibility reader, directory scanner or runtime path adapter is retained.
Existing prototype output is treated as derived cache, invalidated and recooked into a
versioned root-plus-pack generation when the schemas land.

Downstream tickets must preserve:

| Delivery | Constraint |
|---|---|
| VTX-002.2/.3 | Canonical source/settings and exact artifact IDs/layouts refine this model without path identity |
| VTX-002.4/.5 | Page generation/manifest and packing algorithms remain deterministic, bounded and manifest-driven |
| VTX-002.6 | Cooker uses generic Assets scheduling, staging, cache and publication |
| VTX-002.7 | Runtime provider pins exact generation and returns immutable bounded leases |
| VTX-002.8/.9 | Corruption, cancellation, replacement and tests cover every store/cache boundary |

## Consequences

Positive consequences:

- Runtime page access works across filesystem, package, memory and future transports
  without exposing their layout to VTX.
- Root-plus-pack aggregation avoids both mutable per-page file explosion and mandatory
  whole-texture reads.
- Layered integrity localizes failures while atomic publication prevents mixed
  generations.
- Cook, provider, decoded and GPU caches can evolve independently because their owners
  and lifetimes are explicit.

Costs and trade-offs:

- Manifests and packs require range tables, two integrity levels and exact-generation
  lease tracking.
- A small page request may retain/coalesce a larger provider pack range; later tickets
  must set evidence-based pack and cache bounds.
- Hot replacement may temporarily retain old/new manifests, packs, decoded pages and
  GPU resources.
- Exact binary layouts and numeric limits remain blocked on downstream schema tickets;
  this ADR deliberately fixes semantics without guessing those values.

## Rejected Alternatives

### Store one mutable file and sidecar per page

Rejected because logical identity would become filesystem layout, aggregate publication
would not be atomic, directory/file counts would scale poorly and runtime scanning
would create a second manifest authority.

### Store each virtual texture as one monolithic artifact

Rejected because independent page demand would require oversized reads or an implicit
unverified internal range protocol. Bounded immutable packs retain aggregation while
making range and integrity semantics explicit.

### Let VTX own its cache root and filesystem reader

Rejected because Assets already owns path trust, content-addressed storage, package
mounts, byte leases and publication. A VTX filesystem creates duplicate security,
cache, generation and cancellation policy.

### Give every page a normal AssetId and current generation

Rejected because pages are members of one VTX semantic generation, not independently
authored assets. Per-page publication could combine incompatible cook generations and
explode registry/dependency state.

### Follow the latest published generation on every page request

Rejected because one live VTX root could then mix page geometry, encoding, dependencies
or semantic revisions. Runtime pins and retires exact generations.

### Trust a pack digest without page-level verification

Rejected because page-range/decode bugs and localized transport corruption need a
bounded page integrity boundary. Both layers are required and neither substitutes for
semantic validation.
