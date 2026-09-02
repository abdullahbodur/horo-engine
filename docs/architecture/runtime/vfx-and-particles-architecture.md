# VFX And Particles Architecture

## Purpose And Scope

This document defines runtime particles, ribbons, mesh particles, decals and
volumetric VFX: scene ownership, emitter domain policy, bounded admission, immutable
extraction and renderer scheduling. [ADR-011](../../adr/011-vfx-effect-ownership-simulation-domain-and-renderer-boundary.md)
records the proposed decision. These are implementation contracts, not claims that
all paths or qualification tests already exist.

[ADR-123](../../adr/123-vfx-cpu-stage-order-determinism-and-gameplay-coupling.md)
specializes the gameplay-coupled CPU path: seven ordered stages, atomic candidate
commit, stable particle identity, versioned counter-based randomness, deterministic
parallel merge and schema-limited gameplay input/output.

[ADR-124](../../adr/124-vfx-gpu-simulation-readback-and-compute-fallback.md)
specializes GPU units: they remain visual-only; cooked readback is bounded,
asynchronous and observational; compute-less fallback is authored and admitted; and
CPU, GPU, shared and readback costs remain one auditable plan.

DCC workflows, full fluid solvers, atmospheric scattering and screen-space
post-processing remain outside this subsystem; see
[Advanced Rendering Architecture](./advanced-rendering-architecture.md).

## Ownership And Renderer Boundary

VfxWorld belongs to one SceneRuntime incarnation. It owns logical effect state,
CPU particle storage and bounded requests; it never owns a native graphics context,
encodes a command buffer, writes mapped GPU memory or submits Render Graph passes.
Host composition selects capabilities/adapters explicitly; inert descriptors do not
register services or discover a backend. No immediate-mode drawing is permitted.

| Component | Owner | Responsibility |
|---|---|---|
| VfxWorld | SceneRuntime | Coordinates effect state and admitted work for this scene incarnation |
| EffectSystem | VfxWorld | Instance lifecycle, compiled emitter descriptors, parameter bindings and ownership |
| SimulationDomainResolver | VfxWorld | Resolves each compiled emitter using validated asset intent and host policy |
| CpuParticleSimulator | VfxWorld | CPU SoA updates, including real gameplay-coupled simulation in headless hosts |
| GpuParticleSimulator | VfxWorld | Produces backend-neutral GpuVfxSimulationWork; tracks logical step/resource identities, never dispatches |
| NullParticleSimulator | VfxWorld | Visual-only suppression with normal admission, tick and lifecycle timing; no gameplay simulation substitute |
| DecalManager | VfxWorld | Logical projection volumes, fades and atlas requests; renderer owns physical atlas storage |
| EffectInstancePool | VfxWorld | Preallocated instance/emitter/CPU storage and explicit capacity admission |
| VfxEventQueue | VfxWorld | Bounded FIFO of admitted typed requests with deterministic drain and overflow outcomes |
| VfxRenderExtractor | VfxWorld | Writes immutable simulation/render descriptors into RenderWorldSnapshot; no graph submission |
| RenderFrontend / resource manager | Renderer | Validates snapshots, admits renderer storage, schedules compute/sort/cull/draw and retires resources |
| Concrete render backend | Renderer composition | Native allocation, upload, command encoding, barriers and completion fences |

Physical renderer allocations and retained snapshot leases may outlive logical
scene shutdown while retiring. They cannot retain raw VfxWorld/ECS pointers or gain
access to the replacement scene. This is deferred ownership, not a live old effect.

For footsteps, [ADR-091](../../adr/091-footstep-and-locomotion-event-ownership.md)
requires an application-owned post-commit adapter to join the Animation marker
occurrence with exact same-tick Character surface evidence. VFX receives only the
immutable deduplicated cue intent through normal bounded admission; it does not
query Character/Physics, infer locomotion cadence or change simulation when the
effect is missing, culled, over budget or shutting down.

## Frame, Thread And Timing Contract

Scene composition declares VFX access/dependency edges in the existing scheduler.
Gameplay-coupled CPU emitters use the owning fixed simulation tick after their
required physics results, with typed gameplay results consumed at a declared later
safe point. Cosmetic updates use Presentation before RenderExtraction. A compiled
emitter selects one clock/tick schedule; it is not advanced on both schedules.
Scene time/seed policy controls pause, delta and replay; VFX does not read wall time
or independently tick gameplay when presentation runs. Each VfxEventQueue belongs
to one clock domain; fixed and presentation queues have separate cutoffs and share
the admitted aggregate budget. FIFO is defined within that queue, never by draining
the same request once for each emitter or clock.

1. On the scene simulation owner, freeze each queue cutoff once at the start of
   its declared clock update. Drain at most maxEventsPerTick accepted requests in
   FIFO order; requests produced during the drain/update become eligible next tick.
   No backend invokes a spawn completion inline from submission.
2. Advance CPU candidate state in the ADR-123 order `Spawn -> Initialize -> Forces ->
   Integrate -> Collide -> Kill`, with a barrier between semantic stages. CPU work may
   use Foundation JobSystem/JobId over exclusive preassigned staging slices. Completion
   records are applied on the declared owner phase, never by worker callbacks mutating
   live ECS. The whole candidate commits atomically or is discarded.
3. GPU emitters produce bounded GpuVfxSimulationWork with scene/effect/emitter
   generation, step ID, delta/seed, parameters and typed input/output resource IDs.
   They do not encode GPU dispatches during VfxWorld::Update.
4. Extract immutable CPU frame data, GPU work and render descriptors into a
   frame-owned RenderWorldSnapshot. Extraction reads stable state; no particle
   lifetime, simulation order, seed or emitter state is modified.
5. At render frame synchronization, RenderFrontend consumes that snapshot, uploads
   bounded CPU data, and builds the graph: VFX Compute -> per-view Sort/Cull ->
   consuming depth/shadow/opaque/decal/volume/translucent passes. Backend encoding
   and native synchronization stay on the renderer's permitted execution roles.

### CPU Stage Order And Atomic Commit

Every gameplay-coupled CPU emitter uses this exact logical pipeline for one admitted
fixed tick:

```text
Spawn -> Initialize -> Forces -> Integrate -> Collide -> Kill -> Extract
```

`VfxWorld` owns queue cutoff, capacity admission, deterministic birth count, monotonic
spawn ordinal and slot reservation in Spawn. `CpuParticleSimulator` initializes every
required new-particle attribute once; force kernels produce staged acceleration/
velocity contributions; Integrate advances candidate motion/age; Collide consumes an
immutable generation-matched Physics/scene query snapshot; and Kill selects survivors,
stable compaction and bounded death/collision/sub-emitter occurrences. Requests born
during a stage enter next tick's queue. No stage invokes gameplay, Audio or another
emitter reentrantly.

Spawn through Kill mutate one exclusive candidate generation. VfxWorld publishes
particle state, emitter cursor/spawn ordinal and occurrence batches together only when
the source tick commits. Failure/cancellation/capacity exhaustion preserves the prior
generation and publishes no partial state/effect. `VfxRenderExtractor` then reads the
committed post-Kill generation into immutable frame-owned data; it never mutates the
SoA or repairs a failed stage. Kernel fusion/vectorization is allowed only when it is
observably equivalent to these barriers.

Cooked kernels declare stage reads/writes, required snapshots, scratch and bounded
outputs. Cook rejects backward/cyclic dependencies, uninitialized reads, conflicting
writers, private/render payload access from gameplay, GPU authoritative output and
unbounded occurrences. Runtime violations discard the candidate and return a typed
stage/schema/access result; Shipping never continues a half-updated buffer.

### Stable Identity, RNG And Parallel Reproduction

`ParticleSimulationId` derives from effect/emitter/activation identity and a checked
monotonic spawn ordinal, not from a SoA/freelist slot, pointer or worker. Slot reuse
never reuses identity. Iteration, compaction and occurrence merge use stable particle
identity (or a declared semantic key with particle identity as final tie-break).

Randomness uses versioned counter-based `VfxRngV1` per-particle semantic streams.
Cook assigns each stochastic property/node a stable `VfxRngChannelId`; samples derive
from project/effect seed, effect/emitter/activation and particle identity, channel,
sample ordinal and algorithm version. Emitter spawn decisions use a separate tuple
keyed by activation, committed step, request/trigger and channel. There is no mutable
shared emitter PRNG, platform entropy, wall time or standard-library distribution.

RNG integer/range mapping and integer-to-`[0,1)` conversion have golden cross-platform
vectors. IDs, RNG bits, stage/event order, births/deaths and integer/quantized fields
are exact across supported CPU runs with identical cooked digest and ordered inputs.
Floating simulation/collision attributes are bit-exact only for a separately qualified
deterministic-math/Physics fingerprint; otherwise they use declared numeric tolerances.
This exact/tolerant split is the reference for later CPU/GPU equivalence claims.

Parallel jobs receive disjoint preassigned candidate/output ranges. Stable barriers,
merge slots, reduction trees and particle/collider/feature tie-breaks make job count,
SIMD width, allocation and completion order irrelevant. One failed partition fails the
whole candidate; completed ranges never publish incrementally.

[ADR-010](../../adr/010-job-waiting-and-operation-store-ownership.md) governs jobs,
operation tracking and allowed teardown drains. No ordinary owner/render/transport
path waits on workers or GPU completion, and workers do not wait on nested jobs.
A pending required CPU update holds dependent simulation progress through the host's
non-blocking scheduling/loading policy; it cannot silently use stale gameplay
results. Cosmetic work may use a documented skip/reset outcome under budget.

[ADR-018](../../adr/018-command-registration-permissions-threading-and-packaged-build-policy.md)
WorkerJob is for CPU preparation, not permission to encode native GPU commands.
RenderSafePoint debug mutations enter the same render frame synchronization seam;
normal simulation descriptors are renderer inputs, not console commands. Long
user-visible preload/export tasks use the application-owned OperationStore; per-tick
VFX work does not create a second operation service.

GPU step submission is keyed by scene incarnation, emitter generation and step ID.
A snapshot may be used by multiple views/frames, but a GPU step is submitted once,
not once per view. The frontend tracks ordered submitted steps and returns bounded
admission/submission status records to the scene owner. Submission acknowledgement
is not GPU retirement. Pending work and frames in flight stay leased and charged.
A failed/uncertain submission follows renderer recovery, never blind step replay.
When the pending-work cap is reached, stop accepting cosmetic GPU steps/spawns and
report backpressure; no unbounded catch-up queue or automatic huge CPU migration.
Resume using the authored bounded restart/reset policy, not a giant accumulated delta.

GPU depth collision uses one explicitly selected cosmetic collision view and its
previous completed depth snapshot (with matching scene/view/origin generation).
Missing or stale depth uses the authored no-collision/substitute/disable policy.
It cannot require the current depth pass that itself depends on VFX Compute, or
advance the same emitter separately for each eye/view. No GPU particle readback
may drive authoritative gameplay in the ordinary frame loop.

### Opt-In GPU Readback Observation

GPU readback is absent unless the cooked emitter declares one bounded
`GpuVfxReadbackDescriptor`. Its normalized schema, authoring/diagnostics/capture/
cosmetic purpose, optional-versus-required-visual policy, cadence, maximum records,
bytes and pending results are fixed before activation. Cook rejects native layouts,
unbounded fields, invalid products and any gameplay-authoritative consumer. Stable
particle identity or a versioned reduction order selects records; native buffer order
and completion timing have no semantic authority.

VfxRenderExtractor publishes only logical intent. RenderFrontend validates the
effective readback capability and reservation, then schedules one post-compute graph
copy for the source step. Renderer owns native staging, encoding, mapping/cache rules,
fences and deferred destruction. A delayed result carries scene/effect/emitter/step,
resource generation, schema version, numeric fingerprint and typed status. Ordinary
paths never wait for same-frame completion or map device storage directly.

Optional readback pressure omits the newest observation and reports a bounded gap;
it does not stop the visual effect. Required visual/capture readback follows its
authored suspend/stop/fallback policy. Neither grows a queue, retries blindly, changes
domain mid-step or returns zero-filled data as success. Cancellation/stale generation
closes publication while all native leases and charges remain until completion.

## Domain Selection And Asset Intent

Resolution is **per compiled emitter simulation unit**, not per composite graph.
A unit has one particle state and one resolved domain. Any gameplay interaction or
CPU geometry-query requirement makes the whole unit CPU-mandatory, even above the
count heuristic. It cannot be partly CPU and partly GPU. Graphs may contain separate
CPU and GPU units only with explicit typed boundaries. A dependency requiring
synchronous particle state or gameplay results propagates CPU requirements through
that connected dependency; unsupported cycles/contracts fail cook. A CPU event may
trigger a visual GPU child through a bounded next-tick request. A GPU child cannot
feed synchronous authoritative state back to the CPU parent.

```cpp
enum class SimulationPreference : uint8_t {
    Automatic, RequireCPU, PreferCPU, PreferGPU, RequireGPU
};
enum class ResolvedSimulationDomain : uint8_t { CPU, GPU, Null };
```

Cook records supported kernels, gameplay dependencies, authored fallback assets,
required capabilities and cost bounds. Load/admission resolves against a captured
host capability/quality-policy revision. Call sites do not select concrete backends.
Changing policy requires re-admission/restart at a safe point, not live CPU/GPU state
migration. Migrate the previous proposed SimulationDomain field in cooked descriptor
versioning: Automatic -> Automatic, CPU -> RequireCPU, GPU -> PreferGPU, with a
migration diagnostic. There are not two parallel domain selectors.

An authored CPU/substitute fallback additionally carries a versioned visual envelope:
compatible parameters/outputs, finite CPU peak work, maximum reduced particle count
and explicitly substitutable noise/collision/sort/volume/field features. A cooked CPU
kernel alone does not prove compatibility or affordability. Allowed differences are
visual only; a paired CPU gameplay unit retains identical accepted requests, fixed
ticks and outputs whether the GPU visual runs, falls back or is suppressed.

Resolution order:

1. Validate CPU-mandatory dependencies. RequireGPU plus a CPU-mandatory unit is a
   typed incompatible-asset error; all other preferences resolve that unit to real
   CPU simulation. A missing CPU kernel is an error, never dummy gameplay.
2. Apply host mode and explicit intent. Headless visual-only units may use Null only
   when their authored policy permits visual suppression; gameplay units still use
   CpuParticleSimulator. RequireGPU needs its capabilities or a separately authored
   fallback effect; otherwise the effect is unavailable. Null suppression is an
   explicit authored headless fallback, never an implicit override of RequireGPU.
   RequireCPU never becomes GPU.
3. PreferCPU selects a supported admitted CPU path, then an explicitly permitted GPU
   path; PreferGPU does the reverse. A CPU fallback requires a compatible cooked CPU
   kernel and admitted CPU work/memory. GPU-only noise/depth features are substituted
   only by an authored fallback, never silently deleted from required behavior.
4. Automatic prefers GPU for eligible visual units above
   VfxQualityPolicy.autoGpuParticleThreshold (desktop default 2048), CPU at/below it.
   Authored cost/kernel requirements may prefer GPU even below it, after step 1.
   It uses the same supported, budget-admitted fallback rules as a preference.

The threshold is a configurable heuristic, not a capacity limit. Per-emitter caps
and aggregate CPU/GPU/memory/work budgets apply to **every** profile. Cosmetic counts
may be clamped only where the asset declares a valid reduced-count result. Required
gameplay counts are never silently clamped: reject admission and let the gameplay
owner use its explicit safe fallback/loading/failure policy. A 500k-particle GPU
asset is not automatically run as 500k CPU particles when GPU support is missing.

### Capabilities And Fallback Matrix

VfxCapabilities is a typed snapshot of actual selected-device capabilities, including
compute, indirectDraw, gpuSorting, volumeTextures, vectorFields and resource/work
limits. VfxQualityPolicy adds validated positive finite budgets, CPU fallback caps,
autoGpuParticleThreshold and permitted cosmetic degradation. Backend names do not
imply any capability. The renderer parity/availability contracts remain authoritative;
VFX fallback never silently switches the selected renderer or an interactive host to
RenderNull.

| Host policy profile | Domain behavior | Unsupported/budget outcome |
|---|---|---|
| Headless/test | Real CPU for gameplay; permitted visual suppression through Null | Same queue/tick/lifecycle contract; no GPU allocations; incompatible required visuals reported unavailable |
| Low visual budget | Admitted CPU, GPU only if actual capabilities and policy permit | Default cosmetic CPU fallback cap 512 per emitter; authored substitute or typed rejection for unsupported kernels/RequireGPU |
| Desktop baseline | CPU/GPU according to intent, actual capabilities and costs | Finite per-emitter and aggregate caps; reject new work or apply authored cosmetic reduction |
| High visual budget | Larger admitted CPU/GPU budgets and optional sort/volume/field paths | Still bounded; missing capabilities use authored fallback or typed unavailability, never unrestricted simulation |

Legacy labels es3, dx11/opengl4 and dx12_vulkan/metal are not VFX capability enums.
They may identify external product presets, but must resolve through typed capability
facts and quality policy. Identical inputs resolve deterministically; this does not
promise bitwise-identical visual CPU/GPU algorithms across devices.

Readback is a separate effective capability with normalized format/alignment,
device-to-host copy, maximum byte/work and pending-result limits. Missing optional
readback produces an explicit degraded resolution while the GPU visual may continue.
Missing required visual readback uses an authored compatible fallback or typed
`VfxReadbackUnavailable`. Missing compute uses the same matrix: `RequireGPU` does not
fall back implicitly; `PreferGPU`/`Automatic` may select an admitted compatible CPU
kernel, authored substitute or permitted suppression in that order. No outcome
selects another graphics backend or makes Null an interactive-renderer fallback.

### Null Timing And Gameplay Parity

Null suppresses visual particle kernels/draws, not scheduler semantics. It obeys the
same submission cutoff, FIFO drain limit, next-tick spawn/result delivery, owner phase,
logical lifetime and cancellation rules as the real paths. Completion is queued,
never inline merely because there is no GPU. Device timing is not simulated: tests
requiring delayed completion/retirement use controllable bounded fake completions.

Particle collision/death-derived gameplay, audio or other externally observable
nonvisual events cannot rely on Null dummy particles. Such dependencies require the
real CPU implementation or a separately authored deterministic CPU event source.
Visual-only Null units emit only declared logical lifecycle outcomes and do not
fabricate per-particle collisions. Headless gameplay uses the same CPU kernels,
seeds, fixed ticks and admission requirements as graphical execution.

## Capacity, Pool Exhaustion And Event Overflow

Pool sizes, particle slices, scratch, packed frame data, per-view sort indices,
pending GPU work, completion records and retirement entries are preallocated or
admitted during bounded preparation before activation. No pool grows on Update,
spawn, queue drain or extraction. Resizing is an explicit asynchronous preparation
transaction with peak old/new memory admitted; old readers/fences must retire before
reuse. Zero steady-state heap allocation is a requirement to verify for these paths,
not an unlimited-capacity or measured performance claim.

TryEnqueueSpawn returns `Result<VfxRequestId>` and copies a bounded typed parameter
block, never an arbitrary heap-allocating VariantMap. The scene owner serializes
accepted requests; external producers submit typed commands through declared owner
seams. Authoritative producers have stable tick/producer/sequence ordering before
admission, so OS worker arrival races are not gameplay order. FIFO means this
accepted owner order. Retrying failed submission is explicit, not hidden recursion.

- Queue full: reject the **incoming/newest** request with EventQueueFull. Never
  overwrite accepted entries or silently drop the oldest. Preserve reserved gameplay
  capacity; cosmetic requests cannot consume the configured gameplay reserve.
- Pool/particle/renderer reservation exhausted: reject the new request with
  EffectCapacityExceeded or ResourceBudgetExceeded; existing effects are not evicted
  implicitly. A queued request that fails later receives a next-phase typed result
  from reserved result capacity. Failed admission leaves no partial live effect.
- An admitted emitter reaching its particle cap drops **new cosmetic births**, not
  existing particles, and records a counter. Gameplay emitters must reserve their
  authored worst case; inability to satisfy it reports a gameplay failure rather
  than silently changing the simulation.
- Rejections/degradation increment structured counters and rate-limited diagnostics.
  Callers may discard a cosmetic request only after receiving that outcome. A required
  caller must handle rejection with its declared safe policy; it may not block/spin.

Drain work is bounded by maxEventsPerTick and admitted per-request cost. Remaining
entries retain FIFO order for later ticks. Results, cancellation and retirement have
reserved capacity so overload cannot prevent cleanup. Configuration validates queue,
result and gameplay-reserve limits together. Null follows the same capacity policy;
a separate reduced visual resource budget cannot consume the gameplay reserve.

## Scene And Cell Teardown

Effect/request handles include scene incarnation, slot generation and owner identity.
Zero is invalid; checked exhaustion retires a slot/incarnation rather than wrapping.
Scene replacement closes admission, invalidates the old incarnation and logically
stops effects at the owner safe point. It cancels pending requests and detaches
bindings before destroying entities they depend on. CPU jobs, frame-data readers,
asset leases and GPU submissions retain their own immutable retirement records.

The renderer resource manager owns deferred GPU destruction after all frame readers
and completion fences release resources. Snapshot/job-owned CPU slices also cannot
be reused early. Old allocations remain charged; no normal-frame device-idle wait,
forced free or raw pointer callback into a destroyed VfxWorld is permitted. Only
logical deactivation is immediate; physical retirement can span frames.

Cell-bound effects additionally capture ADR-012 StreamingFence (partition identity,
PartitionEpoch, cell and StreamingGeneration). An IFeatureStreamingProvider adapter
participates in estimate/reserve, Ready/Prepared publication and Retired barriers.
Its prepared publication attaches already admitted effects without allocation or
recoverable work. Required/Optional/Degradable behavior follows that cell's validated
policy; ambient visuals are not automatically Required or automatically Optional.

On partial cell eviction, reject late spawns/publication for that fence and retire
only the cell-owned effects. Retired is acknowledged after CPU readers/jobs and
renderer fences release all cell-owned resources, not when the stop command is queued.
Shared asset caches keep separate charged leases. Persistent or network-owned effects
are not removed merely because their positions lie within the cell. Normal cell
eviction, whole-scene replacement and shutdown use the same registered dependency
DAG and Scene safe points; whole-scene shutdown retires all remaining ownership scopes.

ADR-010's bounded teardown drain and ADR-012's timeout safety apply: on timeout return
a typed incomplete-retirement diagnostic and retain outstanding resource/module/
asset dependencies in a retirement owner. Never detach work referencing freed state
or report a clean unload before retirement. New scene admission accounts for retained
old resources. Cancellation fences publication but cannot promise physical I/O/GPU abort.

## Immutable Extraction And Typed Contracts

```text
Scene simulation: CPU SoA + logical GPU work
    -> VfxRenderExtractor
    -> RenderWorldSnapshot (frame-owned immutable values and leases)
    -> RenderFrontend (validation, bounded CPU upload, graph construction)
    -> VFX Compute (once per step)
    -> per-view Sort/Cull
    -> standard Shadow/Depth/Opaque/Decal/Volume/Translucent passes
    -> renderer deferred retirement
```

CPU simulation writes only its private SoA. Extraction copies/packs CPU data into
frame-owned slices; only the renderer uploads those slices to GPU buffers. GPU
identities below are Horo-owned generation-safe logical resource leases resolved by
the renderer, not native handles. Every slice is bounds/layout checked against its
retained storage, with checked offset/count/stride arithmetic. GPU buffers and
indirect arguments require correct admitted usage/size and current resource generation.

The shapes below are schematic contracts, not installed public headers. FrameSlice
refers to immutable frame-owned storage, not an unowned span into simulation memory.
GpuVfxStateHandle identifies a renderer-owned state bundle (particle ping-pong,
count/indirect and scratch resources); it exposes no allocation or encoding API.
Each work/output step pins its resource version until all consumers retire. Later
steps cannot overwrite storage still referenced by an older snapshot; a full version
ring causes the documented backpressure. Reusing a snapshot renders its retained
output version, not whichever mutable GPU buffer happens to be current.

```cpp
struct CpuParticleView {
    FrameSlice packedParticles;
    ParticleLayoutId layout;
    uint32_t particleCount;
};
struct GpuParticleView {
    GpuVfxStateHandle state;
    VfxStepKey step;                // retained output version consumed by this view
    ParticleLayoutId layout;
    uint32_t maxParticles;
};
using ParticleDataSource = std::variant<CpuParticleView, GpuParticleView>;

struct GpuVfxSimulationWork {
    VfxStepKey key;                 // scene + effect/emitter generations + step
    GpuVfxStateHandle state;
    VfxKernelId kernel;
    FrameSlice parameters;         // delta, seed, bounded typed kernel inputs
    OptionalDepthSnapshot collisionDepth;
};

enum class VfxParticlePass : uint8_t {
    Opaque, Masked, Translucent, Additive
};
enum class VfxPrimitiveTopology : uint8_t {
    CameraFacingBillboard, RibbonStrip, InstancedMesh
};
enum class VfxShadowPolicy : uint8_t { Disabled, OpaqueMaskedCaster };
struct DynamicInstanceTransform {
    Mat4 localToRenderOrigin;
};
struct VfxRenderBatch {
    VfxBatchId id;
    VfxParticlePass pass;
    VfxPrimitiveTopology topology;
    MaterialId material;
    OptionalMeshHandle mesh;        // required for InstancedMesh
    ParticleDataSource instances;
    FrameSlice instanceTransforms;  // DynamicInstanceTransform layout
    RenderRelativeBounds bounds;
    VfxShadowPolicy shadows;
};
struct DecalRenderBatch {
    DecalId id;
    MaterialId material;
    DecalProjectionPath path;       // deferred or forward, validated capability
    DynamicInstanceTransform transform;
    Vec3 halfSize;
    float fadeFactor;
    RenderRelativeBounds bounds;
};
struct VolumetricVfxBatch {
    VfxBatchId id;
    MaterialId material;
    VolumeGridView density;         // typed retained volume resource + dimensions
    DynamicInstanceTransform transform;
    RenderRelativeBounds bounds;
};
struct VfxViewSortKey {
    RenderViewId view;
    VfxBatchId batch;
    float sortDistance;             // render-origin-relative key, never particle state
};
```

VolumetricVfxBatch is a separate volume-grid contract, not a DecalBox particle topology.
Density dimensions/format, sample usage and lifetime are validated; unsupported volumes
use authored fallback/unavailability. Light outputs use the renderer's existing typed
LightData contract and admitted light budget. Shading/layout/pass combinations,
transform count and topology are validated before publication, not inferred from
optional native pointers. The proposed ParticleBufferView-only model is replaced by
ParticleDataSource; renderer-owned upload makes CPU/GPU storage roles explicit.

Spatial extraction follows ADR-026 canonical world coordinates and a captured render
origin/revision. Per-view keys and packed transforms use that same origin; rebasing
cannot change durable emitter identity or race extraction. RenderWorldSnapshot carries
bounded arrays of GPU work, particle batches, decals and volumes alongside scene data,
with scene/origin revision and all leases needed until the final consumer retires.

## Pass Placement, Materials And Per-View Sorting

[Material And Shader Model](./material-and-shader-model.md) owns shared PBR/unlit
compilation. Particles/decals use Unlit, DefaultPBR, DecalPBR or DecalEmissive with
validated opaque/masked/translucent/additive blend modes. Flipbooks use normalized
particle age. Soft-particle fading samples declared scene depth in the draw pass.

| Output | Graph placement |
|---|---|
| CPU particle data | Bounded renderer upload before consuming passes |
| GPU particle state | Compute update before every dependent sort/cull/draw; graph declares read/write and queue barriers |
| Opaque/masked mesh particles | Depth prepass and G-Buffer or opaque forward path; depth writes enabled |
| OpaqueMaskedCaster | Standard shadow caster pass with matching material alpha test; consumes the same completed simulation state |
| Deferred/forward decals | Supported decal/G-Buffer or forward-lighting path; no particle topology workaround |
| VolumetricVfxBatch | Volume injection/accumulation and composite with declared depth/light dependencies |
| Translucent/ribbons | Per-view sort then translucent forward; depth test, no depth write |
| Additive | Additive forward; depth test, no depth write, no distance sort |

Shadow casting is supported here only for opaque/masked mesh particles. Requesting
OpaqueMaskedCaster on ribbons, translucent/additive particles, decals or volumes is
an unsupported combination requiring authored substitute or typed rejection. It is
not a bool that silently inserts arbitrary shadow passes.

Each RenderViewId (game, editor, split screen, XR or reflection) gets its own sort/cull
indices and keys over compatible immutable data. CPU radix sorting operates on packed
frame data or an index copy in extraction/render preparation, never reorders the
simulation SoA or writes particle distance fields. sortDistance lives only in
VfxViewSortKey. Equal distances use stable batch/particle identity; reject nonfinite
keys. GPU sort likewise writes separate per-view index buffers after simulation.
Sorting one view cannot change another view or advance the emitter again.

CPU sorting may use JobSystem staging with completion published before consuming the
view. It cannot block extraction/render on a pending worker. If its bounded work is
not ready, the declared cosmetic view fallback omits that batch and reports it; it
does not submit unsorted alpha as though sorting succeeded. The frontend sorts opaque
batches front-to-back alongside ordinary meshes. Additive sorting is skipped.

## Particle System Data Model

A particle system is a template that describes how particles are spawned,
simulated, rendered, and destroyed. The sortMode is validated against its output
material: None/OldestFirst cannot override required back-to-front alpha ordering.
Authored age order is allowed only for an output whose blend contract permits it.

```cpp
struct ParticleSystemDescriptor {
    ParticleSystemId id;
    SimulationPreference preference; // resolved once per admitted emitter unit
    uint32_t maxParticles;
    EmitterShape shape;
    SpawnRate spawnRate;
    LifetimeRange lifetime;
    InitialVelocityRange velocity;
    InitialSizeRange size;
    InitialColorRange color;
    MaterialId material;
    VfxPrimitiveTopology renderMode;
    SortMode sortMode;            // None, ByDistance, OldestFirst
    CollisionMode collisionMode;  // None, Planes, SceneDepth, PhysicsWorld
};
```

### CPU Simulation Layout (Structure of Arrays)

To maximize SIMD vectorization and cache locality, CPU particle memory is stored
as aligned slices of one pool-owned allocation. The spans below are mutable only
by the owning simulation/staging job; extraction never retains them. Capacity is
fixed at admission, and activeCount never exceeds it. Float positions are bounded
emitter-local coordinates with a canonical WorldCoordinate64 origin; large-world
rebasing uses versioned staging rather than treating floats as durable world identity:

```cpp
struct CpuParticleBufferSoA {
    std::span<float> posX, posY, posZ;
    std::span<float> velX, velY, velZ;
    std::span<float> scaleX, scaleY;
    std::span<float> rotZ, rotVelZ;
    std::span<uint32_t> packedColor;
    std::span<float> age, maxAge;
    std::span<uint32_t> customFlags;
    uint32_t activeCount{0};
    uint32_t capacity{0};
};
```

The SoA is never a gameplay capability. `customFlags` and additional compiled channels
remain private unless their schema class explicitly projects a bounded immutable value
through the gameplay seam below.

### GPU Compute Simulation Layout

The renderer owns GPU device buffers and runs the cooked kernels in declared graph
passes. This shader example is backend-private, not a public VFX API or permission
for VfxWorld to allocate/dispatch:

```hlsl
struct GpuParticleData {
    float3 position;
    float  age;
    float3 velocity;
    float  maxAge;
    float2 size;
    float  rotation;
    float  rotationVelocity;
    uint   packedColor;
    uint   flags;
};

// Ping-pong buffers:
StructuredBuffer<GpuParticleData>   CurrentParticles : register(t0);
RWStructuredBuffer<GpuParticleData> NextParticles    : register(u0);
```

## VFX Graph Asset & Compilation

A VFX graph is an authored node graph that defines a complex effect composed of
emitters, forces, turbulence, events, and render outputs.

```text
VFXGraph
  +-- Spawn Context (Rates, Bursts, Triggers)
  +-- Update Context (Forces, Turbulence, Drag, Collision, Age)
  +-- Output Context (Billboard, Ribbon, Mesh, Decal, Light, Audio)
```

Graph nodes:

| Node | Purpose |
|---|---|
| `Emitter` | Spawns particles continuously or on event. |
| `Force` | Applies gravity, wind, vortex, attraction. |
| `Noise` | Adds turbulent displacement (simplex, Perlin, curl noise). |
| `Collision` | Bounce or die on collision with planes, depth, or physics geometry. |
| `ColorOverLife` | Animates color and opacity curves by normalized particle age. |
| `SizeOverLife` | Animates size curves by normalized particle age. |
| `SubEmitter` | Spawns child particles on birth, death, or collision. |
| `Decal` | Projects a decal at particle position or collision point. |
| `Light` | Spawns a temporary point light (bounded, tier-aware). |
| `Audio` | Triggers an audio event on spawn or collision. |

Graphs compile ahead-of-time (or at asset cook time) into runtime descriptors and
optimized compute/CPU kernels. Compilation validates emitter dependency closure,
CPU-mandatory conflicts, fallback compatibility, bounded parameters and peak cost.
Asset versions migrate the previous domain field explicitly; cook diagnoses required
unsupported nodes rather than silently dropping gameplay behavior. The graph runtime
never interprets arbitrary scripts per particle.

## Decals

A decal is a projected texture applied to scene geometry within an oriented bounding box.

```cpp
struct DecalDescriptor {
    DecalId id;
    MaterialId material;
    ProjectionMode mode;      // BoxProjection, OrientedBox
    Vec3 halfSize;
    float fadeAngle;
    float fadeOutDistance;
    float lifetimeSeconds;
    bool isPermanent;
};
```

- **Projection Mode**: Default is deferred projection writing to the G-Buffer.
  Forward decals use clustered/tiled decal lists on forward feature tiers.
- **Lifetime**: Supports permanent environmental decals, timed fading decals
  (e.g., bullet holes, blood splatters), and event-driven removal.
- **Atlas Management**: `DecalManager` groups logical atlas requests; the renderer
  owns texture packing/upload and deferred atlas retirement under the admitted budget.

## Event-Driven Spawning & Audio Coupling

Gameplay requests effects through fallible bounded admission to VfxEventQueue;
this schematic payload is copied into queue-owned storage:

```cpp
struct VfxSpawnRequest {
    AssetId effectAsset;
    Transform worldTransform;
    std::optional<Vec3> impactNormal;
    float scale{1.0f};
    VfxParameterBlock parameters; // fixed-capacity, schema-typed values
    VfxOwnershipScope ownership;  // scene, cell fence, or explicit persistent owner
    VfxRequestClass requestClass; // gameplay-required or cosmetic
};
```

### Gameplay Payload Boundary

Cook classifies every custom channel as `SimulationInternal`, `RenderOnly`,
`GameplayInput` or `GameplayOutput`. Gameplay writes only declared typed/ranged
GameplayInput parameters through bounded VFX commands before the tick cutoff;
VfxWorld copies them into an immutable generation/schema-keyed input snapshot. Late,
stale, nonfinite, unauthorized or private-channel writes return typed failure and do
not alter the active step.

GameplayOutput is available only from committed real CPU simulation. Its schema fixes
source stage, maximum occurrences, stable order, target owner boundary and required/
cosmetic policy. It exposes semantic IDs and bounded authored hit/position/normal/
payload values, never mutable spans, SoA indices, render fields, pointers or native
Physics handles. The application adapter drains it at the gameplay owner's next safe
point; delivery cannot reenter or rewrite the source step. Required output capacity is
reserved before stepping, so overflow fails the candidate instead of dropping events.

Gameplay may read only declared last-committed effect-level aggregate snapshots.
Per-particle queries are not baseline API. GPU readback, RenderOnly fields, extraction
and Null cannot produce authoritative GameplayOutput. Invalid stage/private/schema
access returns `VfxGameplayAccessDenied`, `VfxPayloadSchemaMismatch`,
`StaleVfxGeneration` or `VfxStageContractViolation` with zero mutation.

### Audio Routing

VFX graphs trigger audio events on spawn, collision, or sub-emitter creation. Audio
events are bounded typed requests to AudioWorld with asset/event IDs, owner fence
and world position; VFX never plays sounds directly. Queue rejection remains visible
and follows the same required/cosmetic failure distinction. Gameplay-relevant or
headless-required audio is driven by CPU/logical events, not GPU readback or Null
fabricated collisions. Renderer completion never calls audio from a native GPU thread.

## Performance Budgets And Diagnostics

Budgets cover maximum particles per emitter, concurrent instances per asset, emitter
slots, decals, pending events/results, worker scratch, per-view packing/sorting,
GPU work and every frame/retirement lease. Baseline configurable defaults remain
16,384 aggregate CPU particles, 1,000,000 GPU particles, 256 decals and four transient
lights per effect. They are ceilings, not guaranteed allocations. Device byte/work
limits, host aggregate budgets and required gameplay reservations may lower usable
capacity. Every count/byte product is checked before allocation or admission.

Admission includes all simultaneous CPU/GPU/upload/sort copies and frames in flight;
old scene/cell retirement stays charged. Streaming cell reservations are slices of
ADR-012's aggregate ledger, not an extra VFX allowance. Resource growth needs prior
reservation; GPU completion, not logical release, returns credits. Bounded extraction
work prevents starting excess work, not preempts native calls or guarantees frame time.

GPU readback additionally charges copy work/bytes, staging capacity, pending result
records, retained source generations and consumer records. `VfxWorld` produces one
admission plan covering CPU, GPU, shared and readback costs; renderer resource/work
reservations referenced by it are the same host-envelope charges, not a second VFX
budget. Fallback requires a complete new plan, and replacement overlap remains charged
until retirement. GPU/readback pressure cannot consume reserved gameplay CPU work or
output capacity.

The single overflow policy above rejects incoming requests/new cosmetic births.
There is no separate implicit oldest/farthest eviction policy. Authored quality
reductions are selected during admission/restart and reported with the resolved
cap, capability/policy revision, emitter identity and reason. Diagnostics expose
queue/pool use, rejected requests, deferred steps, gameplay failures, sort omissions,
retired bytes and timeout causes through the observability system.

## Typed Failure Contract

Follow [ADR-008](../../adr/008-error-model-exception-boundary-and-registry.md) Result/Error.
Failures retain scene/emitter/request identity, policy/capability revision and the
underlying asset/renderer/job cause. DomainConflict, UnsupportedCapability or
MissingKernel rejects incompatible admission. EventQueueFull, EffectCapacityExceeded
and ResourceBudgetExceeded preserve existing state and require explicit caller
handling. Cancelled/StaleGeneration suppress publication but still route matching
retirement acknowledgements. RetirementIncomplete keeps resources/dependencies
charged and alive; it is never translated into successful shutdown.

## Testing And Verification Requirements

These are qualification scenarios for implementation, not tests delivered by this
architecture-only change:

- Resolve a mixed CPU/GPU graph per emitter; force a high-count gameplay emitter to
  CPU; reject RequireGPU conflicts and invalid synchronous cross-domain dependencies.
- Exercise 2048/2049 default heuristic, configured thresholds, 512 cosmetic fallback,
  missing kernels/capabilities, RequireGPU authored fallback/unavailability, and no
  silent gameplay count clamp or concrete renderer switch.
- Run gameplay CPU kernels with identical seed/tick inputs in graphical and headless
  hosts; verify Null's accepted FIFO, next-tick results, pause/cancel/lifetime timing.
  Fake delayed submission/fences rather than assuming Null matches device latency.
- Record every ADR-123 stage boundary and verify exact stage order, no reentrant
  spawn/output delivery, atomic rollback on a stage failure and read-only extraction.
- Reproduce identical RNG words, particle identities, collision tie-breaks and stable
  merge order across repeated runs, worker counts and supported CPU platforms. Treat
  float-state equivalence as exact only under the same deterministic-math fingerprint.
- Attempt undeclared gameplay fields, private SoA access, render-only reads, schema
  mismatches and authoritative GPU/Null output; require typed rejection with no
  partial state mutation or gameplay publication.
- Fill instance/particle/event/result/frame pools. Verify incoming rejection, gameplay
  reserve, stable ordering, no old-entry overwrite, cleanup progress and zero hot-path
  heap growth, including bounded typed parameter copying and diagnostics.
- Verify extraction never submits passes, writes mapped GPU buffers or mutates SoA;
  validate CPU/GPU source layouts, slice bounds, generations and retained ownership.
- Render one snapshot in multiple views/frames: submit each GPU step once; sort each
  view independently; preserve SoA and stable ties; reject nonfinite keys and invalid
  shadow/volume/decal combinations. Exercise a missing previous-depth snapshot.
- Verify graph compute/read/write/sort/draw dependencies and native affinity on each
  supported backend/capability profile, including an explicit no-compute composition.
- Verify no-readback emitters schedule no copies; exercise every cooked schema/cadence/
  byte/pending limit, delayed gaps, stale completion, cancellation and device loss.
  Reject runtime field expansion and every authoritative gameplay readback consumer.
- Exercise compute/readback capability and budget failure for every ADR-124 fallback
  matrix row. Require recorded resolver evidence, an authored visual envelope and no
  backend switch, hidden node deletion or change to paired CPU gameplay outputs.
- Saturate mixed CPU/GPU/shared/readback charges and replacement overlap. Preserve
  gameplay reserves and return readback/GPU credits only after final fence retirement.
- Delay workers, GPU fences and snapshot readers across cell eviction and scene
  replacement. No early free/slot reuse, stale publication or lost accounting;
  Retired remains pending and teardown timeout retains dependencies safely.
- Verify provider Ready/Prepared/Retired barriers, authored optional/fallback behavior,
  typed failures, and partial cell eviction without stopping unrelated effects.

## Related Documents

- [ADR-011: Effect Ownership, Simulation Domain Policy and Renderer Boundary](../../adr/011-vfx-effect-ownership-simulation-domain-and-renderer-boundary.md):
  Proposed ownership, simulation and renderer-boundary decision.
- [ADR-123: VFX CPU Stage Order, Determinism and Gameplay Coupling](../../adr/123-vfx-cpu-stage-order-determinism-and-gameplay-coupling.md):
  Normative CPU stage, RNG, payload and gameplay publication contract.
- [ADR-124: VFX GPU Simulation, Readback and Compute Fallback](../../adr/124-vfx-gpu-simulation-readback-and-compute-fallback.md):
  Normative GPU authority, observation, fallback and shared-admission contract.
- [Particle Editor UI Reference](./particle-editor.html): Emitter stack, curve editing,
  and live preview panel.
- [Material And Shader Model](./material-and-shader-model.md): Particle and decal materials.
- [Rendering Architecture](./rendering-architecture.md): Render graph and pass extraction.
- [Scene Runtime Architecture](./scene-runtime.md): Scene runtime lifecycle and ownership.
- [Advanced Rendering Architecture](./advanced-rendering-architecture.md): Volumetrics and post-processing.
- [Audio Architecture](./audio-architecture.md): Audio event routing.
- [Asset Pipeline](./asset-pipeline.md): Effect compilation and cooking.
- [World Streaming Architecture](./world-streaming-architecture.md): Provider barriers and cell ownership.
- [Render Backend Parity Contract](./render-backend-parity-contract.md): Typed capabilities and equal backends.
- [Renderer Distribution And Availability](./renderer-distribution-and-availability.md): Explicit renderer selection.
