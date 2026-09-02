# Virtual Texturing Architecture

## Purpose And Decision Authority

Virtual texturing is an optional Post-1.0 runtime vertical slice that decouples
logical texture address space from resident GPU backing. It coordinates cooked page
artifacts, bounded demand, logical residency intent, renderer realization and material
sampling without taking ownership from Assets, Materials, World Streaming, Renderer
or source-domain producers.

[ADR-164: Virtual Texturing Ownership, Product Scope and Capability Tier](../../adr/164-virtual-texturing-ownership-product-scope-and-capability-tier.md)
is the normative ownership, composition, product-scope, capability and lifecycle
decision. Exact identities, numeric bounds, artifact formats, queues, feedback policy
and renderer contracts are assigned to the dependent VTX tickets. Illustrations in
this document do not create a second contract.

## Ownership Summary

| Domain | Owns | Does not own |
|---|---|---|
| VTX API/runtime | Logical identities/generations, bounded demand merge, page choice, feature-local eviction and residency snapshots | Files, material definitions, world cells, native GPU resources or a global scheduler |
| Assets/Pipeline | Source identity, deterministic cook, immutable artifact publication and bounded byte leases | Runtime demand, logical residency or GPU mapping |
| Materials | Authored semantic slots, sampling intent and required/fallback variants | Page priority, physical slots or feedback state |
| World Streaming | Cell relevance, aggregate admission and activation/retirement barriers | Per-page policy or a second VTX state machine |
| Renderer | GPU reservations, physical atlas/sparse backing, uploads, mappings, graph passes and safe retirement | Logical page identity, demand policy or artifact discovery |
| Producers | Source-domain meaning and immutable payload contribution | Global admission, package paths or GPU allocation |
| Host/application | Composition, product policy and effective-tier admission | Feature/runtime state after ownership transfer |

The host composes VTX through typed ports. VTX uses Assets for page bytes, the injected
ADR-010 job contract for bounded preparation, ADR-034 reservations for GPU work and
Renderer requests for realization. It never opens a project/build/cache path, queries a
service locator, creates its own global worker pool or exposes a backend-native handle.

## Data And Runtime Flow

Authored content is deterministically cooked into immutable target/capability-keyed
artifacts. Runtime data moves through generation-checked typed requests:

```text
material/producer/world demand snapshots
  -> VTX bounded demand merge and logical page plan
  -> Assets page-byte lease
  -> Renderer admitted upload and mapping
  -> VTX generation-checked residency commit
  -> immutable residency/binding projection
```

Demand is evidence, not authority. Renderer completion becomes visible only when it
matches the active VTX generation and all required leases/readiness records. Workers
may prepare immutable data, but VTX owner safe points commit logical state and Renderer
safe points mutate GPU state.

Replacement admits old/new peak overlap and publishes atomically. Cancellation
invalidates the operation/generation before late completions arrive. Stale work releases
its asset leases and reservations without publication. Physical slots and resources are
reused only after the relevant GPU work and dependent leases retire.

## Source, Artifact And Page-Store Model

[ADR-165: Virtual Texture Source, Cooked Artifact, Page Store and Cache Ownership](../../adr/165-virtual-texture-source-cooked-artifact-page-store-and-cache-ownership.md)
is the normative source/cook/storage decision. Assets owns tracked source identity,
generic cook scheduling/cache/staging, atomic generation publication, packages and
runtime byte leases. VTX import/cook owns page geometry, mip/tail, border, color,
encoding and deterministic aggregation semantics.

One immutable cooked generation contains a root manifest and canonically ordered,
bounded page-pack artifacts. A logical page is the independently requested and
verified content unit; a pack is the range-addressable Assets storage unit. Pages are
not independent authored assets or mutable side files. The manifest is the sole
membership/order authority and records exact pack/page ranges, costs and digests.

Runtime pins an exact published/package generation and requests finite ranges through
an Assets-owned provider port. Local filesystem, archive, memory/test and future remote
providers have the same semantics. VTX never constructs paths, asks for an ambient
"current" generation, enumerates storage or combines packs from different generations.

The cook cache, published generation, provider byte cache, VTX decoded-page cache and
Renderer physical-page cache are distinct owner/lifetime domains. Eviction in one does
not imply publication, invalidation, unmapping or capacity release in another.

## Memory And World-Streaming Boundary

[ADR-034: GPU Memory and Residency Ownership](../../adr/034-gpu-memory-and-residency-ownership.md)
owns GPU allocation, reservations and pressure policy. VTX selects disposable pages
within an admitted allowance; Renderer owns native atlas/sparse backing and safe mapping
execution. Evicting a logical page from an allocated atlas frees a slot, not physical
heap capacity.

For streamed worlds, VTX consumes World Streaming's aggregate reservation through a
host-composed adapter. Neither VTX page eviction nor Renderer pressure independently
evicts a world cell. Activation-critical page failure participates in the cell barrier;
optional content may use only a fallback variant admitted before activation.

## Capability And Product Scope

VTX uses feature-local tiers:

| Tier | Meaning |
|---|---|
| `Unavailable` | VTX assets are rejected; supported ordinary texture/material variants remain available |
| `Atlas` | An explicitly budgeted physical atlas and logical page-table path is implemented and admitted |
| `Sparse` | Qualified native sparse/tiled backing satisfies the same VTX semantics |

Content policy (`Required`, `OptionalWithFallback`, `Disabled`) is separate from the
tier. Effective support is the intersection of compiled implementation, selected
backend/device operations, formats and limits, budgets, shader/material variants,
cooked artifacts and product policy. Backend names, profile ranks and extension bits do
not prove support. Sparse-to-atlas fallback requires independent atlas admission.

Horo 1.0 does not require VTX. VTX runtime delivery remains Post-1.0 and depends on the
RND-010.9 sparse/atlas residency foundation. Headless and dedicated-server hosts do not
initialize GPU residency; validation/cook tools may compose narrow non-rendering
contracts explicitly.

## Unsupported Shortcuts

- direct runtime filesystem/cache-path access or loose-file discovery;
- process-global VTX schedulers, renderer selection or memory ledgers;
- backend/API names and native handles in public/persisted contracts;
- runtime cooking or shader generation after a page miss;
- silent capability, quality or content fallback;
- unbounded work/queues, normal-frame blocking waits or frame-index-only reuse; and
- debug UI state acting as runtime authority.

Failures are typed and observable, including unsupported composition, missing artifacts
or variants, invalid descriptors, capacity, stale generation, cancellation, I/O,
decode, renderer admission, upload/mapping, device loss and stalled retirement.

## Related Documents

- [ADR-164: Virtual Texturing Ownership, Product Scope and Capability Tier](../../adr/164-virtual-texturing-ownership-product-scope-and-capability-tier.md)
- [ADR-034: GPU Memory and Residency Ownership](../../adr/034-gpu-memory-and-residency-ownership.md)
- [Virtual Texturing Debug UI Reference](./virtual-texturing-debug.html)
- [Rendering Architecture](./rendering-architecture.md): virtual texture realization and passes
- [Material And Shader Model](./material-and-shader-model.md): semantic virtual-texture slots and variants
- [Asset Pipeline](./asset-pipeline.md): cooked artifacts and bounded byte access
- [World Streaming Architecture](./world-streaming-architecture.md): aggregate admission and cell barriers
- [Terrain And Foliage Architecture](./terrain-and-foliage-architecture.md): typed producer integration
- [LOD And Culling Architecture](./lod-and-culling-architecture.md): bounded demand inputs
