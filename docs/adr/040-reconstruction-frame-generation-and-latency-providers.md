# ADR-040: Reconstruction, Frame Generation and Latency Providers

- **Status**: Proposed
- **Date**: 2026-09-01
- **Supersedes**: None
- **Scope**: Reconstruction, denoising, frame generation, latency categories and provider ownership
- **Issue**: [RND-016.1](https://github.com/abdullahbodur/horo-engine/issues/422)
- **Jira**: [HORO-422](https://horo-engine.atlassian.net/browse/HORO-422)
- **Companion decision**: [ADR-033: Presentation and Display Ownership](033-presentation-and-display-ownership.md)
- **Normative documents**: [Rendering Architecture](../architecture/runtime/rendering-architecture.md), [Advanced Rendering Architecture](../architecture/runtime/advanced-rendering-architecture.md)

## Context

Advanced Rendering currently presents Native, TAAU, DLSS, FSR and XeSS as one
“upscaler interface”. That does not distinguish spatial scaling, temporal
reconstruction, effect denoising, synthetic frame generation or latency-control
hooks. These categories consume different inputs, own different history, change
different frame timelines and have different safe fallbacks.

A vendor package may expose several categories, but that does not make them one
feature or authorize one to enable another. In particular, frame generation must
not advance gameplay/simulation, input, render extraction or real-frame temporal
history, and it cannot truthfully be advertised as a latency reduction. Provider
selection must also remain backend-neutral and cannot leak SDK headers, native
handles or vendor IDs into scenes and materials.

This ADR owns category boundaries, provider registration/selection, common frame
inputs, history, synthetic presentation and latency seams. It does not choose a
specific vendor/version, dynamic-resolution algorithm, denoising algorithm,
presentation mode, material motion implementation or effect quality setting.

## Decision

### 1. Four provider categories have separate contracts

Horo defines these non-interchangeable categories:

| Category | Input/output and ownership |
|---|---|
| `ReconstructionProvider` | Converts one real rendered frame at a declared render extent into one real reconstructed frame at the target extent. Spatial and temporal reconstruction are distinct modes/capabilities. Temporal mode owns view history and anti-aliasing semantics. |
| `DenoisingProvider` | Filters one named noisy effect signal (for example ray reflection or GI) plus its effect-specific guides/history. The effect owner chooses it; it does not own scene/output resolution or global anti-aliasing. |
| `FrameGenerationProvider` | Produces optional synthetic presentation frames between qualified real reconstructed frames. It owns interpolation history and synthetic-frame GPU work only, never simulation or real-frame history. |
| `LatencyProvider` | Supplies optional scheduling/marker/wait integration for reducing or measuring end-to-end latency. It owns no image reconstruction, generated frame, present mode, gameplay clock or input semantics. |

One installed component may contribute descriptors to several registries, but
each category is independently reported, implemented, effectively admitted,
selected, initialized, failed and shut down. “Supports DLSS/FSR/XeSS” or a vendor
name is not a Horo capability. Product settings select a category route and an
ordered provider preference by stable Horo/provider IDs; they never encode native
SDK enums or choose the renderer backend.

The built-in baseline routes are:

- `native` reconstruction: no resolution conversion and no reconstruction-owned
  history; raster MSAA or a separate post AA recipe remains explicit;
- `taau` reconstruction: Horo-owned temporal anti-aliasing/upscaling provider;
- effect-owned declared denoisers or an effect's non-denoised/alternate fallback;
- `none` frame generation: present only real frames; and
- `default` latency: ordinary host input/frame/presentation scheduling with typed
  timing diagnostics and no vendor hook.

These IDs are route identities, not claims that every composition implements them
today. Native-resolution rendering is the required optional-reconstruction
fallback. No generated frames and default scheduling are always the semantic
fallbacks for optional frame-generation/latency features.

### 2. Registries and selection are host/frontend owned

Verified inert renderer/provider components contribute backend-neutral
descriptors at the application composition root. Descriptor creation and
registration perform no device call, SDK initialization, thread creation,
telemetry or global mutation. `RenderFrontend` seals category registries before
project work, intersects provider declarations with the selected backend's
reported/effective operations, cooked variants, driver policy and product plan,
then creates only the selected provider instances.

Descriptors include stable provider/category/version IDs, supported modes,
required backend-neutral operations/formats/stages, supported color/input/output
contracts, extent/scale/alignment ranges, history/surface requirements, finite
resource/work declarations, fallback identity and private adapter factory.
Exact SDK/runtime/library version, digest, license, redistributables and supported
OS/architecture/driver range are release/component metadata, not project data.

Public render/editor/application headers expose only Horo values. Vendor SDK
headers, feature structs, command contexts, resources, tokens, markers and native
handles stay in private provider/backend targets. A provider borrows a narrow
frontend execution port; it cannot downcast the backend, inspect scene/ECS,
allocate outside resource/residency owners, present independently or mutate host
settings. Where a native SDK requires backend command/resource access, the
selected backend supplies a private negotiated adapter implementing Horo's typed
provider operations.

Selection returns one immutable `ImagePipelinePlan` with category/provider/mode
IDs, actual render/output extents, scale, input/output contracts, capability,
driver, cook and configuration generations, budgets and applied fallback rules. Missing
required support fails before resource creation. Optional selection follows only
declared edges; it never downloads a runtime, switches backend, changes present
mode, disables an effect or chooses another category implicitly.

### 3. Reconstruction consumes one canonical real-frame contract

The frontend produces a `RealRenderFrame` with stable scene/view/simulation tick,
render-frame, GPU Scene, raster recipe, color pipeline, exposure and history
generations. A reconstruction request declares:

- render and target extents, finite scale and jitter sequence/sample;
- exposed or pre-exposed linear ACEScg scene color in the exact ADR-037 stage;
- positive linear view-space depth in meters plus projection/near/far metadata;
- normalized motion vectors where `previousUv - currentUv` excludes projection
  jitter, plus explicit current/previous jitter values;
- exposure/current-to-previous pre-exposure scales and reset/camera-cut flags;
- optional reactive, transparency, disocclusion and composition masks in finite
  normalized `[0, 1]`, each with declared producer/meaning; and
- output format, precision, target extent and graph access/lifetime requirements.

Adapters convert that canonical contract to private provider units/layouts. They
cannot reinterpret device depth, flip motion sign, include jitter twice, infer
meters from a projection matrix or invent a missing mask. Each selected provider
declares which optional inputs become required for its mode; missing required
inputs reject that candidate before execution.

Scene reconstruction executes before ADR-037's target output transform and before
display-referred UI. Its output remains exposed/pre-exposed linear ACEScg with the
declared color/exposure generation. Effects declare whether they execute at render
or target extent and graph dependencies order them around reconstruction; provider
selection does not silently move an effect. A future display-space scaler is a
different named mode with a separate color/UI/capture contract.

`ResolutionController` is separate frontend/product policy. It proposes a finite
render extent/scale at a frame boundary within provider, content, memory and raster
limits. Reconstruction accepts the resolved extent; it does not measure frame time
and change resolution itself. Scale/extent changes create a new plan/history
generation and follow the provider's declared resample-or-reset rule. No hidden
mid-frame dynamic-resolution mutation is allowed.

### 4. Histories are view-scoped and generation complete

Temporal reconstruction and denoising instances are scoped to view, provider,
mode, extent, raster/color/exposure/input-schema and device generations. History
resources use ADR-027/034 ownership, finite memory, graph access and retirement.
Provider-private history does not enter snapshots, saves or gameplay queries.

Every real frame records whether history was successfully consumed and published.
A failed/cancelled frame cannot advance it. Required reset/invalidation includes:

- first frame, camera cut/teleport or missing predecessor;
- view/surface/device/provider/mode/schema replacement;
- incompatible render/target extent, projection/jitter or color-plan change;
- GPU Scene origin/motion discontinuity or missing/mismatched motion/depth;
- long suspension, skipped real-frame sequence or explicitly invalid effect input;
- provider-reported recoverable reset requirement translated to a typed Horo
  reason.

Exposure/pre-exposure changes use the declared provider rescale contract or reset;
they are never guessed. A provider may support bounded history migration across a
compatible scale change, but stages a candidate and publishes atomically. Old
in-flight frames retain old history generations. Failure keeps the last good plan
only if it is still compatible; otherwise selection resolves an explicit fallback
or reports the required feature unavailable.

Denoising history belongs to one effect/view/input schema. A reflection denoiser
cannot reuse GI history or become the global reconstruction history. Effect owners
declare noisy signal, albedo/normal/depth/motion/exposure guides, sample count,
confidence/variance, history and output semantics plus the denoiser fallback.

### 5. Frame generation creates presentation frames, not engine frames

The frontend and presentation contract distinguish:

```text
SimulationTickId -> RealRenderFrameId -> RealPresentationFrameId
                                      \-> SyntheticPresentationFrameId(s)
```

A generated frame references exactly the qualified bracketing real-render/present
frames and the `FrameGenerationPlan` generation. It never receives a new
`SimulationTickId`, invokes render extraction, advances animation, physics, AI,
networking or audio, samples gameplay input, executes a real-frame render graph,
updates exposure/TAA/denoiser/material/VFX histories, increments real-frame jitter,
fires gameplay frame callbacks or satisfies capture/save readiness.

The first Horo frame-generation stage consumes reconstructed **display-linear
scene content before display-referred UI/accessibility/final transfer encoding**,
plus provider-required depth, motion, exposure, masks and pacing metadata from the
two real frames. It outputs a synthetic display-linear scene image. The frontend
then composes the latest qualified display-referred UI/accessibility state and
performs the target gamut/dither/encoding stage for that presentation frame. This
prevents interpolation of ordinary HUD/editor text. A provider that cannot support
this separation is unavailable for the baseline contract; integrated UI paths
need a separate named/qualified mode.

Synthetic frames are generation-tagged and observable in diagnostics/capture
metadata. Ordinary screenshots default to the latest real presentation frame;
capturing a synthetic frame is an explicit option and labels both source real
frames. Frame generation is suppressed—without error for an optional plan—on the
first frame, camera cut/reset, missing/dropped/failed predecessor, suspension,
resize/output/color/provider/device generation change, invalid motion/depth/mask,
late real frame or presentation backpressure. It resumes only after the provider's
required number of consecutive qualified real frames.

Generated frames acquire/present through ADR-033 surface tokens and cannot exceed
the resolved present-mode, refresh, queue-depth or frames-in-flight contract. They
do not invent display refresh or present independently. If a required generated-
frame plan cannot maintain its declared contract it returns a typed degraded or
unavailable result; optional policy presents real frames only and records why.

### 6. Latency policy is independent and truthful

Frame generation often adds interpolation history or queued real frames, so it is
never marketed or resolved as latency reduction. `LatencyProvider` exposes only
typed operations the selected native integration implements, such as CPU input,
simulation and render-submit markers, GPU work markers, bounded host wait/sleep hints,
present markers and measured timing correlation. Each operation has owner-thread,
frame-token and lifecycle requirements.

The application host owns desired latency budget and input policy. Platform/Input
owns event acquisition; simulation owns fixed-step consumption; RenderFrontend
owns render scheduling; ADR-033 owns frames in flight, pacing and presentation.
The latency provider can advise/execute a validated wait at a declared host safe
point and emit markers, but cannot pump input, change fixed timestep, reorder
networking/simulation, reduce queue depth below admitted resource safety, change
present mode, busy-wait on an owner thread or claim measured end-to-end latency
from GPU timing alone.

`ImagePipelinePlan` evaluates reconstruction, frame-generation, frames-in-flight,
present-mode and latency-provider requirements together. It records additional
real/synthetic buffering and the maximum allowed real-frame queue. A request that
cannot meet a hard latency budget fails or selects a declared no-frame-generation
and default-latency fallback. Measurements distinguish simulation-to-real-present,
simulation-to-synthetic-present, CPU, GPU and display-estimated segments with
explicit unknown fields; no synthetic cadence is reported as simulation FPS.

### 7. Scheduling, memory, failure and shutdown are bounded

Each provider request is a typed graph pass or host-safe-point operation over
generation-checked Horo resources. Providers declare queue, stage and access plus
input, output, history and scratch lifetimes and completion. Async compute is optional and used
only when effective queue/synchronization support and measured plan policy admit
it; a provider cannot assume concurrency or insert native barriers outside the
graph. Normal frames never wait for GPU idle or synchronous readback.

Finite budgets cover provider instances, queued CPU requests, persistent history,
per-frame inputs/outputs, scratch, masks, overlap during plan/history replacement,
generated frames/tokens and telemetry. Every byte remains charged through
in-flight retirement. Queue/capacity pressure returns typed backpressure or the
selected optional fallback; no provider allocates unbounded history, drops a real
frame while labeling it rendered, or silently lowers quality/scale.

Plan replacement stages provider instances/resources and publishes atomically at
a render safe point. Cancellation/stale completion destroys the candidate and
retains the last good compatible plan. Device loss invalidates provider-native
objects/tokens/history and re-resolves effective support before reconstruction.
No opaque SDK cache/handle is the only recovery source.

View/surface close stops admission, cancels CPU preparation, suppresses synthetic
frames, retires graph/presentation work, releases histories/resources and then
shuts provider instances down on their declared owner threads. Frontend/device
shutdown applies the same order and is idempotent after partial initialization.
Optional provider crash/exception is contained as a typed failure and resolves a
new fallback plan; required-provider failure marks output unavailable without
switching backend or process-global SDK state.

Diagnostics identify category/provider/version/mode, real/synthetic frame IDs,
view/surface/plan/history generations, actual extents/scale, required/missing
inputs/capabilities, reset/suppression/fallback reason, queue/budget usage and
timing provenance. They exclude native handles, image/history contents, user input
payloads and vendor telemetry by default. Provider network access, runtime download
or telemetry requires separate Delivery/Privacy policy and is absent from baseline
runtime execution.

## Rejected Alternatives

| Option | Decision and reason |
|---|---|
| One universal “upscaler” interface for reconstruction, denoising and frame generation | Rejected: inputs, histories, timelines, fallback and ownership differ materially. |
| Select providers by vendor/GPU/backend name | Rejected: reported hardware does not prove installed implementation, cooked variants or a complete input contract. |
| Let a provider control dynamic resolution and effect placement | Rejected: product/frontend policy owns extent and graph semantics; provider consumes a resolved request. |
| Feed native depth/motion/exposure conventions directly | Rejected: creates per-provider sign, jitter, units and generation errors. Use one canonical contract and private conversion. |
| Interpolate final HUD/editor UI by default | Rejected: produces text/input artifacts and stale interactive state. Generate scene content, then compose current qualified UI. |
| Count synthetic frames as render/simulation frames | Rejected: corrupts timing, history, gameplay callbacks and performance reporting. Use separate presentation identity. |
| Enable frame generation to reduce latency | Rejected: it may add buffering. Latency is a separately measured/resolved category. |
| Let latency SDK hooks pump input or change simulation timing | Rejected: violates Input/Simulation ownership and deterministic fixed-step ordering. |
| Silently fall back between vendor providers | Rejected: artifacts, quality and history differ. Follow an explicit ordered plan and report the selected route. |
| Require network download or runtime compiler on provider miss | Rejected: packaged execution uses verified installed components and cooked variants. |

The selected split requires more provider descriptors, adapters and plan state. It
prevents synthetic frames and vendor SDKs from becoming hidden engine clocks or
policy owners, and makes fallback/latency behavior testable.

## Migration And Verification

The current Native/TAAU/DLSS/FSR/XeSS table migrates to category-specific provider
IDs and modes. Existing settings are schema-versioned: known reconstruction choices
map to explicit reconstruction preferences; unknown/combined vendor modes require
editor migration and do not automatically enable frame generation or latency
hooks. The old unqualified “upscaler” API remains a diagnostic adapter only until
RND-016.2/.3 land, then is removed with caller migration.

| Delivery | Required implementation evidence |
|---|---|
| RND-016.2 / #423 | Canonical motion/depth/exposure/mask inputs, history identity/reset and shared reconstruction contracts. |
| RND-016.3 / #424 | Category registries, provider discovery/compatibility, license/package metadata, private adapters and lifecycle/error tests. |
| RND-016.4 / #425 | Horo-native spatial and TAAU fallback providers with image/history qualification. |
| RND-016.9 / #430 | Frame-generation real/synthetic timeline, UI separation, presentation/pacing integration and suppression tests. |
| RND-016.10 / #431 | Latency marker/wait contract, truthful measurements and interaction with frames-in-flight/present policy. |

Tests must cover:

- duplicate/invalid descriptors, installed versus implemented/effective support,
  exact selection/fallback order and no backend/vendor shortcut;
- canonical motion sign/units/jitter, linear depth, exposure/pre-exposure, masks,
  extent/scale/alignment bounds and missing required inputs;
- spatial and temporal reconstruction, first frame, reset, camera cut, resize,
  color, exposure, device and provider changes, history rescale/invalidation and
  failed-frame non-advance;
- effect-scoped denoiser isolation, mismatched guide/history schemas and declared
  denoised/non-denoised fallback;
- synthetic frame IDs/bracketing, no simulation/input/extraction/history/callback
  advancement, UI-after-generation ordering, capture labels and resume warm-up;
- missing/dropped/late real frames, backpressure, suspend/hotplug/resize/HDR change,
  present token/queue bounds and real-only fallback;
- latency markers/waits on correct owner phases, hard-budget rejection, no input
  pumping/tick change/busy wait and separate real/synthetic measurements;
- exact memory/queue limits, multi-frame replacement, cancellation, provider
  exception, view/surface close, device loss/recovery and repeated shutdown; and
- native image, cadence and latency fixtures for every advertised provider,
  backend and display route with stated tolerances. Null validates contracts and schedules,
  not provider quality, native timing or display cadence.

## Consequences

Reconstruction, denoising, frame generation and latency now have honest owners,
inputs, histories and fallbacks. Synthetic cadence cannot mutate simulation or be
reported as render FPS, and vendor integrations remain private replaceable
components. The cost is category-specific adapters, more generation/timeline state,
UI separation, stronger pacing integration and extensive native qualification.
This ADR selects no vendor provider and enables no new runtime feature by itself.
