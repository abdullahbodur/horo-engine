# ADR-161: XR Interaction, Runtime UI, Locomotion and Accessibility Ownership

- **Status**: Proposed
- **Date**: 2026-09-02
- **Supersedes**: None
- **Scope**: XR ray/direct/proximity evidence, Runtime UI hit testing/focus/capture, world-space canvas presentation, gameplay interaction intent, Character locomotion authority, Camera/view composition, tracking-origin/recenter policy, comfort/accessibility preferences, capability tiers, lifecycle, migration and validation
- **Issue**: [XRA-005.1](https://github.com/abdullahbodur/horo-engine/issues/2148)
- **Jira**: [HORO-2102](https://horo-engine.atlassian.net/browse/HORO-2102)
- **Related**: [ADR-026](026-large-world-precision-and-floating-origin-strategy.md), [ADR-061](061-animation-ownership-update-order-and-clock.md), [ADR-073](073-runtime-ui-ownership-scope-and-update-order.md), [ADR-078](078-runtime-ui-input-context-and-player-routing.md), [ADR-082](082-runtime-ui-accessibility-capability-and-ownership.md), [ADR-089](089-character-controller-ownership-implementation-and-update-order.md), [ADR-157](157-xr-ownership-runtime-composition-and-capability-tier.md), [ADR-159](159-xr-action-tracking-and-input-projection-ownership.md), [ADR-160](160-xr-rendering-openxr-compositor-and-renderer-ownership.md)
- **Normative documents**: [XR Architecture](../architecture/runtime/vr-ar-architecture.md), [Game UI and HUD](../architecture/runtime/game-ui-and-hud.md), [Input Architecture](../architecture/runtime/input-architecture.md), [Character Controller](../architecture/runtime/character-controller-architecture.md), [Accessibility Architecture](../architecture/runtime/accessibility-architecture.md)

## Context

The XR foundation keeps tracking/actions in XR, canonical actions in Input, world-space
UI semantics in Runtime UI, GPU work in Renderer and authoritative character movement
in Character/Physics. Existing text says XR supplies rays, touches and poses through
adapters and that locomotion produces character intent. It does not yet define where a
ray becomes a hit, where hover becomes focus, who owns pointer capture, how tracked head
motion composes with a collision root, or which comfort/accessibility controls must be
available independently of rendering/device tier.

Without explicit boundaries, an XR callback could set UI focus, a gesture could move an
entity, a camera could teleport the Character capsule, a physics hit could invoke a
button, or a world-space canvas could maintain a second interaction tree. Recenter and
room-scale motion could also rewrite authored/world transforms. Treating comfort as a
premium renderer capability would make accessibility depend on quality tier rather than
on gameplay semantics.

This ADR fixes the typed evidence/intent/authority chain and the non-gating comfort
baseline. XRA-005.2 and later tickets may specialize ray/direct adapters, world-space
projection, focus/capture, locomotion commands, comfort settings and tests without
moving these owners.

## Decision

### 1. XR supplies evidence; domain owners decide meaning

The interaction path is:

```text
XRRuntime tracking/action snapshots
        |
        v
host-composed XR interaction adapters
  ray/direct/proximity evidence with generation and validity
        |
        +--> Physics/scene query ports --> bounded hit candidates
        |
        +--> Runtime UI interaction input
        |      hit-test, hover, focus, capture, submit/cancel
        |
        +--> gameplay interaction resolver
               use/grab/teleport/locomotion intents
                        |
                        v
               Character / Physics / gameplay authority
```

An adapter converts coordinate spaces, validates generations and forms bounded
candidates. Evidence says where a tracked source was and what it intersected; intent
says what the user requested; authority validates and commits the resulting state change
at its safe point. No pose, ray, contact, dwell, gesture, haptic response or native
action invokes UI or gameplay directly. Input context routing remains the gate through
which semantic actions become eligible.

### 2. Every interaction and movement responsibility has one owner

| Responsibility | Sole owner | Deliberate non-owner |
|---|---|---|
| Tracking/action/device/reference-space snapshots | XRRuntime/XROpenXR under ADR-159 | Runtime UI, Character and Camera |
| Canonical actions, contexts, player/device assignment and consumption | Input | XR interaction adapters |
| Ray/direct/proximity coordinate conversion and evidence candidates | Host-composed XR interaction adapter | Native callbacks and UI widgets |
| World geometry query, collision/contact/overlap truth | Physics/Scene query owners | XRRuntime and Runtime UI |
| Runtime UI layout, hit-test tree, hover, focus, capture and semantic action | RuntimeUiService | XR and Renderer |
| World-space canvas semantic ownership and lifecycle | RuntimeUiService owner scope | XR session and scene ECS by proximity |
| Canvas projection, depth/occlusion and per-view pixels | Runtime UI extraction plus Renderer | UI hit-test semantics and XR tracking |
| Gameplay use/grab/manipulation meaning and target validation | Gameplay/interaction owner | Input, gesture and Physics callback |
| Desired locomotion/turn/teleport intent | Gameplay/player locomotion owner | XRRuntime and Camera |
| Collision-root displacement, support and final transform | Character/Physics | Camera and tracked head pose |
| Tracked-head/view composition and presentation-only comfort effects | Camera/view coordinator plus Renderer | Character collision authority |
| Tracking-origin/recenter/calibration policy | Application/XR spatial coordinator | Character Transform and UI widgets |
| Accessibility/comfort preference values and provenance | Configuration/Accessibility | Renderer quality tier and XR runtime defaults |
| Effective per-player comfort plan | Gameplay/Camera/UI owners at their boundaries | A global XR manager |
| Haptic request scope/cancellation and native output | ADR-159 owners | Interaction hit candidates |

Local player, Input user, XR device, interaction source, pointer, Runtime UI context,
canvas, focus target, gameplay actor, Character controller, camera view and tracked
space are distinct identities joined by immutable generation-scoped mappings.

### 3. Interaction sources publish bounded typed evidence

An `XRInteractionSourceSnapshot` names the XR session/device/source generation, Input
user/player assignment revision, source kind, reference space/origin revision, sample
time, validity/confidence and enabled capabilities. Source kinds are finite Horo values
such as controller aim ray, controller direct point, admitted hand pointer, admitted eye
gaze ray or proximity volume. Native paths, handles and product names are absent.

Ray evidence records origin, normalized direction and bounded maximum distance. Direct
evidence records a generation-scoped point/shape and contact phase; proximity records a
bounded volume/range. Invalid orientation/position, missing permission, tracking loss or
stale origin produces explicit unavailable evidence, never an identity ray, last-known
hit or zero-length success.

Adapters use narrow read-only Physics/Scene/Runtime UI query ports and return ordered,
bounded candidates with target-domain identity, distance/depth, surface/element evidence,
query revision and occlusion policy. A candidate is not hover, focus, capture, selection,
grab, use or teleport approval. Each destination validates its own target generation.

The adapter runs only at the host-declared interaction boundary from one immutable XR
and presented-layout snapshot. It retains no mutable target pointers, blocks on no query,
allocates no unbounded candidate list and publishes no high-frequency evidence on a
general data bus.

### 4. Runtime UI remains the sole UI interaction authority

World-space canvases are ordinary Runtime UI documents/instances under an exact game,
player, scene or viewport owner scope. XR session lifetime does not silently own them.
They use the same element IDs, layout, localization, semantic accessibility tree, route
bands, focus graph and action-command model as screen-space UI.

The XR/UI adapter projects an eligible source into the last successfully presented
`UiInteractionSnapshot` for the exact player/viewport/view/canvas revisions. Runtime UI
alone performs semantic hit testing and resolves hover, focus, press/release, drag,
scroll, submit/cancel and capture. A Renderer depth result may participate as bounded
occlusion evidence but cannot choose focus or invoke a control.

Each XR pointer has a generation-safe pointer/capture identity. Capture begins only from
an eligible routed press/action and matching presented target. It ends on release,
cancel, modal/route exclusion, focus loss, source/device/profile/session loss, tracking
invalidity, owner/canvas destruction, assignment change or presentation-revision loss.
Switching between ray and direct modes crosses an explicit neutral boundary.

Failed/skipped presentation leaves new world-space geometry ineligible, just like
screen-space UI. Pointer visuals may show unavailable/blocked state but cannot make
unpresented layout interactive. UI actions become typed application/gameplay commands;
widgets never mutate Character, Scene, Physics or XR state directly.

### 5. Rendering owns world-space projection and occlusion, not semantics

Runtime UI publishes immutable per-view world-canvas render and interaction geometry
from the same document/layout generation. Renderer transforms it through the admitted
XR view set, performs N-view projection, clipping, depth policy, occlusion and pixel
composition under ADR-160. It never reconstructs a UI tree from draw calls.

Logical layout/hit geometry is view-independent where possible and remains in Runtime UI
units/world coordinates. Per-view pixel snapping, depth tests, foveation, render scale or
visibility do not feed back into semantic element identity. If product policy requires
visible-only interaction, Renderer provides delayed/bounded typed occlusion evidence for
the matching view/frame; the UI owner decides eligibility against its revision. There is
no same-frame GPU readback stall.

Head-locked HUD, world-anchored canvas, hand/controller-attached panel and compositor
layer are different attachment/presentation policies, not different UI ownership
systems. A native composition layer remains XROpenXR submission under ADR-160 and cannot
carry mutable widget pointers or bypass Runtime UI focus/accessibility.

### 6. Gameplay interaction receives intent, never direct mutation

After Input routing, the gameplay interaction resolver may combine an eligible semantic
action with the exact XR source and bounded Physics/Scene candidate into a typed intent.
An intent names player/actor, tick, source/candidate generations, requested operation and
bounded parameters. It contains no native handles, arbitrary target pointers or pre-
authorized result.

The owning gameplay system revalidates range, line of sight, target capability,
authority, cooldown, inventory/state and network role at its fixed-tick safe point.
Physics owns collision/contact evidence; gameplay owns use/grab/throw/equip meaning;
Character owns its collision root. Multiplayer clients send bounded intentions through
the normal network authority contract, not trusted XR poses or client hit results.

Focus/capture in Runtime UI and grab/capture in gameplay are separate leases. A source
cannot drive both when its Input context/consumption ledger grants exclusivity. Opening
a modal, changing assignment or losing tracking neutralizes pending interaction before
another domain becomes eligible.

### 7. Locomotion is gameplay intent resolved by Character

The player locomotion owner maps canonical Input/XR actions, settings and tracking-space
evidence into tick-assigned intents such as move, rotate, snap turn, teleport request,
stance/height mode or recenter request. It owns locomotion state-machine meaning,
cooldowns and mode policy but never writes Scene Transform directly.

Continuous/snap movement becomes an ordinary Character movement/heading request for the
exact fixed tick. Character applies platform carry, collision queries, slope/step rules,
root motion and final displacement under its existing authority. A teleport is a typed
Character/application command with destination/query provenance, clearance validation,
network authority and commit/rollback; moving the camera or XR reference space is not a
collision-safe teleport.

Tracked room-scale head/controller motion is local evidence inside the calibrated
tracking space. It does not move the collision root each render frame. A product may
declare a bounded body-follow/room-scale reconciliation policy that produces future
Character intents at tick boundaries; Character still validates displacement and owns
the committed root. Boundary violation returns typed blocked/recovery evidence rather
than silently moving world geometry.

### 8. Camera/view composition does not acquire movement authority

The Camera/view coordinator composes the committed Character/world root, tracking-origin
calibration, current admitted head/view pose and presentation-only offsets into render
views. Each input carries its generation and clock domain. The coordinator does not
write Character, XR tracking or authored camera transforms.

Recenter changes an application/XR calibration transform at an owner safe point. It does
not teleport the Character capsule, rewrite the scene, fabricate a Stage space or modify
native tracking history. A product may separately request a Character heading/root
alignment transaction; success is explicit and collision/authority checked.

Camera collision/comfort presentation effects such as vignette, blink/fade, snap-turn
transition or motion reduction are derived visual/audio/UI outputs. They cannot hide a
failed Character move, advance simulation, change the native pose or become authority
for locomotion. Presentation-late pose remains visual-only under ADR-159.

### 9. Comfort and accessibility are non-gating across product tiers

Every XR product declares a finite `XRComfortPlan` per local player/profile. The plan
resolves configuration preferences against authored locomotion capabilities and current
session state. Rendering quality, headset class, optional package, ray tracing,
multiview, hand/eye tracking or post-1.0 capability cannot gate the semantic availability
of baseline comfort/accessibility controls.

When the product offers artificial locomotion or rotation, its baseline plan exposes:

- a way to disable artificial movement/rotation independently where gameplay permits;
- snap-turn support with configurable finite angle/cooldown, alongside any smooth turn;
- configurable move/turn speed and handed/dominant-side control mapping;
- seated/standing and calibrated height/recenter behavior with explicit availability;
- motion-reduction presentation policy, including intensity/disable controls for any
  vignette/tunneling/fade used by the product;
- teleport/continuous locomotion availability and limitations as separate authored
  capabilities rather than silent fallbacks; and
- remappable canonical actions plus existing subtitle, visual, input and semantic UI
  accessibility families.

An authored experience that fundamentally requires a movement mode may state that
requirement, but cannot falsely advertise an unavailable alternative or hide comfort
settings on a lower renderer tier. If a required accessibility/comfort semantic cannot
be provided, product admission/qualification reports the limitation explicitly; it does
not silently enable a different movement mode.

Configuration owns desired preference/provenance. Gameplay owns locomotion meaning,
Character owns movement, Camera/Renderer own presentation effects, Runtime UI owns
settings/focus semantics and Accessibility owns cross-feature policy/evidence. There is
no global `XRComfortManager` mutating all owners.

### 10. Focus, tracking and capability loss neutralize the whole chain

Interaction follows explicit source and target lifecycle:

```text
Unavailable
  -> Eligible
  -> Hovering
  -> Pressed / Captured
  -> Released / Cancelled
  -> Neutralized
  -> Unavailable
```

Runtime UI owns its state transitions; gameplay interaction and locomotion use their own
intent/command lifecycles. Source/device/profile/session, Input assignment/context,
tracking validity, reference-space/origin, target/canvas/scene, presented-layout and
permission revisions fence every operation.

Loss closes new intent admission, cancels UI/gameplay capture, releases projected Input
actions, drops pending uncommitted intents, stops owned haptics and publishes neutral/
unavailable visuals. It never transfers capture to another source, reuses the last ray,
commits a pending teleport or invokes release on a replacement target.

Replacement publishes complete new mappings/snapshots before eligibility. Matching
labels, poses, hit points or element IDs do not revalidate stale generations. Shutdown
disconnects interaction producers, neutralizes Input/UI/gameplay, joins query/provider
work, releases focus/capture and render leases, then destroys owners in established
reverse dependency order.

### 11. Unsupported and privacy-sensitive paths are explicit

Eye-gaze and articulated-hand interaction remain optional under ADR-159. Their derived
ray/gesture is admitted only for a purpose and consumer; it cannot silently replace a
required controller path or grant UI/gameplay/diagnostic access to raw data. Gaze dwell,
pinch, contact or voice never constitutes approval for destructive, purchase, privacy or
editor operations.

Stable typed failures include source unavailable, tracking invalid, permission/consent
required or revoked, stale assignment/origin/layout/target, query capacity exceeded,
occluded/ineligible target, Input consumed/blocked, focus/capture conflict, movement mode
unavailable, teleport invalid/unauthorized, Character obstruction, comfort limitation and
cancelled/shutdown. Failure retains no raw continuous pose/gaze/joint history in ordinary
logs or metrics.

Non-XR products use the same Runtime UI, Input, Character and Accessibility semantics
without XR adapters. Headless/server products omit local XR/UI/camera presentation;
servers validate received gameplay intentions and never trust client tracking or hits as
authority.

### 12. Migration and contract coverage are required

There is no production XR interaction implementation to preserve. Initial work must add
narrow host adapters and typed snapshots/intents. It must not make XR devices into UI
owners, add camera-driven Character transforms, bypass Input contexts, fork a second
world-space widget system or gate accessibility settings on renderer tier.

Required automated coverage includes:

- owner/dependency tests across XR, Input, Runtime UI, Renderer, Character, Camera and
  Accessibility with no native handles or mutable cross-owner pointers;
- ray/direct/proximity validity, coordinate/origin conversion, bounded query ordering,
  stale generation and capacity behavior;
- last-presented UI hit testing, focus/capture/consumption, modal/route arbitration,
  source-mode change and every loss/cancellation path;
- world-space canvas N-view projection/occlusion correlation without semantic feedback
  or same-frame GPU readback;
- gameplay intent revalidation, fixed-tick/network authority and proof that candidates/
  gestures cannot mutate Scene/Physics directly;
- continuous/snap/teleport intent through Character collision/root authority, including
  room-scale reconciliation and failed recenter/teleport separation;
- Character root + tracking-origin + head-pose Camera composition with presentation-late
  pose unable to alter simulation;
- baseline comfort setting availability across renderer/device/product tiers, explicit
  authored limitations and no silent movement fallback;
- hand/gaze purpose isolation and approval/privacy protections; and
- replacement/shutdown with no pointer, capture, haptic, intent, query, render or
  snapshot lease surviving its owner.

Deterministic XR/Input/UI/Physics/Character fakes validate order and faults. Physical
device evidence remains required for ergonomics, tracking, occlusion, motion comfort and
accessibility qualification.

## Consequences

### Positive

- XR evidence reaches UI and gameplay through one typed, reviewable intent chain.
- Runtime UI retains authoritative focus/capture and Character retains collision-root
  movement.
- Camera/recenter/room-scale behavior cannot silently mutate authoritative transforms.
- World-space UI reuses existing semantic/accessibility ownership across N views.
- Baseline comfort settings stay independent of graphics/device tier.

### Negative

- Interaction requires explicit adapters, query snapshots and generation correlation
  instead of direct callbacks.
- Visible-only world-space interaction may use delayed occlusion evidence rather than
  immediate GPU truth.
- Locomotion and recenter require separate Character and calibration transactions.
- Products must document authored comfort alternatives and limitations per profile.

## Rejected Alternatives

### Let XR set Runtime UI focus or invoke widgets

Rejected because XR supplies physical evidence while Runtime UI owns the presented hit-
test tree, focus, capture, semantic actions and command boundary.

### Let Physics hits trigger gameplay directly

Rejected because collision evidence does not own action consumption, gameplay meaning,
authority, cooldowns or target-state validation.

### Move the Character by writing the camera or tracking-space transform

Rejected because visual/tracking transforms do not perform collision, support, network
authority or fixed-tick movement and would split collision-root truth.

### Create a separate XR widget system

Rejected because it would duplicate Runtime UI documents, layout, focus, localization,
accessibility, lifecycle and rendering semantics.

### Gate comfort controls on high-end rendering or headset tiers

Rejected because comfort/accessibility semantics must remain available independently of
quality tier. Unsupported authored alternatives are explicit product limitations.

### Treat teleport or recenter as the same operation

Rejected because teleport changes an authoritative collision root, while recenter changes
tracking calibration. They have different owners, validation and rollback.

### Preserve capture or the last hit through tracking loss

Rejected because stale rays/targets can invoke the wrong control or gameplay object.
Loss neutralizes and requires a complete new generation.
