# ADR-124: VFX GPU Simulation, Readback and Compute Fallback

- **Status**: Proposed
- **Date**: 2026-09-02
- **Supersedes**: None
- **Scope**: GPU particle authority, opt-in asynchronous readback, compute/readback capability admission, compute-less fallback, mixed CPU/GPU effects, shared budget accounting and failure semantics
- **Issue**: [VFX-003.1](https://github.com/abdullahbodur/horo-engine/issues/1751)
- **Jira**: [HORO-1708](https://horo-engine.atlassian.net/browse/HORO-1708)
- **Related**: [ADR-008](008-error-model-exception-boundary-and-registry.md), [ADR-011](011-vfx-effect-ownership-simulation-domain-and-renderer-boundary.md), [ADR-012](012-world-streaming-partition-authority-and-subsystem-boundaries.md), [ADR-028](028-renderer-capability-limits-and-product-profiles.md), [ADR-034](034-gpu-memory-and-residency-ownership.md), [ADR-123](123-vfx-cpu-stage-order-determinism-and-gameplay-coupling.md)
- **Normative documents**: [VFX and Particles Architecture](../architecture/runtime/vfx-and-particles-architecture.md), [Rendering Architecture](../architecture/runtime/rendering-architecture.md), [Render Backend Parity Contract](../architecture/runtime/render-backend-parity-contract.md)

## Context

ADR-011 assigns logical GPU-emitter state to scene-owned `VfxWorld`, native GPU
resources and scheduling to Renderer, and one resolved simulation domain to each
compiled emitter unit. It forbids authoritative GPU-to-CPU gameplay feedback and
silent migration of an unaffordable GPU effect to CPU. ADR-123 defines the real CPU
path used when particle behavior is gameplay-authoritative.

The remaining gap is narrower but operationally important. An effect asset may need
bounded GPU results for authoring preview, diagnostics, capture or later cosmetic
work, yet the architecture does not distinguish such asynchronous observation from
gameplay output. It also does not say exactly how missing compute or readback support
affects every simulation preference, how an authored CPU substitute may differ
visually, or how simultaneous CPU/GPU units and readback copies share budgets.

Backend-specific staging-buffer, queue or map details cannot leak into VFX contracts.
The decision must therefore make readback explicit and typed, preserve Renderer
ownership of native work, and turn every fallback/degradation into a deterministic
admission result rather than a hidden runtime branch.

## Decision

### 1. GPU particle state is never authoritative gameplay state

A GPU-resolved emitter is visual-only. Its particle state, collisions, deaths,
compaction, counters and readback observations cannot directly author gameplay,
Physics, Networking, save/replay, authoritative Audio events or another fixed-step
simulation result. Declaring readback does not relax this rule.

Gameplay-relevant behavior uses an ADR-123 CPU-mandatory unit or a separately authored
deterministic CPU event source. A composite effect may pair that CPU unit with GPU
visual children through bounded next-tick requests. The GPU child may disappear,
reduce quality or arrive late under its declared visual policy without changing the
CPU unit's accepted requests, fixed-step outputs or gameplay result.

GPU-to-CPU observations may be consumed only by declared authoring, diagnostics,
capture or cosmetic presentation adapters. A cosmetic consumer may affect a later
presentation frame after generation validation; it cannot reenter the producing
step, update an authoritative component or be recorded as deterministic gameplay.

### 2. Readback is off by default and declared in the cooked asset

Every compiled GPU unit has either no readback descriptor or one bounded typed
`GpuVfxReadbackDescriptor`. There is no global "read all particles" toggle and no
consumer may request an undeclared field at runtime.

```cpp
enum class GpuVfxReadbackPurpose : uint8_t {
    AuthoringPreview,
    DiagnosticsCapture,
    CosmeticObservation
};

enum class GpuVfxReadbackRequirement : uint8_t {
    Optional,
    RequiredForVisualEffect
};

struct GpuVfxReadbackDescriptor {
    VfxReadbackSchemaId schema;
    GpuVfxReadbackPurpose purpose;
    GpuVfxReadbackRequirement requirement;
    uint32_t cadenceSteps;
    uint32_t maximumRecords;
    uint32_t maximumBytesPerResult;
    uint32_t maximumPendingResults;
};
```

The cooked schema lists only normalized Horo scalar/vector/ID fields, their encoding,
stride, order, numeric qualification and aggregate/sample selection. It cannot expose
native buffers, pointers, descriptors, mapped memory, shader-private padding or
unbounded variable data. `maximumRecords * stride` and every cadence/pending product
are checked during cook and admission.

Sample selection is deterministic from stable emitter/particle identity and step,
not native storage order or completion timing. Aggregate reduction order and numeric
policy are versioned. A schema or reduction change changes the cooked artifact
version and cannot be silently interpreted by an older consumer.

`Optional` means the visual effect may run without observations and reports a typed
degraded result. `RequiredForVisualEffect` means the effect's declared visual/capture
contract is unavailable without readback; it still does not make the data gameplay-
authoritative. Required readback must resolve before activation.

### 3. Effective capabilities and budgets decide readback admission

The host gives VFX an immutable backend-neutral `VfxCapabilities` snapshot derived
from the selected Renderer plan. Readback admission uses typed facts such as:

- GPU compute and required storage/indirect operations;
- asynchronous device-to-host copy support for the declared resource class;
- supported normalized formats, alignments and maximum copy/result sizes;
- maximum admitted copies, bytes and pending generations per frame; and
- whether the requested observation is qualified for the active product profile.

Backend or API names are never capability values. OpenGL, Metal, Vulkan and future
backends translate the same logical copy/format/schema request privately. Renderer
may report a narrower effective capability than hardware marketing claims because of
driver policy, resource pressure or an unqualified path.

Admission reserves particle buffers, destination staging capacity, copy work,
pending result records and frames-in-flight overlap. A capability or reservation must
exist before the unit becomes active. Ordinary frames never allocate a surprise
readback buffer, block for capacity or wait for a fence.

### 4. Renderer schedules asynchronous readback and owns native lifetime

`VfxWorld` and `GpuParticleSimulator` produce a bounded backend-neutral readback
intent alongside `GpuVfxSimulationWork`. `VfxRenderExtractor` copies that intent into
the immutable `RenderWorldSnapshot`; it does not encode, map or inspect GPU storage.

After the producing VFX Compute pass, `RenderFrontend` validates the descriptor and
adds a graph copy/readback operation with explicit producer, resource usage and queue
barriers. The selected backend owns native staging resources, copy encoding, fence
tracking, mapping/cache maintenance and deferred destruction. Dedicated queues are a
private implementation choice that cannot alter logical order or result identity.

Completion returns a bounded `GpuVfxReadbackResult` containing scene/effect/emitter,
step, resource generation, schema version, record count, numeric fingerprint and
typed status. Results are delivered asynchronously at an owner safe point and may be
one or more frames late. No simulation, extraction, render or UI path performs a
same-frame wait, device-idle call or synchronous map.

Logical cancellation closes publication but retains staging/resources and budget
charges until GPU completion. A stale scene, emitter, schema, policy or resource
generation is discarded and acknowledged; it is never retargeted to a replacement
instance. An uncertain submission follows renderer recovery and is not blindly
replayed, because duplicate readback or simulation work would be ambiguous.

### 5. Readback has bounded backpressure and no hidden retry loop

The descriptor cadence is evaluated once per committed logical GPU step. Reused
snapshots and multiple views do not schedule duplicate copies. When the admitted
pending-result or per-frame byte/work budget is exhausted:

- optional readback omits the newest observation, increments a rate-limited typed
  counter and leaves simulation/rendering unchanged;
- required visual readback fails or suspends the declared effect/capture operation
  according to its authored policy; and
- neither path grows a queue, waits, retries recursively, steals another owner's
  reservation or changes simulation domain mid-step.

Results retain source-step identity, so a consumer can detect gaps and age. Missing,
late or dropped optional observations are `Unavailable`/`Degraded`, never zero-filled
records or evidence that no particles existed.

### 6. Compute-less fallback is resolved before activation

Domain resolution retains ADR-011's order and specializes GPU/readback outcomes:

| Unit intent or requirement | Compute/readback unavailable outcome |
|---|---|
| CPU-mandatory gameplay or CPU geometry query | Run the compatible admitted CPU kernel; `RequireGPU` is an incompatible-asset error |
| `RequireCPU` | Run the admitted CPU kernel or fail typed admission |
| `RequireGPU` | Use only a separately authored compatible fallback effect satisfying the requirement; otherwise `VfxComputeUnavailable`/`VfxReadbackUnavailable` |
| `PreferGPU` or GPU-selected `Automatic` | Try an authored compatible CPU kernel within its fallback envelope and CPU budget, then an authored substitute, then permitted visual suppression; otherwise typed unavailability |
| GPU-only node/feature | Use its authored substitute or permitted suppression; never silently delete a required node or pretend the original CPU kernel exists |
| Optional readback only | Run the GPU effect without readback and publish a degraded admission/result diagnostic |
| Required visual readback | Resolve an authored compatible fallback or report the effect/capture unavailable; never reinterpret it as optional |

A cooked CPU kernel is necessary but not sufficient for fallback. The asset also
declares a versioned `VfxVisualFallbackEnvelope`: compatible parameters/outputs,
maximum CPU particle/work cost, allowed reduced particle count and explicitly
substitutable visual features. Resolver output records the selected domain/effect,
failed predicates, policy/capability revisions, reserved costs and fallback rule.

Allowed differences are visual only and authored: reduced density, coarser visual
noise, a substituted collision approximation, omitted optional sort/volume/field
detail or complete visual suppression where policy permits. Gameplay-visible
differences are never acceptable. Timing/order/results of a paired CPU gameplay unit
remain unchanged, and required Audio/Networking/save output cannot originate from
the substituted visual path.

Missing compute never selects another graphics backend or switches an interactive
host to RenderNull. Changing effective capabilities or policy triggers safe-point
re-admission and effect restart/hold/failure according to authored policy; live GPU
particle state is not migrated to CPU.

### 7. CPU and GPU units coexist through typed asynchronous seams

A scene may run CPU and GPU emitter units concurrently, including separate units in
one compiled effect. Each unit retains one resolved domain and independent particle
state. Cross-unit edges are limited to cooked bounded messages:

- CPU may enqueue a GPU visual child for the next eligible presentation step;
- GPU may publish declared delayed cosmetic observations after readback; and
- synchronous particle-state dependencies, authoritative GPU-to-CPU edges and one
  unit split across CPU/GPU are rejected at cook.

Stable effect/emitter/request identities correlate units without sharing mutable
buffers. CPU extraction produces frame-owned upload slices; GPU simulation produces
logical resource views. Renderer consumes both through the same immutable snapshot
and standard material/pass contracts, without VFX inspecting native resource types.

### 8. One admission plan charges CPU, GPU and shared costs

`VfxWorld` builds one bounded `VfxAdmissionPlan` per effect from cooked peak costs and
the captured policy/capability revisions. The host/VFX ledger owns logical per-domain
allowances and gameplay reserves; Renderer/ADR-034 owns GPU backing-capacity and work
reservations inside the same host envelope. A renderer reservation referenced by the
VFX plan is the same charge, not a second independent budget.

| Cost family | Charged resources |
|---|---|
| CPU simulation | Particle SoA/candidate state, scratch, job work, occurrences and packed frame copies |
| GPU simulation/render | Current/next particle state, compute/sort/cull/draw work, resource bindings and frames-in-flight overlap |
| GPU readback | Copy work/bytes, staging capacity, pending results, retained source generations and consumer records |
| Shared VFX | Instances, accepted events, compiled asset leases, extraction descriptors, decals, volumes, uploads and retirement records |

Admission is deterministic in accepted owner order with explicit required/gameplay
reserves and cosmetic limits. CPU and GPU caps are independent dimensions under the
shared envelope: spare GPU budget is not permission to exceed CPU/gameplay capacity,
and GPU/readback pressure cannot consume reserved gameplay CPU work or output slots.
Streaming effects use [ADR-012's](012-world-streaming-partition-authority-and-subsystem-boundaries.md)
existing aggregate reservation projection rather than a VFX-local global scheduler.

Fallback is a new plan, not an arithmetic rename of GPU cost to CPU cost. It must
reserve the complete selected CPU/substitute peak before activation. Domain restart,
quality replacement and resize charge old/new overlap until readers and fences retire;
logical cancellation does not return GPU or readback credits early.

### 9. Failures preserve state and identify the failed boundary

Cook failures include `VfxReadbackSchemaInvalid`, `VfxFallbackIncompatible`,
`VfxAuthoritativeGpuDependency` and `VfxCostBoundInvalid`. Admission/runtime results
include `VfxComputeUnavailable`, `VfxReadbackUnavailable`,
`VfxReadbackBudgetExceeded`, `ResourceBudgetExceeded`, `StaleVfxGeneration`,
`VfxSubmissionUncertain` and `VfxDeviceLost`.

Every result retains effect/emitter/request/step identity, requested and resolved
domain, capability/policy/schema revisions, failed predicate and underlying renderer
cause. Optional visual degradation is explicit status plus diagnostics, not success
with silently altered behavior. Required failure creates no partial reservation,
readback consumer or active unit. Runtime failure retains the last valid generation
or follows the authored restart/suspend/stop policy; it never publishes fabricated
records or partial native state.

### 10. Qualification covers capabilities, latency, fallback and mixed load

Required implementation evidence includes:

- no-readback assets scheduling zero copies/maps and rejecting ad hoc field requests;
- each purpose/schema/cadence/record/byte boundary at exact limit and one beyond;
- deterministic sampled identity and aggregate reduction fixtures across supported
  backends, while respecting the declared numeric fingerprint/tolerances;
- delayed, reordered native completion normalized to source-step order, optional
  gaps, cancellation, stale generations, device loss and uncertain submission;
- proof that no result reaches gameplay, Physics, Networking, save/replay or
  authoritative Audio, including malicious/stale consumer attempts;
- every matrix row for compute/readback capable, unsupported, unqualified and
  budget-exhausted profiles with recorded resolver evidence and no backend switch;
- CPU fallback envelope validation, cap boundaries and rejection of GPU-only required
  features without an authored substitute;
- mixed CPU/GPU effects, next-tick CPU-to-GPU children and rejection of synchronous
  GPU-to-CPU/cross-domain state edges;
- aggregate CPU/GPU/readback/shared budget saturation, protected gameplay reserves,
  replacement overlap and credits returned only after final retirement; and
- shared logical fixtures under Null/fakes plus native GPU smoke tests for each shipped
  backend; Null cannot qualify native copy, map, fence or driver behavior.

## Consequences

### Positive

- Readback is a reviewable asset capability rather than an ambient GPU memory escape.
- Compute-less behavior is deterministic, bounded and visible before activation.
- Gameplay authority is identical whether GPU visuals run, fall back or are suppressed.
- CPU/GPU coexistence and readback consume one auditable admission plan.
- Backend implementations remain private translations of Horo-owned descriptors.

### Costs

- Cooked assets need readback schemas, visual fallback envelopes and peak cost data.
- Asynchronous staging and retained source generations increase admitted peak memory.
- Some GPU-only or required-readback effects are explicitly unavailable on low tiers.
- Native readback qualification is required separately for every shipped backend.

## Rejected Alternatives

### Allow readback to drive gameplay one or more frames later

Rejected because device/queue timing, loss, budget pressure and backend behavior would
become gameplay authority. Gameplay-derived particle behavior remains on the CPU path.

### Permit runtime tools to request arbitrary particle fields

Rejected because it bypasses cooking, privacy/layout boundaries and bounded admission.
Only cooked schemas with finite records, bytes and cadence may be observed.

### Automatically run every missing-compute GPU effect on CPU

Rejected because a compatible kernel, visual substitution and affordable CPU budget
are not guaranteed. Fallback requires explicit cooked compatibility and admission.

### Drop unsupported GPU nodes until the effect runs

Rejected because silent deletion makes content and performance profile-dependent in
unreviewable ways. Only authored substitutions/reductions or suppression are valid.

### Make VFX own native staging buffers or map GPU memory

Rejected because Renderer owns native resources, graph synchronization and fence-based
retirement. VFX supplies logical intent and receives typed delayed observations.

### Maintain separate CPU, GPU and readback global schedulers

Rejected because independent ledgers would double-count or oversubscribe the shared
host envelope and could steal gameplay reserves. One admission plan projects charges
to existing VFX/host and renderer owners.
