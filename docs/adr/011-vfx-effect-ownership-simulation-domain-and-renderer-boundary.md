# ADR-011: Effect Ownership, Simulation Domain Policy and Renderer Boundary

- **Status**: Proposed
- **Date**: 2026-08-28
- **Supersedes**: None
- **Scope**: Runtime VFX ownership, per-emitter domain policy, bounded admission, immutable extraction, GPU scheduling, retirement and per-view sorting
- **Issue**: [VFX-001.1](https://github.com/abdullahbodur/horo-engine/issues/1749)
- **Jira**: [HORO-1706](https://horo-engine.atlassian.net/browse/HORO-1706)
- **Normative document**: [VFX And Particles Architecture](../architecture/runtime/vfx-and-particles-architecture.md)

## Context

Particles, ribbons, decals and volumetric effects cross simulation and rendering
boundaries. The previous proposal assigned compute encoding and Render Graph
submission to scene-owned components while also requiring backend neutrality and
immutable snapshots. It promised synchronous GPU reclamation and zero allocations
without defining overflow. Its Null and GPU-to-CPU fallback rules could change
headless gameplay or move an unaffordable visual effect onto the CPU.

The decision must separate logical scene state from renderer-owned native work,
resolve domain intent per compiled emitter, and specify capacity and timing outcomes.
It must reconcile ADR-010/018 owner dispatch and ADR-012 cell retirement, rather than
add a separate asynchronous execution or streaming model. This record remains
Proposed; merging this documentation is not a claim of acceptance or implementation.

## Decision

**SceneRuntime owns VfxWorld and logical effect state. SimulationDomainResolver
selects one domain per compiled emitter using gameplay constraints, asset intent,
actual capabilities and admitted budgets. VfxRenderExtractor writes immutable,
backend-neutral work/render descriptors into RenderWorldSnapshot. RenderFrontend
owns GPU compute/sort/cull/draw scheduling and physical resource retirement. VFX
never submits immediate-mode calls or Render Graph passes.**

### Ownership And Execution

| Component | Owner | Contract |
|---|---|---|
| VfxWorld | SceneRuntime | Logical scene-scoped effects, CPU state, bounded queues and extraction |
| EffectSystem / SimulationDomainResolver | VfxWorld | Lifecycle, compiled graph bindings and per-emitter resolution |
| CpuParticleSimulator | VfxWorld | Real CPU kernels, including gameplay-coupled headless execution |
| GpuParticleSimulator | VfxWorld | Produces GpuVfxSimulationWork with logical step/resource IDs, never native dispatch |
| NullParticleSimulator | VfxWorld | Permitted visual suppression, preserving queue/tick/lifecycle contracts |
| DecalManager | VfxWorld | Logical projection/fade/atlas requests; no physical texture ownership |
| EffectInstancePool / VfxEventQueue | VfxWorld | Preallocated storage, explicit admission and overflow outcomes |
| VfxRenderExtractor | VfxWorld | Immutable frame packing; no graph submission or simulation mutation |
| RenderFrontend / renderer resource manager | Renderer | Upload, graph construction, native resource leases and deferred retirement |

Host composition provides inert descriptors, capability snapshots and adapters.
It never makes a concrete graphics backend discoverable from gameplay/VFX code.
Scene-owned logical GPU state does not imply ownership of native buffers or commands.

### Simulation, Snapshot And Graph Phases

```text
Scene simulation owner
    CPU SoA update + backend-neutral GpuVfxSimulationWork
        -> VfxRenderExtractor
        -> immutable RenderWorldSnapshot
        -> RenderFrontend
        -> VFX Compute (once per step)
        -> per-view Sort/Cull
        -> standard Shadow/Depth/Opaque/Decal/Volume/Translucent passes
        -> fence-based renderer retirement
```

The existing Scene scheduler declares access/dependency edges. Gameplay-coupled CPU
emitters use the owning fixed simulation tick after required physics results; their
gameplay outputs reach a declared later owner safe point. Cosmetic work uses
Presentation before RenderExtraction. A unit is not advanced on both schedules.
Scene clock/seed policy controls pause and replay, not wall time or view count.

VfxWorld::Update drains a bounded accepted-event prefix and advances CPU/logical
state. GpuParticleSimulator only writes typed work descriptors. Extraction publishes
stable data and leases; it does not encode compute, write mapped GPU buffers or
submit passes. At render frame synchronization, RenderFrontend validates the
snapshot, uploads CPU slices and constructs the graph. The selected backend owns
native encoding and resource/queue barriers on its permitted execution roles.

[ADR-010](010-job-waiting-and-operation-store-ownership.md) JobSystem/JobId and
non-blocking completion ownership apply to CPU preparation. Workers cannot mutate
live scene state or wait on nested work. Required CPU progress uses the host's
non-blocking scheduling policy rather than stale gameplay data. Long user-visible
operations use the application OperationStore, not a VFX-owned replacement service.
[ADR-018](018-command-registration-permissions-threading-and-packaged-build-policy.md)
WorkerJob does not authorize native GPU encoding; RenderSafePoint commands enter
the same render frame synchronization seam. No ordinary owner/render frame waits
on workers or GPU fences.

GPU work is keyed by scene incarnation, effect/emitter generation and simulation
step. Multiple views or snapshot reuse must not submit that step twice. Output versions
remain leased, so later steps cannot overwrite data needed by old snapshots. Bounded
pending work retains leases until renderer acknowledgement; submission is not
retirement. Backpressure stops new cosmetic GPU work and reports it, with an authored
bounded restart/reset policy, not unlimited catch-up or automatic CPU migration.
Uncertain submission uses renderer recovery rather than blindly replaying the step.

GPU depth collision uses one declared cosmetic collision view's previous completed,
generation-valid depth snapshot. Missing depth follows authored fallback/disable
policy. It cannot create a cycle through the current VFX-dependent depth pass or
advance a simulation once per view. GPU readback cannot drive authoritative gameplay.

### Per-Emitter Domain Policy

A compiled emitter simulation unit owns one particle state and one resolved domain.
Any gameplay interaction or required CPU geometry query makes that entire unit
CPU-mandatory, regardless of particle count. A composite graph may contain separate
CPU and GPU units; an individual unit is never partly CPU and partly GPU. Synchronous
state/gameplay dependencies propagate CPU requirements through connected units or
fail cook if incompatible. CPU-to-GPU visual child events use bounded next-tick
requests; synchronous authoritative GPU-to-CPU feedback is prohibited.

The normative contract separates SimulationPreference (Automatic, RequireCPU,
PreferCPU, PreferGPU, RequireGPU) from ResolvedSimulationDomain (CPU, GPU, Null).
Resolve in this order:

1. Validate CPU-mandatory behavior. RequireGPU conflicts are typed asset errors;
   otherwise use real CPU simulation, with a required compatible CPU kernel.
2. Apply explicit intent and host mode. RequireCPU cannot become GPU. RequireGPU
   needs the capabilities or a separately authored fallback effect; otherwise it
   is unavailable. Headless visual suppression through Null is permitted only by
   authored policy and cannot replace gameplay behavior.
3. Preferences try the preferred compatible/admitted path, then an explicitly
   permitted fallback. CPU fallback needs a cooked CPU kernel and CPU memory/work
   admission. Unsupported required noise/collision behavior is not silently erased.
4. Automatic uses VfxQualityPolicy.autoGpuParticleThreshold: eligible visual emitters
   above desktop default 2048 prefer GPU; at/below it prefer CPU. Authored kernel/cost
   needs can prefer GPU below it only after mandatory checks. The same admission and
   fallback rules apply.

2048 is a configurable heuristic, not a cap. Per-emitter and aggregate budgets always
apply, including high-end profiles. The default low-budget cosmetic CPU fallback cap
is 512. Only authored valid cosmetic reductions may clamp counts; required gameplay
capacity failure returns a typed rejection to its owner. No 500k GPU effect becomes
an unbounded CPU workload by fallback.

Cooked descriptors record supported kernels, required capabilities, dependencies,
fallbacks and peak costs. Load resolves against a captured host policy/capability
revision. Changing domain requires safe re-admission/restart, not live state migration.
The prior proposed SimulationDomain field migrates with descriptor versioning:
Automatic -> Automatic, CPU -> RequireCPU, GPU -> PreferGPU, with diagnostics. Do not
retain competing selectors or silently reinterpret stored asset intent.

### Capabilities, Profiles And Null Semantics

VfxCapabilities reports actual compute, indirect draw, GPU sorting, volume textures,
vector fields and device limits. VfxQualityPolicy provides validated finite budgets,
heuristics and authored degradation rules. Backend/API names do not imply support.

| Profile | Execution and fallback |
|---|---|
| Headless/test | Real CPU gameplay; permitted visual Null; same queue/tick/lifecycle timing, zero GPU dependency |
| Low visual budget | Admitted CPU or explicitly supported GPU; default cosmetic CPU fallback cap 512; authored substitute or rejection |
| Desktop baseline | CPU/GPU by intent/capability/cost; finite work, memory and spawn limits |
| High visual budget | Larger finite budgets; optional advanced paths still require actual capabilities |

Legacy es3/dx11/opengl4/dx12_vulkan/metal labels are not VFX capability enums.
The renderer parity and availability contracts remain authoritative: VFX fallback
cannot silently select another renderer or switch an interactive host to RenderNull.
Reproducible resolution does not claim bitwise visual CPU/GPU parity across devices.

Null keeps accepted FIFO order, drain cutoff/limit, next-tick result delivery,
logical lifetime, pause and cancellation timing. It cannot complete submission inline.
It suppresses visual kernels; it does not fabricate per-particle collision/death
results. Gameplay, audio or other required nonvisual events use real CPU simulation
or an authored deterministic CPU event source. Headless gameplay uses the same CPU
kernels, seed/tick inputs and admission rules as graphical hosts. Tests needing GPU
latency use delayed fake completions; Null does not guarantee device timing equivalence.

### Capacity And Overflow

Preallocate or admit pools, particle/scratch slices, frame copies, per-view sort data,
GPU work, completion and retirement records before activation. Never grow those pools
during Update, spawn or extraction. Resizing requires explicit preparation and peak
old/new reservation. Zero steady-state allocation is a qualification requirement,
not a claim of unlimited capacity or a benchmark result.

| Condition | Outcome |
|---|---|
| Event queue full | Reject incoming/newest with EventQueueFull; never overwrite accepted entries |
| Instance/particle capacity exhausted | Reject new request with EffectCapacityExceeded; do not implicitly evict an existing effect |
| Renderer/memory/work budget exhausted | ResourceBudgetExceeded or declared cosmetic reduction at admission |
| Admitted cosmetic emitter reaches cap | Drop new births, retain existing particles, record a counter |
| Required gameplay capacity unavailable | Typed failure requiring explicit owner handling; no silent clamp/drop |

TryEnqueueSpawn returns `Result<VfxRequestId>` and copies bounded schema-typed parameters.
The scene owner serializes acceptance; authoritative producer inputs have stable
ordering before admission. Each clock domain has its own queue/cutoff under the aggregate budget;
no request is drained once per emitter or on both clocks. Freeze the accepted queue
prefix at tick start, drain at
most maxEventsPerTick, and defer requests generated during that update until the next
tick. Result/cancellation/retirement capacity is reserved before acceptance. Cosmetic
requests cannot consume configured gameplay reserves. All paths, including Null,
follow those rules and report rate-limited diagnostics; callers do not spin or block.

CPU, GPU, upload/sort copies and frames in flight count toward admitted peak memory.
ADR-012 cell reservations are slices of the host ledger, not an extra allowance.
Logical cancellation does not return credits for memory still used by readers/GPU.

### Scene, Streaming Cell And GPU Retirement

Scene/effect/request handles carry incarnation and slot generation; checked exhaustion
retires identities instead of wrapping. Scene replacement closes admission and
invalidates old handles at the owner safe point. Logical stopping is immediate;
physical memory reclamation is not. CPU jobs and snapshots retain their slices;
renderer resources retire only after frame readers and GPU fences finish. Retirement
records retain no raw VfxWorld/ECS pointers and cannot publish into a replacement scene.

[ADR-012](012-world-streaming-partition-authority-and-subsystem-boundaries.md) cell-bound
effects additionally capture StreamingFence with PartitionEpoch and cell generation.
An IFeatureStreamingProvider adapter reports admitted cost, Ready/Prepared and Retired.
Prepared publication attaches already admitted resources without allocation or new
recoverable work. Required/Optional/Degradable behavior follows the cell contract.

Partial eviction retires only cell-owned effects, rejects late fenced spawns, and
acknowledges Retired only after CPU/asset/snapshot/device leases finish. Shared caches
have separate charged ownership; persistent/network effects are not evicted by position.
Whole-scene teardown retires every remaining scope using the same registered dependency
DAG and Scene safe points. There is no synchronous GPU free or ordinary device-idle wait.

ADR-010 bounded teardown and ADR-012 timeout behavior apply: report incomplete retirement
and retain outstanding module/resource/asset dependencies safely. Never detach work
referencing freed state or claim a clean unload. Old retirement remains charged during
new-scene admission. Cancellation prevents stale publication, not guaranteed physical
interruption of I/O or GPU work.

### Render Primitives, Passes And Sorting

The normative document owns the schematic typed contracts: GpuVfxSimulationWork,
ParticleDataSource (CPU frame slices or GPU logical resource views), VfxRenderBatch,
DynamicInstanceTransform, DecalRenderBatch, VolumetricVfxBatch and VfxViewSortKey.
They carry frame/resource leases, checked layouts/bounds and scene/origin identity;
they never expose mutable simulation spans, native handles or backend contexts.
CPU simulation writes private SoA; extraction packs immutable CPU frame data and the
renderer performs uploads. Volumes have an explicit volume-grid contract separate
from particle topology; decals and light outputs use their own typed render contracts.

Shared PBR/unlit material compilation remains authoritative. RenderFrontend schedules
compute before dependent sort/cull and draw passes, with declared barriers. Opaque/
masked mesh particles use standard depth/opaque and permitted shadow caster passes.
Translucent/ribbon and additive paths depth-test without depth writes; additive skips
sorting. Batch color is unexposed linear ACEScg from
[ADR-037](037-scene-color-and-hdr-architecture.md) pipeline step 1: additive adds
scene-referred RGB with no coverage alpha; translucent/ribbon use the scene-color
resource's declared linear coverage/opacity alpha. VFX does not blend after
exposure or after the output transform. Decals and volumetric accumulation use capability-validated dedicated paths.
Shadow casting in this contract is opaque/masked mesh only; unsupported combinations
require an authored substitute or typed rejection, not a silently ignored boolean.

Sort/cull is per RenderViewId, covering editor/game, split screen, XR and reflections.
CPU radix sorting writes packed-frame index/key data; GPU sorting writes per-view
indices after simulation. sortDistance is a render key, never particle state. Neither
path reorders the simulation SoA. Equal keys use stable batch/particle IDs; nonfinite
keys fail validation. A pending CPU sort cannot block the frame: omit/report the
cosmetic batch under the declared view fallback rather than falsely submit sorted alpha.

## Consequences

- Scene ownership, logical GPU intent and renderer-native work have separate lifetimes.
- Bounded admission and failure outcomes make bursts/overload implementable without
  hidden heap growth, gameplay loss or unsafe resource reclamation.
- Domain fallback requires authored compatibility and budgets; some effects will be
  explicitly unavailable on unsupported hosts.
- Multi-view extraction requires extra admitted frame/index storage but leaves
  simulation state camera-independent except declared cosmetic depth input.
- Implementation qualification must cover overload, delayed completion, partial cell
  eviction, headless gameplay, graph dependencies and all supported renderer profiles.
  This documentation change does not implement those tests or runtime paths.

## Rejected Alternatives

- **Scene-owned native dispatch or direct extractor-to-graph submission**: Violates
  immutable snapshot consumption, resource scheduling and backend isolation.
- **Renderer-owned logical VFX world**: Moves gameplay/physics/scene ownership into
  a renderer that should consume values and typed work.
- **Synchronous GPU destruction on scene unload**: Releasing a logical handle does
  not prove frames in flight have completed; waits also stall ordinary frames.
- **Dummy headless gameplay or unconditional GPU-to-CPU fallback**: Changes behavior
  or admits unaffordable work without an authored compatible substitute.
- **Implicit pool growth, old-entry overwrite or silent required-event loss**: Breaks
  bounded capacity, reproducibility and observable admission failure.
- **Per-call backend branching or a proprietary VFX shading stack**: Duplicates
  capability policy and the standard material/render pipeline.
