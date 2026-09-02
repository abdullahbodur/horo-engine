# ADR-167: VTX Feedback, Readback, Prediction and Camera-Data Ownership

- **Status**: Proposed
- **Date**: 2026-09-02
- **Supersedes**: None
- **Scope**: VTX GPU feedback plan, Renderer pass/resource/readback ownership, immutable observations, camera and producer hints, prediction, bounded loss, multiview, lifecycle and privacy
- **Issue**: [VTX-004.1](https://github.com/abdullahbodur/horo-engine/issues/2205)
- **Jira**: [HORO-2159](https://horo-engine.atlassian.net/browse/HORO-2159)
- **Parent**: [VTX-004](https://github.com/abdullahbodur/horo-engine/issues/2204)
- **Related**: [ADR-008](008-error-model-exception-boundary-and-registry.md), [ADR-010](010-job-waiting-and-operation-store-ownership.md), [ADR-026](026-large-world-precision-and-floating-origin-strategy.md), [ADR-027](027-renderer-resource-identity-and-descriptors.md), [ADR-034](034-gpu-memory-and-residency-ownership.md), [ADR-038](038-gpu-scene-and-instance-data-model.md), [ADR-042](042-cpu-gpu-timestamps-and-pipeline-statistics.md), [ADR-119](119-camera-authority-during-cinematics.md), [ADR-164](164-virtual-texturing-ownership-product-scope-and-capability-tier.md), [ADR-166](166-vtx-feature-local-residency-and-eviction-within-global-reservations.md)
- **Normative documents**: [Virtual Texturing Architecture](../architecture/runtime/virtual-texturing-architecture.md), [Rendering Architecture](../architecture/runtime/rendering-architecture.md), [Cinematic Sequencer Architecture](../architecture/runtime/cinematic-sequencer-architecture.md)

## Context

ADR-166 makes VTX responsible for feature-local page priority and residency inside a
global reservation. It does not define where page-demand evidence comes from. The old
VTX sketch implied a compute pass writing an append buffer followed by CPU readback,
but combined native GPU resources, CPU request vectors and camera visibility into a
single `VirtualTextureFeedback` object.

That model could make VTX own native buffers/fences, poll a mapped resource, wait for
same-frame feedback or read mutable camera state. It could also let a feedback sample
or producer hint bypass World Streaming admission and directly evict/load pages. Camera
cuts, origin rebases, dynamic resolution, multiview and delayed readback make such
assumptions incorrect even when the raw page IDs are valid.

Feedback is optional delayed evidence, not state authority. Renderer must own graph
placement, encoding resources, copies and GPU-safe lifetime. Camera/view coordinators
must own canonical per-view snapshots. VTX may aggregate immutable observations and
predict page demand, but ADR-166 remains the only page scheduling/eviction authority
and World Streaming retains global admission.

This ADR fixes those boundaries. VTX-004.2 and later tickets freeze exact encoding,
compaction, readback, prediction, limits and test vectors without changing ownership.

## Decision

### 1. Feedback responsibilities remain split by authority

| Responsibility | Authority | Deliberate non-owner |
|---|---|---|
| Logical feedback requirement, sampling policy and bounded semantic descriptor | VTX | Renderer does not invent page-demand semantics |
| Render-graph pass placement, GPU feedback/compaction/copy resources, native encoding and synchronization | Renderer frontend/backend | VTX never owns/maps native buffers or fences |
| Asynchronous readback staging, pending-result capacity and GPU/CPU retirement | Renderer under ADR-027/034 | VTX cannot poll, block or refund Renderer resources |
| Immutable validated feedback observation and gap/overflow evidence | Renderer publishes; VTX consumes | Observation does not mutate residency by itself |
| Canonical camera/view selection, transforms, projection, viewport and discontinuity | Camera/view authority under ADR-119/038 | VTX cannot select, move or query a global camera |
| Producer-domain visibility/coverage hints | Owning producer adapter | VTX cannot reinterpret producer state or lifetime |
| Observation history, prediction and demand-candidate generation | VTX | Prediction does not grant admission or eviction authority |
| Global admission/criticality/reservations | World Streaming/application under ADR-166 | Renderer feedback cannot enqueue global work directly |

The host composes these ports before VTX activation. Descriptor validation is inert and
does not create graph passes, allocate readback pools, subscribe globally or capture a
camera pointer.

### 2. VTX supplies a bounded semantic feedback plan

An admitted generation declares a plan equivalent in semantics to:

```cpp
struct VtxFeedbackPlan {
    VtxFeedbackSchemaId schema;
    VirtualTextureGeneration generation;
    VtxFeedbackMode mode;
    CountLimit maximumSamples;
    CountLimit maximumUniquePages;
    ByteCount maximumGpuBytes;
    ByteCount maximumReadbackBytes;
    CountLimit maximumPendingResults;
    FrameInterval cadence;
    VtxFeedbackOverflowPolicy overflow;
};
```

Exact layout and limits belong to downstream tickets. The plan contains Horo identities,
finite costs and semantic encoding requirements, never shader/native resource handles,
API enums or an arbitrary callback. Its costs participate in ADR-166 global admission
and ADR-034 Renderer resource admission before graph/resource creation.

Supported modes are `None`, bounded renderer feedback and producer/camera-hint-only.
They are capabilities orthogonal to ADR-164's `Atlas`/`Sparse` realization tier. A VTX
path may work without GPU feedback when its declared producer/hint policy is complete;
missing feedback cannot silently enable a different algorithm.

### 3. Renderer owns pass execution and asynchronous readback lifetime

Renderer validates the plan against the selected backend/device, graph, formats,
atomics/compaction operations, queue synchronization, copy/readback support and reserved
capacity. It creates all feedback targets, counters, compacted lists, readback staging
and fences as generation-checked Renderer resources.

The feedback pass reads only declared render bindings and writes a bounded encoding.
Overflow saturates a count/flag and never writes out of bounds. Compaction/copy is an
explicit graph operation with resource usage and barriers. Backends translate the same
Horo schema privately; native layouts never become the public observation contract.

Normal frames never wait for feedback, map an incomplete buffer or call GPU idle.
Renderer publishes only after copy completion and CPU ownership of the bounded result
is established. Readback arenas remain charged and leased until VTX consumption or
expiry plus all GPU readers retire. Cancellation suppresses publication but does not
return credits early.

### 4. Published observations are immutable, delayed and generation checked

Renderer publishes a value/lease equivalent in semantics to:

```cpp
struct VtxFeedbackObservation {
    VtxFeedbackSchemaId schema;
    VirtualTextureGeneration textureGeneration;
    RendererDeviceGeneration deviceGeneration;
    RenderFrameId sourceFrame;
    ViewSnapshotId view;
    ViewSnapshotGeneration viewGeneration;
    BoundedArray<VtxPageObservation> pages;
    VtxFeedbackCompleteness completeness;
    FrameCount observedLatency;
};
```

Entries contain stable virtual page identity and bounded evidence such as quantized
coverage/frequency, not physical atlas slots, native addresses or mutable residency.
Canonical ordering and duplicate aggregation are defined by schema. Completion repeats
the exact operation/resource generations and declared bounds.

VTX rejects or retires wrong-schema, stale texture/device/view generations, malformed,
oversized or impossible entries. Reordered delivery is processed by source identity and
captured policy, not callback order. Duplicate completion is idempotently ignored.
Observation age is explicit; VTX never presents delayed data as current-frame truth.

### 5. Camera data arrives as an immutable per-view snapshot

The Camera/view authority resolves one canonical immutable snapshot for each rendered
view/frame before extraction. The VTX adapter may receive only the fields admitted by
its versioned hint schema, such as stable view/snapshot identity, world-space origin,
view/projection parameters, viewport/scale, jitter state and typed discontinuity flags.

VTX does not hold a `Camera*`, query Scene/ECS, select an active camera or infer authority
from editor/gameplay/cinematic names. Camera cuts, teleports, origin rebases, projection
changes and large dynamic-resolution changes carry explicit discontinuity evidence.
Prediction resets or downweights incompatible history; it does not reinterpret the
change as extreme velocity.

Multiple eyes, editor viewports, reflection/capture views and runtime cameras remain
separate typed view scopes. The host declares which views contribute and their bounded
weight; VTX cannot merge them by mutable camera identity or multiply global admission.

### 6. Producer hints are typed evidence with provenance

Terrain, World Streaming and other producers may emit immutable bounded hints containing
their owner/generation, target VTX generation, page/region evidence, validity window,
confidence/priority class and cost. They never expose producer pointers, mutable
containers or backend resources.

VTX validates provenance and generation, normalizes hints under captured policy and
merges them with feedback/camera evidence deterministically. A producer cannot pin a
page, allocate budget, override content criticality or evict another consumer. Required
cell pins and global priorities still originate from ADR-166 owner contracts.

### 7. Prediction produces demand candidates, not commands

VTX owns a finite generation-scoped observation history and deterministic prediction
policy. Inputs are admitted immutable observations, camera snapshots, producer hints,
current logical residency snapshot and versioned settings. Output is a bounded,
canonically ordered batch of demand candidates with reason/provenance and expiry.

Prediction may estimate near-future pages from velocity, mip/coverage trends or producer
lookahead after downstream algorithms are specified. It cannot mutate Camera, Scene,
World Streaming or Renderer; invent required content; turn confidence into a pin; or
submit I/O/GPU work outside ADR-166. Demand candidates enter the same coalescing,
priority, reservation and fallback path as other evidence.

No wall-clock timing, pointer order, unordered iteration or worker completion order
affects deterministic output. Prediction history has fixed count/byte/age limits. An
overflow or missing input drops/downweights optional future evidence according to the
captured policy and reports a gap; it never expands storage.

### 8. Loss, overflow and capability failure are explicit

Feedback is inherently delayed and may be incomplete. `completeness` distinguishes at
least complete, sampled, overflowed, skipped-by-cadence, budget-dropped, cancelled and
device/view discontinuity. Missing observations do not mean no demand.

For optional feedback, capacity pressure may omit the newest observation and publish a
bounded gap diagnostic. VTX continues from declared producer/camera hints and retained
residency policy without pretending the sample was complete. If a product declares
feedback required, unavailable capability or sustained gaps fail/suspend that VTX
composition or select a separately admitted hint-only/non-VTX fallback. They cannot
silently downgrade required behavior.

Overflow cannot synchronously read a larger buffer, allocate mid-frame, retry without a
reservation or turn every possible page into an unbounded demand set. Conservative
response is bounded by the plan and remains subject to ADR-166 admission.

### 9. Lifecycle and replacement retain every owner generation

Activation admits the feedback plan, creates Renderer resources, registers immutable
camera/producer snapshot routes and reserves observation/history/completion capacity
before publication. VTX readiness requires only the declared mode; `None` allocates no
feedback/readback resources.

On VTX replacement, new feedback resources and history use the new generation. Old
observations cannot seed it unless the replacement policy proves schema/content/view
compatibility; default behavior resets history. Old graph/readback resources and
observations remain leased until Renderer and VTX consumers retire.

Shutdown closes new plans/snapshots, invalidates VTX publication, drains bounded
completions and asks Renderer to retire exact resources. Camera/producer lifetimes are
not extended beyond immutable snapshot leases. A deadline reports stalled retirement;
it cannot clear native pools or publish stale demand.

### 10. Privacy, diagnostics and qualification are bounded

Raw camera trajectories, feedback page streams and content/page identities are not
durable telemetry by default. Ordinary diagnostics expose aggregate counts, latency,
completeness/gap reasons, safe generations and bounded prediction outcomes. Capture or
export requires explicit developer/qualification policy, redaction and retention under
the owning diagnostics system.

Typed failures distinguish plan invalid/unsupported, graph/resource/readback admission,
encoding overflow, malformed observation, stale texture/device/view, camera/producer
snapshot unavailable, history capacity, cancellation and retirement stall.

Qualification covers no-feedback composition; exact-cap/overflow encodings; delayed,
reordered, duplicate and stale completion; camera cuts/rebases/dynamic resolution;
multiview bounds; producer provenance; full pending-result/history queues; cancellation
before/after submission; replacement/device loss; zero same-frame waits; cleanup under
full queues; and deterministic prediction across scheduling orders.

### 11. Migration removes mixed CPU/GPU feedback objects

Prototype `VirtualTextureFeedback` objects combining a `BufferHandle` and mutable CPU
page vector are not compatibility contracts. They split into VTX semantic plans,
Renderer-private resources and immutable observation results. Live camera pointers and
ad hoc producer callbacks are replaced by versioned snapshots/hints. Any frame loop
that maps feedback or waits synchronously moves to delayed owner completions.

## Consequences

Positive consequences:

- Renderer owns every GPU/readback resource while VTX retains demand semantics.
- Camera cuts, multiview and origin rebases are explicit inputs rather than hidden
  prediction errors.
- Feedback loss and overflow remain bounded and observable.
- Prediction cannot bypass global reservation or residency policy.

Costs and trade-offs:

- Feedback reacts after asynchronous latency rather than the same frame.
- Multiple generations/identities must be correlated through each observation.
- Hint-only fallback needs explicit cooked/product policy and qualification.
- Exact encoding and prediction algorithms remain downstream decisions.

## Rejected Alternatives

### Let VTX allocate/map feedback buffers directly

Rejected because graph placement, native synchronization, readback capacity and GPU-safe
retirement belong to Renderer and must remain backend neutral to VTX.

### Read feedback synchronously in the frame that produced it

Rejected because it creates a CPU/GPU stall and makes streaming behavior backend timing
dependent. Delayed observations carry source frame and age explicitly.

### Give VTX a live Camera or Scene pointer

Rejected because it bypasses per-view camera authority, creates lifetime/thread races
and cannot represent cuts, editor/cinematic ownership or multiview consistently.

### Let feedback or prediction directly load/evict pages

Rejected because observations are evidence. ADR-166 owns coalescing, pins, reservations,
fallback and eviction; World Streaming retains global admission.

### Treat overflow as demand for every page

Rejected because it is unbounded and converts missing evidence into budget authority.
Overflow follows a finite declared policy and remains observable.
