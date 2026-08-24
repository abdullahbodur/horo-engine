# XR Architecture

## Purpose

This document defines Horo Engine's virtual reality, augmented reality, and
mixed-reality architecture. It owns the engine-facing XR contracts for runtime
composition, session lifecycle, views, tracking, input, presentation,
interaction, diagnostics, and qualification.

OpenXR is the first runtime backend. It is not the public engine API, and OpenXR
handles or extension types do not cross Horo-owned subsystem boundaries.

## Current Status

The repository does not yet contain a production OpenXR backend. The contracts
below describe the target architecture. A runtime, headset, or platform is not
supported merely because it appears in a design example or because an OpenXR
loader can discover it. Support requires reproducible qualification evidence.

## Ownership

```text
application composition
  selects XR backend, renderer backend, platform host, and project policy
          |
          v
XR runtime frontend
  capabilities, sessions, spaces, frame lifecycle, bounded snapshots
          |
          +------> OpenXR backend
          |          loader, instance, system, session, actions, swapchains
          |
          +------> Renderer
          |          external images, N-view extraction, passes, synchronization
          |
          +------> Input
          |          canonical actions and neutralized device state
          |
          +------> Runtime UI / Interaction / Accessibility
          |          world-space UI, locomotion, comfort, focus
          |
          +------> Editor diagnostics
                     setup, inspection, capture, qualification evidence
```

Required boundaries:

- The composition root selects the concrete XR backend before session and
  presentation resources are created.
- The XR frontend owns engine-visible session, space, frame, capability, and
  device identities.
- The OpenXR backend owns native loader, instance, system, session, action,
  swapchain, composition-layer, and extension state.
- Renderer owns rendering work and GPU resources after a runtime image is
  imported through a typed external-resource contract.
- Input owns canonical game/editor action projection. It does not poll OpenXR.
- Runtime UI and interaction consume XR rays, touches, poses, and actions through
  adapters; they do not become a second XR source of truth.
- The XR runtime owns neither asynchronous reprojection nor platform boundary
  systems such as guardian/chaperone. Those remain runtime/platform behavior.
- Editor panels are query and command clients. They do not own the active XR
  session or directly call native runtime APIs.

## Capability And Identity Model

Capabilities are discovered from the selected runtime, platform host, renderer,
device, permissions, and admitted optional extensions. They are not inferred
from a headset product name.

```cpp
struct XRRuntimeIdentity {
    XRRuntimeId runtime;
    SemanticVersion runtimeVersion;
    XRBackendVersion backendContract;
};

struct XRCapabilitySnapshot {
    XRCapabilityRevision revision;
    XRSessionGeneration sessionGeneration;
    std::span<const XRViewConfigurationDescriptor> viewConfigurations;
    std::span<const XRInteractionProfileDescriptor> interactionProfiles;
    std::span<const XRExtensionCapability> extensions;
    XRTrackingCapabilities tracking;
    XRPresentationCapabilities presentation;
    XRMixedRealityCapabilities mixedReality;
};
```

Snapshots are owner-backed, immutable for their documented lifetime, and
bounded. A capability can be `Unavailable`, `Available`, `PermissionRequired`,
`Denied`, `TemporarilyUnavailable`, or `Lost`. Boolean feature flags are not
sufficient for permission-sensitive or replaceable capabilities.

Runtime discovery, project policy, feature admission, activation, and current
availability are distinct states. Horo never silently selects another runtime,
renderer, view configuration, interaction profile, or privacy-sensitive feature.

## OpenXR Backend Lifecycle

The backend follows explicit state transitions:

```text
Unconfigured
  -> LoaderAvailable
  -> InstanceCreated
  -> SystemSelected
  -> SessionCreated
  -> Ready
  -> Running
  -> Stopping
  -> Idle / Lost
  -> Destroyed
```

Runtime events drive session transitions. Duplicate, delayed, focus-loss,
visibility-loss, instance-loss, and session-loss events are normal inputs. Every
session-owned handle carries a generation so stale actions, spaces, swapchain
images, and snapshots fail before native use.

Frame submission follows the runtime's predicted timing contract:

```text
poll runtime events
  -> wait frame
  -> begin frame
  -> locate admitted views/spaces at predicted display time
  -> acquire and wait for required images
  -> render admitted views
  -> release images
  -> submit composition layers / end frame
```

Cancellation and shutdown finish or abandon the current frame only through
runtime-valid transitions. The host stops producers before destroying session,
instance, renderer, and platform dependencies.

## Extension Negotiation And Graceful Degradation

Optional OpenXR features use a typed negotiation record:

```cpp
struct XRExtensionAdmission {
    XRExtensionId id;
    XRExtensionVersion discoveredVersion;
    XRExtensionAdmissionState state;
    std::span<const XRExtensionId> dependencies;
    std::optional<XRCoreVersion> promotedToCore;
    XRCapabilityProviderId provider;
};
```

The backend records discovered, requested, enabled, rejected, dependency-missing,
and promoted-to-core states. Providers are registered only after validation and
are invalidated when their backend/session generation ends. Unsupported optional
extensions disable the associated capability with an actionable reason; they do
not prevent a baseline session unless the active project capability profile
requires them.

## Views And Coordinate Spaces

Horo uses metres, a documented handedness, and explicit local/stage/view space
identities. Poses include validity, tracking confidence, timestamp, source space,
and session generation. Invalid orientation or position components are not
replaced with fabricated identity values.

View count is runtime-driven. Public and renderer-facing contracts must not use
fixed two-element eye arrays:

```cpp
struct XRViewDescriptor {
    XRViewId id;
    XRViewRole role;
    XRPose pose;
    XRProjection projection;
    Extent2D recommendedExtent;
    XRSwapchainTargetId colorTarget;
    std::optional<XRSwapchainTargetId> depthTarget;
};

struct XRLocatedViewSet {
    XRViewConfigurationId configuration;
    XRSessionGeneration sessionGeneration;
    XRTime predictedDisplayTime;
    std::span<const XRViewDescriptor> views;
};
```

The contract supports a bounded variable number of views. The first production
slice may admit only primary stereo. A runtime configuration with more views is
then reported as unsupported before image acquisition; it is never truncated to
two views. Quad-view and foveated-inset execution can be added later without a
public contract migration.

Reference-space changes, recentering, bounds changes, and world-origin rebasing
are translated through an explicit adapter. Runtime tracking state never mutates
authoritative gameplay transforms from a native callback.

## Rendering And Presentation

The OpenXR backend supplies view descriptors and generation-safe runtime-owned
image identities. Renderer imports those images into the render graph with
declared format, usage, extent, array/view index, synchronization, and lifetime.
Native image handles remain private to the concrete XR/renderer adapter.

The initial implementation may render stereo as multi-pass or instanced
multiview according to backend capability. Instanced rendering is an
optimization, not a correctness requirement and not a claim that draw calls are
always halved.

The external-target contract anticipates:

- runtime-selected color and depth formats;
- per-view or array-backed image layouts;
- variable render extents and dynamic-resolution history invalidation;
- fixed foveation, variable-rate shading, and density-map capabilities;
- optional gaze-driven foveation after consent and valid eye tracking;
- motion/depth/timing inputs required by admitted runtime space-warp features;
- frames in flight, deferred destruction, loss, and swapchain recreation.

Foveation is a renderer/backend capability, not a generic post-process effect.
Horo does not emulate runtime-owned asynchronous timewarp or reprojection. When
an optional feature is unavailable, ordinary projection-layer rendering remains
the explicit fallback.

Normal frames do not use device-idle waits. Image acquire, wait, import, render,
release, and composition submission maintain explicit ownership and
synchronization. Session or surface loss retires resources only after queued GPU
references are safe.

## Tracking, Actions, And Haptics

OpenXR action sets and suggested bindings are backend data. Engine systems
consume canonical Horo actions and bounded tracking snapshots:

```cpp
struct XRTrackedPose {
    XRDeviceId device;
    XRPoseKind kind;
    WorldTransform pose;
    LinearVelocity linearVelocity;
    AngularVelocity angularVelocity;
    XRTrackingValidity validity;
    XRTime sampleTime;
};

struct XRHandState {
    XRHand hand;
    XRTrackingValidity validity;
    std::span<const XRHandJoint> joints;
};
```

Frame-hot hand data uses fixed-capacity or owner-backed storage; it does not
allocate a `std::vector` per hand per frame.

Suggested-binding data has its own schema version. Resolution records the active
interaction profile, profile changes, generic fallback, conflicts, and user
override migration. A runtime path such as an OpenXR component path never
becomes a localized UI label or a public gameplay identifier.

Focus loss, tracking loss, disconnect, profile replacement, permission
revocation, and session replacement neutralize affected actions immediately.
Haptic requests have explicit owner, duration, cancellation, device generation,
and capability fallback.

## Interaction, Runtime UI, And Comfort

XR interaction adapts ray, direct-touch, proximity, grab, and spatial hit
evidence into existing runtime UI and gameplay contracts. It does not duplicate
physics queries, UI focus/capture, or scene authority.

World-space canvases remain runtime UI documents projected into world space.
They use the same semantic focus, navigation, accessibility, localization, and
input-action model as non-XR UI. Stereo presentation and occlusion belong to the
renderer/UI projection adapter.

Locomotion produces intent for the character-movement owner. Teleport, smooth
move, snap turn, arm-swing, and room-scale behavior are policy/configuration, not
hard-coded controller logic. Comfort settings include turning, vignette,
movement, height, seated/standing, handedness, boundary awareness, and reduced
motion where supported.

Refresh rate, view extent, and frame budget come from runtime/device capability
snapshots. Architecture does not prescribe a universal 90 Hz minimum. Each
qualified tuple records its supported modes and measured comfort/performance
evidence.

## Mixed Reality And Privacy

Passthrough, anchors, plane/mesh understanding, hit testing, and light estimation
are optional capabilities with explicit consent, permission, lifetime, and
failure states.

- Raw camera frames do not enter ordinary engine logs, captures, or public APIs.
- Eye gaze, continuous poses, environment geometry, voice, and spatial anchors
  are privacy-sensitive diagnostic inputs.
- Permission revocation removes derived capability state and invalidates
  outstanding work.
- Persistent anchors identify localization state and failure; they do not imply
  that every runtime supports cross-session restore.
- Renderer consumes admitted passthrough/composition descriptors without owning
  camera permission or environment understanding.

## Android And Standalone XR

Standalone Android-class headsets require the Android runtime host in addition
to XR implementation. Android activity lifecycle, native surfaces, permissions,
storage, ABI packaging, deployment, thermal state, and device diagnostics are a
parallel critical path.

```text
Android runtime host --------+
                             +--> standalone device qualification
XR runtime/render/input -----+
```

PC streaming or tethered evidence does not qualify the standalone path. Each
connection mode is a separate qualification tuple.

## Immersive AI Authoring Boundary

Immersive AI authoring is owned by the Editor AI system, not XR. XR contributes
timestamped poses, rays, selections, interaction events, and session identity.
Audio contributes an admitted timestamped capture stream. A provider-neutral
speech service contributes transcript hypotheses. AIA aligns this evidence and
invokes approved MCP/editor capabilities.

Editor-owned authoring proxies and real gameplay objects may both provide
spatial evidence. AIA does not own grabbing, throwing, contact generation,
physics, or the gameplay meaning of those objects.

Voice, gaze, pointing, contact, or a final transcript never constitutes
approval. Agent-proposed scene mutations require a visible preview and an
explicit approval bound to the proposal/document revision. Application uses the
editor command/transaction owner so a successful proposal is one coherent undo
entry and failures cannot partially mutate the scene.

## Diagnostics And Qualification

Setup and diagnostics consume the same typed runtime snapshots as production
code. They show loader/runtime discovery, system/session state, view
configuration, interaction profile, optional extensions, permissions,
swapchains, frame timing, tracking confidence, and redacted failure details.

Support evidence is keyed by the full tuple:

```text
headset model
+ runtime and version
+ operating system/platform host
+ renderer backend and driver/device class
+ connection mode
+ interaction profile
+ admitted extension/capability set
```

Qualification categories describe planning intent rather than product-name
guarantees:

| Category | Meaning |
|---|---|
| Reference | Primary regularly tested development tuple |
| Compatibility | Additional or constrained tuple tested for compatibility |
| Enterprise | Hardware/access-dependent professional target |
| Conditional | Requires an external runtime, licensed SDK, or platform target |
| Future | Deliberately outside the current release commitment |

Every result is `Passed`, `PassedWithLimitations`, `Failed`, `Blocked`, or
`NotRun`, with evidence provenance and known issues. Simulator and replay results
are never reported as physical-device qualification. Refresh rates and render
extents are recorded from runtime enumeration, not a static headset table.

## Performance, Concurrency, And Shutdown

- Frame-hot snapshots use bounded owner-backed storage and avoid heap allocation,
  blocking I/O, logging-string formatting, and contended locks.
- Runtime event polling and frame submission occur on their declared owner
  threads. Native callbacks publish bounded state and do not mutate scene data.
- Diagnostics observe revisions and snapshots at their own cadence; they never
  stall frame production.
- Extension providers, action/profile state, spaces, images, and captures are
  invalidated by session generation.
- Shutdown stops new frame/input work, cancels owned jobs, drains valid runtime
  transitions, retires GPU resources, and destroys dependencies in reverse order.

## Validation

Required automated coverage includes:

- loader absent, runtime unavailable, unsupported system, and permission denial;
- session lifecycle, focus/visibility loss, instance/session loss and recovery;
- one-view, stereo, unsupported greater-than-two, and recorded variable-view data;
- extension dependency, promoted-to-core, rejection and provider invalidation;
- action/profile changes, tracking loss, hand-joint bounds, and haptic cancel;
- image acquire/wait/import/release, resize, loss and frames-in-flight teardown;
- fixed foveation/dynamic resolution fallback and history invalidation;
- deterministic simulator, capture/replay and fault injection;
- sensitive-data redaction and diagnostic bundle policy.

Physical-device evidence is required for every declared supported tuple. Missing
hardware is reported as `NotRun`; deterministic fakes do not silently satisfy a
device release gate.

## Related Documents

- [XR Setup UI Reference](./xr-setup.html)
- [Rendering Architecture](./rendering-architecture.md)
- [Render Backend Parity Contract](./render-backend-parity-contract.md)
- [Input Architecture](./input-architecture.md)
- [Physics Architecture](./physics-architecture.md)
- [Game UI And HUD](./game-ui-and-hud.md)
- [Accessibility Architecture](./accessibility-architecture.md)
- [Audio Architecture](./audio-architecture.md)
- [Runtime Lifecycle](./runtime-lifecycle.md)
- [Platform Abstraction](../foundation/platform-abstraction.md)
- [Android Platform Host](../foundation/android-platform-host.md)
- [Editor AI Agent Architecture](../editor/editor-ai-agent-architecture.md)
- [Application Security](../security/application-security.md)
