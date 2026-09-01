# ADR-047: RenderDoc, PIX and Metal Capture Integration

- **Status**: Proposed
- **Date**: 2026-09-01
- **Supersedes**: None
- **Scope**: External graphics-capture availability, operations, triggers, adapters and artifacts
- **Issue**: [RND-017.7](https://github.com/abdullahbodur/horo-engine/issues/439)
- **Jira**: [HORO-439](https://horo-engine.atlassian.net/browse/HORO-439)
- **Companion decisions**: [ADR-041](041-backend-neutral-renderer-diagnostics-model.md), [ADR-042](042-cpu-gpu-timestamps-and-pipeline-statistics.md), [ADR-044](044-render-markers-and-debug-labels.md), [ADR-045](045-backend-validation-and-debug-layer-integration.md)
- **Normative documents**: [Platform Abstraction](../architecture/foundation/platform-abstraction.md), [Rendering Architecture](../architecture/runtime/rendering-architecture.md), [Render Backend Parity Contract](../architecture/runtime/render-backend-parity-contract.md), [Observability Architecture](../architecture/observability/observability.md)

## Context

RenderDoc, PIX and Metal GPU capture expose different control and artifact models.
A tool may launch the process, attach externally, inject a runtime, require capture
configuration before device creation or provide an in-process begin/end API with
strict thread affinity. Treating them as a universal “capture frame” button would
hide availability, silently select another tool, capture the wrong surface/frame
or leave an unbounded artifact in an arbitrary path.

Graphics captures are also unlike ordinary profiler traces. They may contain
resource bytes, shader code, pipeline state, object labels and rendered user
content. They therefore require explicit local developer intent, stricter privacy
and artifact handling. The tools must consume ADR-044 markers and ADR-041 context;
they must not own renderer policy, stores, fallback or UI.

## Decision

### 1. Application owns one typed capture operation

The application composition root creates a `GraphicsCaptureService` over the
selected renderer's narrow provider port, Platform filesystem/process services,
Observability context and the job/operation model. Editor, CLI and MCP surfaces
submit the same typed request and observe the same immutable operation snapshot.
They do not call native tool APIs, poll files or own capture state.

```cpp
struct GraphicsCaptureRequest {
    GraphicsCaptureProviderId provider;
    GraphicsCaptureMode mode;
    GraphicsCaptureTarget target;
    GraphicsCaptureTrigger trigger;
    GraphicsCaptureExtent extent;
    GraphicsCaptureArtifactPolicy artifact;
    GraphicsCaptureCorrelationPolicy correlation;
    ProductProfileId productProfile;
    RequestId request;
};
```

The provider is explicit (`renderdoc`, `pix` or `metal-capture`) after the host
presents available choices. There is no `auto` provider in a persisted request and
no fallback to another tool. `mode` is one advertised mode: `ExternalAttachment`,
`LaunchUnderTool`, or `InProcessControl`. A provider need not support every mode.
Mode changes that affect process/device creation return `ProcessRestartRequired`
with a launch plan; they do not mutate the running process.

Requests are allowed only in local Developer or Qualification profiles with an
authenticated diagnostics entitlement where applicable. Shipping and untrusted
remote clients reject them. A project, scene, asset, plugin, console script or
incoming network message cannot arm capture. MCP/CLI calls still require the same
local authorization and explicit operation request.

### 2. Discovery, installation, attachment and readiness are distinct

Each private adapter reports an immutable generation-scoped capability:

```cpp
struct GraphicsCaptureProviderCapability {
    GraphicsCaptureProviderId id;
    GraphicsCaptureProviderVersion version;
    GraphicsCaptureAvailability availability;
    GraphicsCaptureModeMask modes;
    GraphicsCaptureTriggerMask triggers;
    GraphicsCaptureLimitSet limits;
    RenderBackendId backend;
    std::optional<RendererGeneration> renderer;
    std::optional<DeviceGeneration> device;
};
```

Availability distinguishes `NotInstalled`, `Installed`, `LaunchRequired`,
`Attached`, `RuntimeAvailable`, `Ready`, `Busy`, `ProfileDisabled`,
`BackendUnsupported` and `RestartRequired`. Installed does not mean attached;
attached does not prove the selected backend/device can be captured; a marker or
validation capability does not imply capture readiness.

Pre-device discovery states such as `NotInstalled`, `Installed`, `LaunchRequired`
and `RestartRequired` carry explicit absent renderer/device generations. `Attached`,
`RuntimeAvailable` and `Ready` must carry the exact live generations they describe;
a missing or stale generation makes those states invalid. A launch plan is
provisional availability evidence, not a fabricated ready device capability.

Discovery uses reviewed platform/tool integration and bounded safe metadata. Horo
does not scan arbitrary DLL/dylib/shared-object paths, load a project-supplied
library, execute a shell command, download a tool or trust an environment-provided
binary. Launch uses `ProcessService` with an explicit executable identity,
argument vector, allowlisted environment and timeout. Tool installation and
licensing remain external developer-environment concerns.

Provider adapters are inert until explicitly composed. Descriptor validation,
availability queries and UI refreshes cannot attach, inject, launch, arm or create
files. Readiness is resolved again immediately before arming because attachment,
device and surface state may change.

### 3. Targets and triggers identify exact renderer work

`GraphicsCaptureTarget` names one renderer/device generation and either one
surface/view, one offscreen target, or the whole provider-supported device scope.
Absence is explicit. A stale, foreign, suspended or destroyed target fails before
arming. Multi-window and multi-GPU hosts never infer “current” from focus or tool
enumeration order.

Triggers are typed:

- `NextRealRenderFrame` after the request reaches the render safe point;
- an exact future `RealRenderFrameId` inside the admitted horizon;
- the next presentation of an exact surface generation;
- an exact registered ADR-044 marker boundary when the provider supports it; or
- `ExternalToolControlled`, where Horo observes correlation but does not claim to
  choose the native frame.

Synthetic presentation frames do not acquire a false GPU graph capture. A trigger
cannot refer to an already submitted frame, wall-clock delay, arbitrary marker
text or unregistered asset/entity label. Exact frame requests validate monotonic
horizon and wraparound. Marker triggers use registered stable IDs and graph
placement; display strings are never trigger identity.

The default extent is one real render frame. A bounded range may request 1–8 real
frames by default, up to a hard maximum of 120 when the provider and qualification
profile explicitly allow it. Capture begins and ends at provider-qualified safe
boundaries; it never splits a command encoder/list/buffer illegally or calls GPU
idle merely to align a trigger.

### 4. One explicit state machine owns arming and completion

An operation moves monotonically through:

```text
Queued -> Validating -> AwaitingRestart | Armed -> Capturing -> Finalizing
       -> Completed | Partial | Cancelled | Failed | TimedOut | StaleGeneration
```

`AwaitingRestart` is a terminal result for the current process and contains a
bounded user-approved launch plan, not a hidden relaunch. `Armed` records the exact
provider, target, frame/marker horizon, renderer/device/marker-plan generations and
artifact reservation. Only one operation may be `Armed`, `Capturing` or
`Finalizing` per device unless a provider capability explicitly proves independent
sessions; version 1 permits one globally per device.

Render-safe-point admission validates target state, provider readiness, profile,
ADR-044 marker plan, artifact capacity and required correlation. The renderer
owner invokes native begin/end only on the adapter's declared thread/context. A
worker may prepare manifests or finalize files but never captures borrowed native
objects or schedules work beyond their generation.

Provider callbacks enqueue bounded owned status into the operation; they do not
touch UI, logs, files or renderer policy directly. The operation is authoritative.
ADR-041 events record lifecycle/failure transitions once, while normal progress is
queried from operation snapshots rather than emitted per frame.

### 5. Provider differences stay private and declared

- RenderDoc support may observe an externally launched/attached runtime or use a
  reviewed compatible in-process control interface when already available. Horo
  never injects an arbitrary module or assumes API support from process module
  names alone.
- PIX support distinguishes launch/attachment tooling from any admitted D3D12
  programmatic GPU-capture facility. Windows SDK/PIX installation is not a player
  prerequisite, and capture cannot switch to WARP or another D3D backend.
- Metal capture resolves platform/Xcode tool availability and the supported
  `MTLCaptureManager`-style scope before arming. Launch/environment requirements
  are process-start policy; Horo does not rewrite them after Metal device creation.

Native handles, tool APIs, injected-module identity, command contexts and artifact
implementation details remain private. An adapter translates the common request
only when its capability advertises equivalent target, trigger and completion
semantics. Unsupported semantics return `CaptureModeUnsupported`,
`CaptureTriggerUnsupported` or `CaptureTargetUnsupported`, never approximation.

Tool UI may independently capture work through `ExternalToolControlled`. Horo
labels that provenance and may observe resulting artifacts only through an
explicit import/adoption operation. It never claims a Horo-triggered frame or
deletes/moves an externally owned file without authorization.

### 6. Markers and diagnostics correlate without owning capture

The service resolves a generation-scoped ADR-044 marker plan before arming.
Required native marker visibility must be active or the request fails. Optional
policy may declare logical-only correlation, but cannot claim native-tool marker
visibility. Capture activation never enables validation layers or mutates the
marker plan after frame admission.

The manifest records session/request, renderer/device/backend, real frame range,
surface/view/graph IDs when proven, marker-plan and schema revisions, provider
mode/version, effective capabilities and applied compatibility-policy revision.
It may reference ADR-042 profiler/timing session IDs, but external tool timing is
not substituted for Horo metrics without a separately qualified importer.

Failures map to registered ADR-041 codes with bounded native/tool evidence. A tool
message cannot choose fallback, complete the operation or identify a frame by text.
Native capture APIs do not become log sinks. Provider IDs, capture IDs and file
paths never become metric dimensions.

### 7. Artifacts are application-owned, atomic and private

Horo-owned captures are staged beneath the platform user-state diagnostics/capture
root using generated opaque directory and file names. Callers cannot provide an
arbitrary absolute output path. A later explicit export operation may copy a
completed artifact to a user-selected destination through validated Platform paths.

Every capture directory contains a versioned Horo manifest and at most the finite
provider artifacts declared by the operation. Publication uses same-filesystem
temporary files/directories, flush policy and atomic rename where supported. The
manifest records provider-reported completion, observed byte sizes/digests,
creation times, privacy class, partial/truncated state and tool-owned files that
could not be adopted. A manifest is published only after artifact finalization;
an unclean staging directory is recoverable/quarantined, never presented as a
complete capture.

Graphics captures are `RestrictedDeveloperData` by default because they may
contain resource contents, shader source, rendered user data, paths or labels.
The preflight confirmation states this explicitly. Files use user-private
permissions, are local by default, excluded from project/package/source control
and never auto-uploaded or placed in a normal support bundle. Preview/export is
explicit, allowlisted and warns that native tool payloads cannot be fully redacted.

The default reservation is 2 GiB and 60 seconds; configurable admission may lower
it. Hard ceilings are 16 GiB staged bytes, 120 captured frames and 10 minutes.
Checked free-space preflight reserves policy budget but cannot guarantee a native
tool's final size. If a tool exceeds the admitted bound, Horo requests stop once,
marks the operation `Partial` or `Failed`, quarantines the artifact and reports
actual size; it does not repeatedly delete growing files from a tool callback.

Retention defaults to the latest 8 completed captures and 16 GiB total, with hard
configurable maxima of 64 captures and 64 GiB. Cleanup runs outside frame work,
uses explicit oldest-completed ordering and never removes active, exporting,
pinned or externally owned artifacts. Capacity failure rejects a new request
before arming rather than deleting an unrelated active/user-pinned capture.

### 8. Cancellation, failure and shutdown are bounded

Cancellation before `Capturing` releases reservations and removes owned staging.
During capture it requests one provider stop at the next legal boundary. If the
provider cannot abort safely, status becomes `Finalizing` with cancellation
requested and then `Cancelled` or `Partial` after native completion; Horo does not
destroy in-use device/tool state to force cancellation.

Timeout has separate arm, capture and finalization deadlines. It closes admission,
requests stop once and preserves recoverable evidence. Failure never switches tool,
backend/device, target or trigger; never changes render quality; and never treats a
possibly incomplete file as success. Errors distinguish unavailable, unauthorized,
busy, stale target, restart required, unsupported mode/trigger, capacity, begin,
end, tool disconnect, timeout, finalization and publication failure.

Device loss or backend replacement marks the operation with its source generation,
requests provider stop when safe and prevents old callbacks/artifacts from entering
the new device generation. Recreation does not automatically re-arm. Submitted GPU
work follows normal retirement; capture shutdown adds no unconditional GPU-idle
wait.

Process shutdown stops new requests, cancels queued/armed operations, performs one
bounded stop/finalization attempt for active capture, detaches callbacks on owner
threads, quarantines incomplete staging and then releases provider state before
renderer/platform teardown. Shutdown is idempotent after every partial stage.
Observability/UI/store consumers cannot keep the renderer alive.

### 9. Parity means common semantics, not universal tool support

OpenGL, Vulkan, D3D12 and Metal expose provider capabilities through the same
request/state/error/artifact contract. Tool/backend combinations differ and may be
unavailable. A backend cannot fabricate parity by silently selecting another tool
or trigger. Null reports no native provider and deterministically validates request,
state machine, bounds, authorization, stale generations, artifact manifests and
failure injection with synthetic fixtures.

Native qualification proves exact tool/provider/backend/OS versions, launch and
attachment modes, target/frame accuracy, marker visibility, thread affinity,
cancellation, device loss, artifact readability and overhead. Null and mocked
files cannot prove capture correctness or native-tool compatibility.

## Migration And Verification

Existing ad hoc capture hotkeys, environment toggles or direct tool calls migrate
to `GraphicsCaptureService`. Existing marker integrations remain ADR-044 metadata;
they do not launch tools. Tool-generated files outside Horo ownership remain
external until explicitly adopted and are not silently moved into retention.

Tests must cover:

- provider discovery states, exact mode/backend availability and no tool fallback;
- authorization/profile denial, local CLI/MCP parity and no content/plugin arming;
- exact multi-surface/multi-GPU/frame/marker targets, synthetic-frame rejection,
  stale generations and trigger horizon boundaries;
- every state transition, one-active-session admission, callbacks, cancellation
  before/during capture, timeout, disconnect, device loss and repeated shutdown;
- default/hard frame, time, byte, count and retention limits with checked capacity;
- atomic publication, crash recovery/quarantine, permissions, pin/export races and
  externally owned artifact behavior;
- restricted-data confirmation, no automatic upload/support-bundle inclusion and
  bounded safe diagnostics;
- deterministic Null manifests/failure fixtures; and
- RenderDoc, PIX and Metal native artifact readability, marker/frame correlation,
  thread affinity and measured overhead on qualified configurations.

## Consequences

Capture becomes an explicit, inspectable application operation with exact provider,
target, trigger, generation and artifact provenance. Tool differences remain
honest, capture cannot silently change rendering, and sensitive artifacts receive
bounded private lifecycle ownership. The cost is one adapter per supported tool
mode, restart-aware launch flows, substantial native qualification and explicit
storage/privacy UX.

## Rejected Alternatives

| Option | Decision and reason |
|---|---|
| One generic “capture next frame” native call | Rejected: tools differ in attachment, creation-time requirements, targets and completion. |
| Auto-select any installed tool | Rejected: changes semantics and provenance silently. |
| Let editor panels call tool APIs directly | Rejected: UI would own renderer affinity, operation state and artifacts. |
| Permit arbitrary output paths | Rejected: bypasses structured paths, privacy, atomic publication and retention. |
| Treat graphics captures like normal profiler traces | Rejected: native payloads can contain complete resource/shader/user content. |
| Capture continuously in developer builds | Rejected: unbounded cost/storage and behavior distortion require explicit sessions. |
| Force device destruction to cancel | Rejected: risks corruption/loss and violates renderer lifecycle ownership. |
| Upload or bundle captures automatically | Rejected: opaque restricted native payloads require explicit preview/export. |
| Treat Null fixtures as tool qualification | Rejected: they cannot prove native frame accuracy, readability or overhead. |
