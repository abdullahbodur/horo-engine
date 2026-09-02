# ADR-159: XR Action, Tracking and Input-Projection Ownership

- **Status**: Proposed
- **Date**: 2026-09-02
- **Supersedes**: None
- **Scope**: XR action schemas and native action sets, tracking/device/pose snapshots, interaction-profile bindings, Input projection, fixed-tick consumption, gesture derivation, haptic requests, privacy-sensitive hand/eye data, lifecycle, migration and validation
- **Issue**: [XRA-003.1](https://github.com/abdullahbodur/horo-engine/issues/2128)
- **Jira**: [HORO-2082](https://horo-engine.atlassian.net/browse/HORO-2082)
- **Related**: [ADR-008](008-error-model-exception-boundary-and-registry.md), [ADR-026](026-large-world-precision-and-floating-origin-strategy.md), [ADR-073](073-runtime-ui-ownership-scope-and-update-order.md), [ADR-078](078-runtime-ui-input-context-and-player-routing.md), [ADR-135](135-platform-identity-session-generation-privacy-and-consent.md), [ADR-157](157-xr-ownership-runtime-composition-and-capability-tier.md), [ADR-158](158-openxr-loader-backend-packaging-and-host-composition.md)
- **Normative documents**: [XR Architecture](../architecture/runtime/vr-ar-architecture.md), [Input Architecture](../architecture/runtime/input-architecture.md), [Runtime Lifecycle](../architecture/runtime/runtime-lifecycle.md), [Application Security](../architecture/security/application-security.md)

## Context

ADR-157 assigns native OpenXR actions, spaces and tracking to XROpenXR, Horo-visible
XR state to XRRuntime and canonical action routing to Input. Existing architecture also
requires immutable snapshots, neutralization on loss and fixed-tick input frames. It
does not yet define the exact ownership handoff between product action schemas, native
action sets, live tracking, Input contexts, fixed simulation, gestures and haptics.

Without a single policy, Input could poll OpenXR directly, gameplay could read mutable
latest poses during a fixed tick, Renderer-late poses could overwrite simulation input,
native interaction paths could become durable gameplay identity, or gestures could
mutate gameplay from runtime callbacks. Hand joints and eye gaze add a second risk: a
feature being discoverable does not grant collection, retention, diagnostics, UI focus
or approval authority.

This ADR fixes the owners, typed handoffs, sample domains, loss behavior and privacy
boundary. XRA-003.2 through XRA-003.8 may freeze exact binary schemas, binding catalogs,
interaction-profile rules, haptic scheduling and test harnesses without moving the
authorities established here.

## Decision

### 1. Native XR input and canonical Input remain separate layers

The data path is:

```text
product action/profile declarations
            |
            v
Input canonical action registry + XR product requirements
            |
            v
XROpenXR native action sets/actions/spaces/suggested bindings
            |
            v
XRRuntime bounded tracking/action snapshot
            |
            v
XR-to-Input projection adapter
            |
            v
Input contexts, player/device assignment and immutable InputSnapshot
            |
            +-- Runtime UI / Interaction
            +-- gameplay tick-assigned InputFrame
```

OpenXR is a physical/runtime input provider, not Input's public model. `XRApi` owns the
backend-neutral XR IDs, snapshot values and narrow command/query ports. `XRRuntime`
owns Horo-visible XR device/session/sample generations and snapshot publication.
`XROpenXR` owns every native action set, action, action space, path, interaction-profile
query, sync/locate call and haptic call. Input owns canonical semantic actions, binding
resolution, context/focus/capture, player assignment, transition consumption,
neutralization and simulation-frame projection.

No native callback, event-poll function, Renderer pass, Runtime UI widget, gesture
recognizer or gameplay system may publish directly into another owner's mutable state.

### 2. Every action, tracking and haptic responsibility has one owner

| Responsibility | Sole owner | Deliberate non-owner |
|---|---|---|
| Product gameplay/editor action declarations and required XR profile | Project/application policy | OpenXR runtime |
| Canonical action IDs, value types, contexts, user/player assignment and consumption | Input | XROpenXR and gameplay systems |
| Horo XR device/role/control/pose/gesture capability IDs | XRApi registries | Native paths and display labels |
| Native action sets/actions/subaction paths/action spaces/suggested bindings | XROpenXR | Input and XRRuntime frontend |
| Native action synchronization, space location and result normalization | XROpenXR | Platform Input collector |
| Session-generation-scoped XR action/tracking snapshot and publication revision | XRRuntime | Renderer and Input |
| XR snapshot to canonical Input projection | Host-composed XR/Input adapter | Native callbacks and gameplay |
| Tick assignment, edge consumption, replay/network command form | Input/runtime simulation boundary | Live XR queries |
| World-space UI focus, ray/direct interaction and capture semantics | Runtime UI/Interaction | XR tracking source |
| Gameplay meaning, locomotion, grab/use and authoritative transforms | Gameplay/Character/Physics owners | XRRuntime and gesture recognizers |
| Haptic request admission, scope and cancellation | Input/application haptic coordinator | Native backend callbacks |
| Native haptic apply/stop and runtime capability result | XROpenXR | Input core implementation |
| Consent purpose, privacy class, retention/export and child/region policy | Application Security/privacy owner | XRRuntime, Input and diagnostics |
| OS permission request/state primitive | Platform | OpenXR capability discovery |
| Presentation-time view/head pose and visual late update | XR/Renderer frame bridge | Fixed simulation input |

Device identity, local player, Input user, interaction profile, physical control,
tracked role and semantic action are distinct typed identities. A runtime path, array
index, controller product name, hand label, display string or native handle cannot
substitute for any of them.

### 3. Product actions are Horo schema; native action sets are backend realizations

The application validates a finite immutable action plan before native action creation.
It joins:

- registered Horo `ActionId`, value type and Input context;
- product-required or optional XR action capability;
- permitted tracked roles and physical-control classes;
- finite interaction-profile binding catalogs supplied by XROpenXR;
- user overrides owned and persisted by Input;
- conflict, fallback and unbound-required policy; and
- the accepted XR product-profile and privacy plan.

XROpenXR compiles the accepted plan into native action sets, actions, subaction paths,
spaces and suggested bindings. Native names/paths remain backend-private registered
data. Input persists Horo action and registered physical-control identities plus schema
versions, not arbitrary runtime paths. Presentation obtains glyphs/labels through the
existing provider boundary; display text is not binding identity.

Native action sets do not mirror Input contexts one-for-one. Input contexts may be
pushed, focused or consumed every frame, while native action-set creation/attachment is
session lifecycle. The accepted plan defines which native sets XROpenXR activates for
the current sample; Input remains the authority for which projected semantic action can
reach GUI, Runtime UI, editor or gameplay.

Missing required controls, value-type mismatch, duplicate binding, ambiguous role,
unsupported interaction profile or conflicting user override fails plan admission
before a Ready session. Optional absence follows only a declared fallback. Runtime-
selected profile changes produce a new binding revision and neutral boundary; they do
not rewrite project schema or silently migrate incompatible overrides.

### 4. Tracking snapshots are immutable, bounded and generation-safe

Each published `XRTrackingSnapshot` records at least:

- XR backend, runtime and session generation;
- sample sequence, source clock domain and sample/predicted time evidence;
- reference-space identity/generation and world-origin revision;
- bounded device records with device generation, tracked role and capability set;
- bounded pose samples for declared pose kinds;
- bounded action values/transitions and active binding/profile revision; and
- focus, visibility, tracking, permission and loss state.

A pose records position and orientation validity independently, optional linear/angular
velocity validity, source space, target space, timestamp, confidence provenance and
device/session generations. Invalid components are explicitly absent/invalid; they are
not replaced by identity transforms, zeros, the last valid sample or inferred confidence.
Confidence is a finite Horo state derived from documented runtime evidence, not an
invented floating-point probability.

Snapshots use fixed-capacity or owner-backed immutable storage with explicit maximum
devices, poses, actions, joints and transitions. A lease preserves memory only; focus,
permission, tracking, profile, reference-space, origin, device or session revision can
make its values logically stale. Consumers reject wrong-owner and stale generations
before use.

Native input beyond any admitted device/pose/action/joint/transition bound rejects the
entire affected record with `XRTrackingCapacityExceeded`; it is never truncated,
priority-sampled or written past capacity because omission could change gesture or gaze
meaning. Other independently bounded records may still publish, while a required record
overflow invalidates the candidate snapshot under its product profile.

Head/view poses located for predicted presentation are a separate XR frame snapshot.
Renderer may use a newer admitted pose for visual presentation/late update, but that
pose cannot rewrite the committed Input snapshot, simulation state, network command or
earlier tick. Correlation records both sample identities where needed.

### 5. XR sampling joins the existing Input frame without a second input authority

The host gives XRRuntime one bounded owner-thread sampling opportunity after runtime
event handling and before `BuildInputSnapshot` commits. XROpenXR synchronizes the
accepted native action sets and locates input-owned action spaces through its private
port. XRRuntime validates and publishes one candidate snapshot; the XR/Input adapter
projects it into the same Input collection transaction as other devices.

The Input snapshot commit is the only point at which projected XR transitions become
eligible for context routing. A callback or worker result arriving after the cutoff is
deferred to the next input frame. XR sampling failure publishes an explicit unavailable/
lost state and neutralization candidate; Input never reuses an old snapshot as current.

The adapter maps backend-neutral XR physical controls/roles to canonical action values.
It cannot create project actions, bypass context priority, assign itself to a player,
consume transitions, mutate Runtime UI focus or call gameplay. High-frequency poses,
actions and joints do not travel through `EngineDataBus`.

### 6. Fixed simulation consumes tick-assigned values, never live XR state

After Input routing, the runtime constructs an immutable `GameplayInputFrame` for an
exact simulation tick, player/input-user assignment and source Input snapshot. It may
contain bounded semantic locomotion/look/use values and explicitly admitted pose/
tracking commands required by gameplay. It never contains native paths/handles, mutable
snapshot views or a query back into XRRuntime.

Multiple catch-up ticks follow each action's declared edge/hold/resampling policy.
Pressed/released edges cannot fire once per catch-up tick accidentally. Continuous pose
values retain their source sample time and generation; interpolation/extrapolation, if
admitted later, is a typed simulation policy and cannot be hidden inside a getter.

Authoritative gameplay transforms change only when Character/Physics/gameplay consumes
the tick frame at its safe point. A head/controller pose is evidence or command input,
not permission to write a Transform. Recording, replay and networking serialize the
bounded Horo tick form and provenance, not OpenXR events or “latest” live state.

Runtime UI and editor interaction may consume the frame/VariableUpdate projection
appropriate to their context, but their focus, capture, modal and command rules still
apply. A visual late pose is never retroactively routed as input.

### 7. Gesture recognition is an explicit derived provider

Raw hand joints, controller poses or gaze rays do not become semantic gestures by name
or heuristic in Input core. A host-composed gesture provider declares:

- provider/version and required source capabilities;
- accepted roles/joint/pose inputs and privacy class;
- finite gesture IDs, thresholds, hysteresis, timing and confidence policy;
- output action value types and cancellation/neutralization behavior; and
- deterministic-test and qualification requirements.

The provider consumes immutable XR snapshots and publishes bounded, generation-tagged
gesture candidates before the Input snapshot cutoff. Input maps admitted candidates to
canonical actions and owns routing/consumption. The provider cannot own player
assignment, UI focus, gameplay meaning, scene mutation or approval.

Provider replacement, tracking/permission/focus loss, threshold configuration change or
session replacement ends the old generation and releases active derived actions before
the new provider can publish. A last recognized pinch, grab or gaze dwell is not held as
current through loss.

### 8. Haptics are scoped commands with backend acknowledgements

An XR haptic request includes request/owner ID, Input user/player where applicable,
session/device/target generation, effect kind, bounded amplitude, duration, optional
frequency, start/deadline policy and cancellation token. Product policy and discovered
capability bound every field. An unsupported frequency or target is a typed result, not
silent success or arbitrary clamping unless the accepted plan declares that mapping.

The Input/application haptic coordinator validates caller scope and routes the command
through XRRuntime. XROpenXR applies/stops the native haptic and returns a bounded native
result. Request acceptance, native submission and physical completion are distinct
states; absence of runtime completion evidence cannot be reported as confirmed output.
OpenXR submission success therefore completes only the `SubmittedToRuntime` state. A
caller that needs lifecycle cleanup waits on owner cancellation/expiry and device/
session generation retirement, not on presumed physical completion; `PhysicallyComplete`
is published only when an optional provider capability supplies explicit evidence.

Focus/context loss, owner destruction, device/profile/session change, permission loss,
timeout, shutdown or explicit cancellation stops or invalidates the effect. Shutdown
cancels producers before native action/session destruction. No detached timer or
callback may outlive the request owner or dispatch/session generation.

### 9. Hand and eye data require capability plus purpose-bound consent

Controller aim/grip and head tracking required by an admitted baseline profile follow
that profile's declared product purpose and platform notice. Articulated hand joints,
eye gaze and gaze-derived foveation/interaction remain optional privacy-sensitive
capabilities. Discovery alone does not enable collection.

Admission requires the conjunction of product policy, runtime capability, applicable OS
permission, purpose-specific user/administrator/parental consent and the current privacy
policy revision. States distinguish `NotRequested`, `PermissionRequired`, `ConsentRequired`,
`Granted`, `Denied`, `Revoked`, `DisabledByPolicy`, `TemporarilyUnavailable` and `Lost`.
Permission and consent are not interchangeable.

The accepted purpose constrains which derived consumers receive data. Renderer may
receive an admitted gaze-foveation descriptor without receiving raw gaze history;
Runtime UI may receive a validity-aware ray without gaining diagnostic retention;
gesture providers receive only required joints. Gameplay, AIA and telemetry do not gain
access merely because another consumer is admitted.

Raw joints, gaze, continuous pose history and derived biometric/behavioral signals are
excluded from ordinary logs, crash dumps, metrics dimensions, replay, analytics, AI
context and support bundles. Explicit diagnostic/capture products require a separate
purpose, visible state, bounded duration, retention/export policy and redaction.
Revocation closes new collection, cancels dependent work, neutralizes derived actions
and atomically advances a shared `XRPrivacyRevocationGeneration` immediately. Snapshot
leases expose values only through checked accessors that compare their captured privacy
generation with this atomic generation; a mismatch returns `XRPrivacyLeaseRevoked`
without exposing joints/gaze. The next owner safe point performs orderly action
neutralization and storage retirement, but access invalidation does not wait for it.

Gaze, a gesture, tracked contact or haptic response never constitutes approval for an
editor, purchase, privacy or destructive operation. Approval remains a typed UI/
application transaction.

### 10. Lifecycle and loss are transactional

The action/tracking path follows:

```text
SchemaValidated
  -> NativeActionsCreated
  -> Attached
  -> Sampling
  -> Published
  -> Focused / Unfocused / TrackingLost / PermissionLost
  -> Neutralized
  -> Detached
  -> Destroyed
```

Schema and binding-plan validation is inert. Native candidates are created and attached
before Ready publication. One sample transaction collects native values, validates
bounded output, publishes XRRuntime state, then joins Input commit. Publication failure
does not partially update actions, poses or device assignment.

Focus/visibility/tracking loss, interaction-profile replacement, device removal,
reference-space/origin change, permission revocation, instance/session loss and shutdown
advance relevant revisions. Input releases active projected actions/capture and commits
neutral state before a replacement generation becomes eligible. Old haptic work is
cancelled, and outstanding native/sample work cannot publish into the replacement.

Shutdown closes sampling and haptic admission, neutralizes Input projections, drains or
cancels gesture providers, releases snapshot/consumer leases, destroys native action
spaces/actions/action sets, then releases dispatch/session dependencies in the order
owned by ADR-157/ADR-158.

### 11. Migration and contract coverage are explicit

The repository has no production XR input implementation to preserve. Initial work must
extend typed XRApi/Input contracts and host composition; it must not add OpenXR types to
Input, poll native actions from gameplay, expose latest-pose singletons or serialize
native paths as project action identity. Existing broad hand/gaze examples are
aspirational until capability and privacy gates are implemented.

Required automated coverage includes:

- target/public-header checks proving OpenXR types and native paths do not enter Input,
  XRApi or gameplay contracts;
- finite action-plan validation, binding conflicts, required/unbound failure, profile
  changes and compatible/incompatible override migration;
- bounded device/action/pose/joint snapshots, independent validity components, wrong-
  owner/stale generation rejection and capacity failure;
- XR sampling cutoff, late callback deferral, atomic Input commit and immediate loss
  neutralization;
- fixed-tick assignment, multi-tick edge policy, replay provenance and proof that live/
  presentation-late poses cannot mutate a committed tick;
- gesture provider determinism, hysteresis, replacement and loss neutralization without
  direct gameplay/UI mutation;
- haptic validation, unsupported fields, cancellation, timeout, focus/device/profile/
  session loss and shutdown;
- hand/eye permission and consent combinations, purpose isolation, revocation and
  diagnostic/telemetry/replay redaction; and
- failure/cancellation/shutdown at every lifecycle boundary with no action space,
  snapshot, callback, haptic, provider or consumer lease surviving its owner.

Deterministic fake snapshots and runtimes cover ordering and faults. They do not qualify
tracking quality, interaction-profile correctness, physical haptics, hand joints or eye
tracking on a device/runtime tuple.

## Consequences

### Positive

- Input remains backend-neutral while XR has one native sampling owner and one
  generation-safe projection path.
- Simulation, presentation and interaction can use deliberately different samples
  without corrupting deterministic tick input.
- Loss and replacement release actions, gestures and haptics instead of preserving
  stale state.
- Hand and eye capabilities have purpose-bound privacy gates and data-minimizing derived
  interfaces.
- Downstream binding, snapshot, haptic and harness tickets can implement against fixed
  owners and lifecycle order.

### Negative

- XR input needs explicit schema compilation, snapshot storage and a host-composed
  projection adapter instead of direct native queries.
- Renderer-late and simulation tracking snapshots require correlation and cannot share
  a mutable “latest pose” cache.
- Gesture and haptic providers require lifecycle/cancellation machinery and finite
  schemas.
- Hand/eye features need consent, permission, redaction and qualification work beyond
  runtime extension discovery.

## Rejected Alternatives

### Let Input poll OpenXR and own native action sets

Rejected because Input would acquire native session/path/space lifecycle, lose backend
neutrality and duplicate XRRuntime generation/focus state.

### Let gameplay read the latest XR pose during a fixed tick

Rejected because reads would depend on scheduling and presentation timing, undermine
record/replay/network determinism and allow one tick to observe inconsistent samples.

### Use presentation-late poses as simulation input

Rejected because a visual latency optimization cannot rewrite already committed input
or authoritative gameplay state.

### Persist OpenXR paths as gameplay action identity

Rejected because runtime paths are backend physical bindings, not Horo semantic actions,
localized labels or portable project schema.

### Implement gestures directly in Input core

Rejected because joint/pose interpretation is provider-, policy- and privacy-dependent;
Input should route admitted semantic candidates, not own every recognizer.

### Treat permission or runtime discovery as consent

Rejected because OS access, runtime capability, product purpose and informed consent are
independent requirements with different owners and revocation behavior.

### Preserve the last valid action or pose through loss

Rejected because stale held inputs, grabs, rays and haptics create unsafe and
non-deterministic behavior. Loss publishes invalid/neutral state with provenance.
