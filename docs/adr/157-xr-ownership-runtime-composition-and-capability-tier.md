# ADR-157: XR Ownership, Runtime Composition and Capability Tier

- **Status**: Proposed
- **Date**: 2026-09-02
- **Supersedes**: None
- **Scope**: XR public/runtime module ownership, application composition, OpenXR/backend boundary, Renderer/Input/Platform integration, typed capabilities and admission, product profiles, unsupported paths, lifecycle, migration and 1.0 versus post-1.0 scope
- **Issue**: [XRA-001.1](https://github.com/abdullahbodur/horo-engine/issues/2109)
- **Jira**: [HORO-2063](https://horo-engine.atlassian.net/browse/HORO-2063)
- **Related**: [ADR-008](008-error-model-exception-boundary-and-registry.md), [ADR-010](010-job-waiting-and-operation-store-ownership.md), [ADR-018](018-command-registration-permissions-threading-and-packaged-build-policy.md), [ADR-026](026-large-world-precision-and-floating-origin-strategy.md), [ADR-027](027-renderer-resource-identity-and-descriptors.md), [ADR-028](028-renderer-capability-limits-and-product-profiles.md), [ADR-033](033-presentation-and-display-ownership.md), [ADR-054](054-extension-and-package-authority-boundary.md), [ADR-073](073-runtime-ui-ownership-scope-and-update-order.md), [ADR-078](078-runtime-ui-input-context-and-player-routing.md)
- **Normative documents**: [XR Architecture](../architecture/runtime/vr-ar-architecture.md), [System Design](../architecture/foundation/system-design.md), [Platform Abstraction](../architecture/foundation/platform-abstraction.md), [Android Platform Host](../architecture/foundation/android-platform-host.md), [Rendering Architecture](../architecture/runtime/rendering-architecture.md), [Input Architecture](../architecture/runtime/input-architecture.md), [Runtime Lifecycle](../architecture/runtime/runtime-lifecycle.md), [Post-Processing and Effects Architecture](../architecture/runtime/post-processing-and-effects-architecture.md), [Platform Services Architecture](../architecture/runtime/platform-services-architecture.md)

## Context

The XR architecture describes an OpenXR backend, runtime-driven views, tracking,
actions, presentation, interaction, mixed reality and qualification. It does not yet
ratify concrete module boundaries, one composition authority or a closed 1.0 capability
profile. Without that foundation, engine-facing code could expose OpenXR handles,
Platform could select/own the runtime, Renderer could own the XR session, Input could
poll native actions, or optional extensions could be enabled opportunistically after
session creation.

XR crosses multiple lifecycle owners. The OS host owns windows/activities/surfaces and
dynamic-library primitives; OpenXR owns native instance/system/session/action/space/
swapchain state; Renderer owns GPU work and resources; Input owns canonical gameplay
actions; Runtime UI/Interaction/Accessibility own their semantics. These must compose
without creating a dependency cycle or a second source of truth.

Existing documentation also places XR near Platform Services and post-processing in
navigation/UI contexts. Platform Services is the authenticated achievements/cloud/
presence transport subsystem and owns no headset/runtime state. Foveation, runtime
reprojection and XR composition are not generic post-process effects. This ADR makes
those deliberate non-owner boundaries normative.

The repository has no production OpenXR implementation today. This decision therefore
fixes the implementable ownership, admission and release contract; it does not claim
hardware support. XRA-001.2 through XRA-001.8 specialize typed identities, spaces,
session/frame lifecycle, negotiation, shutdown and qualification without changing the
owners below.

## Decision

### 1. XR is a runtime vertical slice with a narrow public API

Horo introduces these logical targets:

```text
HoroEngine::XRApi
HoroEngine::XRRuntime
HoroEngine::XROpenXR
```

`XRApi` owns backend-neutral fixed-schema values, strong identities/generations,
capability/profile descriptors, immutable snapshots, typed requests/results and narrow
frontend interfaces. It depends only on Foundation and the narrow Horo types required
by its public contract. It exposes no OpenXR headers, handles, extension enums/strings,
loader functions, platform-native objects, renderer-native resources or STL ownership
across a plugin/native boundary.

`XRRuntime` is the Horo frontend/coordinator. It owns engine-visible backend/runtime/
system/session generations, accepted capability plan, logical reference spaces, frame
operation state, bounded view/tracking snapshots, native-adapter command sequencing and
cross-owner readiness. It depends on `XRApi` plus narrow host-composed Horo ports. It
does not create/select a concrete backend, renderer, input system or platform host.

`XROpenXR` is the first concrete native adapter. It owns loader dispatch, OpenXR
instance/system/session/action sets/bindings/spaces/swapchains/composition layers,
enabled extension state, native event translation and API-valid destruction order. It
depends toward `XRApi`, Foundation and the required Platform/Renderer adapter contracts;
native types remain target-private.

### 2. The application host is the sole composition authority

Before creating an XR instance/session or presentation resources, the application host
selects one exact tuple of:

- XR frontend implementation and concrete native backend;
- platform host plus loader/surface/activity capabilities;
- renderer frontend/backend/device and external-image/synchronization capabilities;
- Input action router and interaction adapters;
- product-required/optional XR profile and privacy policy; and
- diagnostics/qualification policy.

Selection is explicit configuration/product policy, not runtime name probing inside a
feature module, executable-name checks, static registration order or service-locator
lookup. Descriptor creation/validation is inert: it performs no loader discovery,
instance creation, permission prompt, renderer allocation or global mutation.

The host transfers or lends typed owner references into `XRRuntime` and adapters, starts
them in dependency order and joins their readiness at declared safe points. A feature
module may request a capability but cannot instantiate OpenXR, switch the renderer or
silently fall back to a different runtime/backend/profile.

### 3. Every XR-adjacent state has one owner

| Responsibility | Sole owner | Deliberate non-owner |
|---|---|---|
| Product XR enablement, required profile, backend/renderer/platform selection | Application composition root | XR feature code and native runtime discovery |
| Engine-facing session/frame/space/view/capability identities and snapshots | XRRuntime | Renderer, Input, gameplay and editor panels |
| Loader, native instance/system/session/actions/spaces/swapchains/layers/extensions | XROpenXR | XRApi, Platform Services and Renderer |
| OS window/activity/surface, dynamic-library primitive, lifecycle and base permissions | Platform host | XROpenXR does not own the OS process/activity |
| GPU device/resources, render graph, per-view extraction, imported external images and retirement | Renderer | XRRuntime/OpenXR do not record general scene rendering |
| Canonical action state, context/focus routing, rebinding and gameplay consumption | Input | Input does not poll OpenXR or retain native paths/handles |
| World-space UI focus/navigation/layout/accessibility semantics | Runtime UI/Interaction/Accessibility | XR supplies evidence only |
| Gameplay locomotion, grab/use meaning and authoritative scene mutation | Character/Physics/gameplay owners | XR tracking/actions do not grant gameplay authority |
| Consent, product privacy policy and permission workflow | Application/privacy owner plus Platform permission primitive | XR backend cannot self-authorize sensitive features |
| Metrics/logs/diagnostic retention/export and release evidence | Observability/qualification owners | XR owns typed event production only |
| Achievements, cloud saves, presence, friends and platform accounts | Platform Services | Platform Services owns no XR lifecycle/capability/device state |

Owner notifications publish only after state commit. Native callbacks and runtime events
produce bounded adapter-owned records; they do not mutate Scene, Input, Renderer or
application policy directly.

### 4. Public identity and data are Horo-owned and generation-safe

XRA-001.2 will freeze exact encodings, but the public model must distinguish:

- stable backend/provider/profile/capability type IDs;
- runtime, system and session incarnations;
- reference-space, view-configuration, view, device and action identities;
- predicted-frame and acquired-target generations; and
- immutable capability, pose/action, view and frame snapshots.

Stable IDs, live handles and revisions are non-interchangeable. Every session-owned
handle includes `XRSessionGeneration`; every frame/image/view value includes its exact
frame and target generation. Zero, unknown, foreign, stale and wrong-owner values fail
before native use. Display labels, runtime strings, OpenXR paths, array indexes, product
names and native handles never substitute for Horo identity.

Snapshots are bounded immutable owner-backed values or leases. A lease preserves memory
validity but not logical currentness after focus/session/space/profile/capability loss.
Public spans cannot outlive their documented snapshot lease and expose no mutable or
native storage.

### 5. Capability discovery, policy and admission are separate

The native backend reports a bounded immutable discovered-capability snapshot containing
core/API versions, system/view configurations, formats, blend modes, tracking/action/
haptic features and available optional extensions. Discovery is evidence, not activation
or support.

The application resolves discovery with renderer/platform/Input capabilities, product
policy, permissions and finite limits into one `XRCapabilityPlan`. Each capability has
typed state such as `Required`, `Optional`, `Available`, `PermissionRequired`, `Denied`,
`DependencyMissing`, `Incompatible`, `TemporarilyUnavailable`, `Lost` or `DisabledByPolicy`.
Boolean flags and extension-name string comparisons are insufficient.

Admission validates the complete plan and creates one immutable capability generation
before native session/swapchain/action resources. A missing required capability fails
preflight with typed evidence and creates no partial session. Optional absence is
recorded and may select only a plan-declared alternative. It never becomes empty
success, an implicit downgrade or permission for another subsystem to emulate the
feature.

Runtime capability/focus/permission loss advances a revision or ends the affected
generation. Outstanding frames, poses, actions, haptics and sensitive work are
invalidated/neutralized through their owner boundaries; they are not kept current by a
physical memory lease.

### 6. Version 1 has two explicit product capability profiles

Horo 1.0 defines two profile identities. They are requirement sets, not ordered quality
levels or headset categories.

`XRProjection1_0` requires:

- one admitted opaque-environment primary stereo view configuration with exactly two
  runtime-driven views (no fixed public eye arrays);
- valid orientation and position head tracking with explicit invalid/lost states;
- `View` and `Local` reference spaces, with `Stage` used only when discovered/admitted;
- predicted display time and the wait/begin/locate/acquire/render/release/end frame
  lifecycle;
- runtime-owned external color targets imported through the Renderer contract;
- canonical boolean/float/vector2/pose action projection sufficient for a product-
  declared controller-neutral action set;
- focus/visibility/session/instance loss, cancellation and generation-safe teardown;
  and
- bounded diagnostics plus simulator/replay and physical-device qualification hooks.

`XRTrackedInteraction1_0` includes `XRProjection1_0` and additionally requires:

- two tracked controller roles with independent valid aim and grip poses;
- product-declared select/menu/trigger/squeeze/thumbstick actions and conflict-checked
  suggested bindings for each qualified interaction profile;
- bounded cancellable controller haptics;
- XR-to-Input projection plus Runtime UI ray/direct interaction adapters; and
- product comfort policy for seated/standing, handedness, snap/smooth turn and motion
  reduction without moving ownership out of Character/UI/Accessibility.

A product may ship `XRProjection1_0` without tracked interaction. A product requiring
`XRTrackedInteraction1_0` fails preflight if any required controller/action/haptic/
adapter capability is absent; it does not silently fall back to gaze-only or projection-
only behavior. Exact action schemas, spaces and frame contracts remain downstream ADR
work, but their owners and required profile meanings are fixed here.

### 7. Optional 1.0 features never redefine the admitted profile

The 1.0 implementation may negotiate these optional capabilities when explicitly
declared by product policy and qualified for the exact tuple:

- depth composition submission;
- fixed foveation that requires no eye-tracking data;
- refresh-rate selection among runtime-enumerated modes;
- visibility-mask optimization;
- controller model/presentation metadata; and
- Android standalone hosting when both the Android and XR tuples qualify.

Each optional capability has exact dependencies, lifecycle, fallback and evidence.
Failure disables only that capability when the base profile remains complete. The
selected frame/render plan identity records the result; Renderer/Input cannot infer it
independently.

An optional feature cannot change core pose/action semantics, fabricate tracking,
select an unrequested runtime/backend, replace a required profile or turn a native
extension into a public API. A provider promoted into a newer OpenXR core version still
maps to the same Horo capability ID with recorded provenance.

### 8. Mixed reality and advanced tracking/rendering are post-1.0

The following are post-1.0 optional capabilities unless a later roadmap revision
explicitly promotes and qualifies them without changing this ownership model:

- passthrough/camera access and non-opaque blend compositions;
- persistent spatial anchors, plane/mesh scene understanding and light estimation;
- eye-gaze input, eye-tracked foveation and gaze-derived UI/gameplay behavior;
- articulated hand-joint, body and face tracking;
- quad/multi-view and foveated-inset view configurations;
- runtime space warp/motion-vector layers and additional composition-layer families;
- cross-runtime anchor interchange and cloud/shared spatial localization; and
- vendor-specific native SDK features outside the admitted OpenXR adapter path.

The public API remains variable-view and capability-extensible so these additions need
not force an ownership migration. However, 1.0 profiles, acceptance tests and supported
device tuples do not depend on them, fake their data or count them as expected failures.
Unsupported requests return typed policy/capability results before sensitive access or
native allocation.

XRA-006 decisions may define post-1.0 mixed-reality privacy/ownership in more detail.
They cannot move camera/anchor consent to Renderer, Platform Services or post-processing,
and cannot let native callbacks mutate authoritative Scene/gameplay state.

### 9. Renderer receives views and external targets, not session ownership

XRRuntime publishes a bounded frame plan with predicted timing, located view set,
render extent/format/usage, target identities and required synchronization. Renderer
owns N-view extraction, render graph scheduling, GPU resources, external-image import,
scene passes, submission and deferred GPU retirement.

The XROpenXR/Renderer bridge is a concrete private adapter that translates native images
and synchronization into the Horo external-resource contract. Native handles never
cross the public XR/Renderer API or become resource identity. XR remains owner of
acquire/wait/release/composition submission state; Renderer cannot release/end a native
frame on its own.

Foveation is an XR/Renderer capability/plan, not a generic post-process effect. Runtime-
owned asynchronous reprojection/timewarp remains native runtime behavior, not a Horo
render pass. Post-processing consumes admitted per-view render inputs like other
Renderer passes and cannot discover the XR runtime or choose view configuration.

### 10. Input receives canonical action projections, not OpenXR ownership

XROpenXR owns native action sets, spaces, suggested bindings and synchronization.
XRRuntime translates native results into bounded session-generation-scoped Horo action/
pose/haptic capability snapshots. The XR-to-Input adapter publishes canonical values at
the declared Input phase.

Input owns action definitions, contexts, focus/capture, rebinding, neutralization and
gameplay/editor consumption. It does not call `xrSyncActions`, resolve native component
paths, retain `XrAction`/`XrSpace`, infer device product names or create an XR session.
Native interaction profile/path strings are bounded diagnostic/binding evidence, not
localized labels or public gameplay IDs.

Focus/tracking/profile/session loss produces explicit invalid/neutral values in the
same owner phase. A last valid pose/action is not silently held as current. Haptics are
typed Input/application requests routed through XR with device/session generation,
duration, amplitude/frequency bounds and cancellation; unsupported haptics do not imply
successful output.

### 11. Platform owns host primitives, not XR policy or native session state

Desktop Platform supplies bounded dynamic-library loading primitives, OS window/surface
integration, event-loop/thread facilities and native handle access only through private
host adapters. Android Platform owns activity/application lifecycle, native surface,
permission APIs, packaging/ABI, deployment, thermal and device diagnostics.

XROpenXR owns use of those primitives for its loader/instance/session. Platform does not
enumerate/select XR runtimes, enable extensions, choose reference spaces/view profiles,
poll actions or submit frames. A platform host may report that required primitives are
unavailable; the application capability plan decides the result.

Platform Services is unrelated: it owns account/achievement/cloud/presence transport
and cannot own XR runtime selection, session, permissions, device identity, anchors or
diagnostics. Platform account presence cannot grant XR or mixed-reality consent.

### 12. Lifecycle is coordinated and generation-fenced

The high-level owner lifecycle is:

```text
Uninstalled
  -> Installed (inert descriptors validated)
  -> Preflighted (backend/platform/renderer/input/profile plan accepted)
  -> Activating (loader/instance/system/session resources prepared)
  -> Ready
  -> Running / Visible / Focused (runtime-event driven)
  -> Stopping / Lost / Recovering
  -> Inactive
  -> Uninstalled
```

XRA-001.4/001.5 freeze exact session and predicted-frame state machines. This ADR fixes
the invariant that native runtime events drive legal transitions, while the XR frontend
publishes one generation-fenced Horo state snapshot. Application UI/gameplay cannot
force a native state or treat visibility/focus as a boolean session owner.

Activation prepares dependencies in order: Platform/loader, OpenXR instance/system,
Renderer device compatibility, accepted capabilities/actions/spaces, native session/
swapchains, Renderer external targets, then Input/UI adapters. Potentially failing work
occurs before the no-fail publication of the Ready generation. Partial activation rolls
back in reverse order.

Session replacement/loss invalidates all spaces/actions/views/frames/targets/snapshots
from the old generation. New resources prepare beside still-draining old resources;
matching runtime/system/product labels do not revalidate stale handles.

### 13. Non-XR and headless/server compositions are explicit

A normal non-XR product omits `XRRuntime` and XROpenXR entirely. It does not need an XR
loader, native libraries, permissions, frame work or a fake headset. Querying the absent
application capability returns typed `NotInstalled`/`DisabledByPolicy` through the
application surface.

Dedicated/headless servers do not install presentation/tracking XR. They consume
networked gameplay intentions through normal Input/gameplay authority and never trust a
client XR pose merely because it originated from a tracking runtime. Server builds have
no OpenXR loader/session/render work or hidden polling thread.

Deterministic simulator/replay is a test/tool adapter with explicit synthetic identity
and evidence status. It follows the same bounded state, generation, frame and shutdown
contracts but never qualifies a physical device/runtime tuple or presents itself as the
production null path.

### 14. Failure and unsupported paths are typed and non-mutating

Stable outcomes include loader absent/incompatible, runtime unavailable, system absent,
required profile unsupported, renderer/device mismatch, platform primitive missing,
permission required/denied, action/profile conflict, session/instance loss, stale
generation, invalid state/handle, capacity exceeded, timeout/cancelled and native
backend failure with bounded redacted evidence.

Preflight failure creates no instance/session/Renderer external target/Input projection.
Activation failure retires only candidates and preserves the prior complete generation
when recovery/replacement is possible. Runtime loss closes new frame/action admission,
neutralizes owner projections, drains/retires GPU/native work and publishes Lost; it
never silently switches backend/runtime/profile or falls back to non-XR rendering while
claiming the XR session is active.

Optional failure follows only the captured plan's declared alternative. Missing/denied
sensitive capability is not empty success. Errors cross boundaries as Horo typed values;
raw native text, paths, handles, serials and privacy-sensitive tracking data remain
bounded/redacted under Observability policy.

### 15. Shutdown destroys owners in reverse dependency order

Shutdown closes new application XR/session/frame/action/haptic admission and invalidates
the frontend generation. It stops Input/UI/gameplay projections, cancels haptics and
owned jobs, drains runtime events and resolves/abandons any begun frame only through
legal native transitions.

Renderer completes or retires queued work and external-image references before the
backend destroys swapchains. The backend then destroys spaces/actions/session/system-
scoped state, instance/debug providers and loader dispatch in API-valid order. Platform
surfaces/activity/library primitives outlive their native XR users. Completion/callback/
snapshot/module leases drain before owner destruction.

Workers are joined without holding owner locks. A deadline reports typed incomplete
shutdown and preserves reachable ownership; it never detaches callbacks, releases an
acquired image twice, submits an invalid frame or force-destroys resources still used by
the GPU/runtime.

### 16. Migration and contract coverage are explicit

Existing aspirational `PCG`-style/backend-named or two-eye examples do not constitute XR
API. Implementation must introduce `XRApi`/`XRRuntime`/`XROpenXR` boundaries rather than
placing native types in Platform, Renderer, Input or public engine headers. Current
architecture text that groups XR with Platform Services/post-processing becomes
navigation only; the normative ownership statements are updated with this ADR.

Required automated coverage includes:

- target dependency/public-header boundaries and absence of OpenXR/native types from
  `XRApi`;
- inert descriptor validation and explicit host composition without service discovery;
- owner-table enforcement, especially Renderer/Input/Platform/Platform Services
  deliberate non-owners;
- discovered versus required/optional/admitted/lost capability states and complete
  preflight rollback;
- `XRProjection1_0` and `XRTrackedInteraction1_0` exact admission plus required-feature
  failure and declared optional fallback;
- post-1.0 mixed-reality/advanced feature rejection under 1.0 profiles;
- non-XR/headless zero loader/thread/frame/resource work and explicit simulator status;
- session/space/action/view/frame/target generation staleness and replacement;
- activation failure at every boundary, runtime/session/permission/focus loss and
  typed/redacted failure propagation;
- renderer external-target and Input projection ownership with no native-handle leaks;
  and
- shutdown from every lifecycle/frame phase with no callback, acquired image, GPU/native
  resource, snapshot, adapter or module lease surviving its owner.

Deterministic test adapters and recorded traces cover state/order/fault behavior.
Physical runtime/device evidence is still required before any product tuple is declared
supported.

## Consequences

### Positive

- XR has one backend-neutral public/frontend authority and one private native OpenXR
  owner selected by the application host.
- Renderer, Input, Platform, Runtime UI/gameplay and Platform Services keep distinct,
  non-overlapping responsibilities.
- Required/optional capability admission is explicit and happens before partial session
  creation.
- Two implementable 1.0 profiles define baseline projection and tracked-controller
  interaction without coupling the API to a headset name.
- Mixed reality, advanced tracking/views and vendor features can evolve post-1.0 without
  blocking or weakening the core release.

### Negative

- XR requires adapter targets across Platform, Renderer and Input plus an application
  coordinator instead of direct OpenXR use.
- Products must declare exact capability profiles and supported tuples; loader/runtime
  discovery alone is not enough.
- `XRTrackedInteraction1_0` cannot silently degrade to projection-only when required.
- Hand tracking, eye tracking, passthrough/anchors, quad view and custom vendor features
  are not part of the 1.0 profile.

## Rejected Alternatives

### Expose OpenXR as the Horo public XR API

Rejected because native handles/extensions/lifetimes would leak across modules, couple
all consumers to one backend and make capability/version migration unsafe.

### Let Renderer own the XR session and frame lifecycle

Rejected because Renderer owns GPU work, not tracking/actions/reference spaces/runtime
events, and other consumers need XR snapshots without reverse dependencies.

### Let Input poll OpenXR directly

Rejected because it duplicates session/action ownership, leaks native paths and makes
focus/profile/session generation changes inconsistent with the XR frontend.

### Put XR under Platform or Platform Services

Rejected because Platform supplies OS primitives only and Platform Services owns
account/cloud/achievement transport; neither owns cross-platform XR semantics or native
session presentation.

### Implement foveation or reprojection as post-processing

Rejected because foveation is a negotiated XR/Renderer plan and asynchronous
reprojection belongs to the native runtime compositor, not the generic effect stack.

### Infer capability tier from headset/product name

Rejected because runtime, connection mode, OS, renderer/device, permissions, extensions
and interaction profile jointly determine support and can change independently.

### Include mixed reality and advanced tracking in the 1.0 baseline

Rejected because they add sensitive permissions, privacy, provider variability and
additional rendering/scene-lifecycle contracts that are independently post-1.0.

### Silently fall back to non-XR or a lower profile

Rejected because the product/session would no longer satisfy its declared interaction
and presentation contract; unsupported paths must remain typed and visible.
