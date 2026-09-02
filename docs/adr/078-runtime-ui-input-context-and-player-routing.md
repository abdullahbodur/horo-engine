# ADR-078: Runtime UI Input Context and Player Routing

- **Status**: Proposed
- **Date**: 2026-09-02
- **Supersedes**: None
- **Scope**: Runtime UI/gameplay input priority, context/player/device/viewport identity, modal exclusivity, action consumption, pointer/text routing, device modality, focus/capture transitions, editor-play isolation, lifecycle, errors, compatibility, and shutdown
- **Issue**: [RUI-005.1](https://github.com/abdullahbodur/horo-engine/issues/735)
- **Jira**: [HORO-735](https://horo-engine.atlassian.net/browse/HORO-735)
- **Parent**: [RUI-005](https://github.com/abdullahbodur/horo-engine/issues/736)
- **Related**: [ADR-008](008-error-model-exception-boundary-and-registry.md), [ADR-018](018-command-registration-permissions-threading-and-packaged-build-policy.md), [ADR-033](033-presentation-and-display-ownership.md), [ADR-073](073-runtime-ui-ownership-scope-and-update-order.md)
- **Normative documents**: [Input Architecture](../architecture/runtime/input-architecture.md), [Game UI and HUD](../architecture/runtime/game-ui-and-hud.md), [Runtime Lifecycle](../architecture/runtime/runtime-lifecycle.md)

## Context

Runtime UI and gameplay consume the same normalized devices/actions but need
different priority and lifetime. A pause modal must block its player's gameplay,
split-screen players need independent focus, a game-instance confirmation may block
all local players, and an editor-hosted game viewport must not steal editor
shortcuts. Keyboard/mouse, gamepads, touch and text input also have different
assignment/capture rules and can change active presentation modality without
changing player ownership.

Treating device, player and viewport as the same identity fails local co-op: one
player may use keyboard plus mouse, a shared menu may accept multiple players, a
player may move to another viewport, and a device may disconnect/reassign. A single
process-global UI focus pointer would let one player or editor viewport overwrite
another. Routing directly from raw native events would bypass Input snapshots,
action mapping, modal ordering and deterministic tests.

ADR-073 introduced generation-checked per-player/viewport UI contexts and the rule
that pointer/focus interaction targets the last successfully presented layout. It
did not define audience identity, gameplay priority, modal scopes, assignment
revisions, consumption evidence or modality transitions. This decision supplies
that contract. RUI-005.2 through RUI-005.11 implement hit testing, event phases,
capture, controls, focus graph, spatial navigation, modality/glyphs, gestures and
feedback without creating another context/player router.

## Decision

### 1. Input owns evidence; Runtime UI owns UI routing

The authority split is:

| Responsibility | Owner |
|---|---|
| Raw/native event collection, device identity/lifecycle, normalization, action maps, per-player device assignment and immutable input snapshots | Input |
| UI context/audience stack, modal exclusivity, UI focus/capture, interaction target and action-consumption ledger | `RuntimeUiService` |
| Gameplay context/state and simulation input-frame construction | Gameplay/Input frame owner |
| Editor/native-dialog/tool/widget priority and embedded viewport focus gate | HoroEditor/Input context owner |
| Application-global/system commands and pause/route capabilities | Application/host owners |
| Glyph presentation and feedback realization | Input glyph/UI/Audio/Haptics capabilities |

Input never walks UI trees, chooses a focused element or invokes a UI callback.
Runtime UI never polls SDL/native devices, owns a device assignment, rewrites an
action map, constructs authoritative simulation input or changes gameplay pause
directly. Gameplay consumes only the eligible unconsumed projection for its exact
player/tick. Renderer and Platform do not route UI input.

All routing for one VariableUpdate uses one immutable `InputSnapshot` and the last
successfully presented `UiInteractionSnapshot`; no subsystem reads a newer mutable
layout or a second device sample.

### 2. Device, input user, player, viewport and UI context are distinct

The identities are:

- `InputDeviceId`: generation-checked physical/virtual device identity owned by
  Input;
- `InputUserId`: Input-owned assignment group containing zero or more devices;
- `LocalPlayerId`: application/gameplay-owned local participant identity;
- `LogicalViewportId`: presentation-owned logical viewport generation;
- `RuntimeUiInputContextId`: Runtime UI identity naming exact game runtime, owner
  scope, audience, route/layer, viewport attachment and context generations.

Mapping snapshots explicitly relate `InputDeviceId -> InputUserId -> LocalPlayerId`
and local player to zero/one/multiple eligible logical viewports. None is derived by
numeric equality, array index, device type, window handle or current focus. A
keyboard/mouse pair may belong to one input user; a player may have multiple devices;
a game-instance menu may admit several players; an unassigned join context may
observe only its declared join actions.

Runtime context handles are transient and never serialized. Authored documents may
name typed audience/route/input policies, not player numbers, device GUID strings,
native window IDs, controller pointers or editor widget identities.

### 3. Every context declares one audience and viewport policy

`UiInputAudience` is a closed variant:

```cpp
enum class UiInputAudienceKind : std::uint8_t {
    SinglePlayer,
    SharedLocalPlayers,
    GameInstance,
    UnassignedJoin,
};
```

`SinglePlayer` names one exact `LocalPlayerId` generation. `SharedLocalPlayers`
contains a bounded stable ordered set and records which player initiated each
action. `GameInstance` accepts application-authorized local users independent of a
single player and is used for boot/main/critical global menus. `UnassignedJoin`
accepts only declared join/navigation actions from unassigned devices and cannot
reach gameplay or ordinary UI actions before assignment.

A context also declares `BoundViewport`, `AnyOwnedViewport`, or `NoPointerViewport`.
Pointer/touch interaction always requires one admitted viewport attachment and
presented interaction revision. Navigation-only global/shared menus may use
`NoPointerViewport`; they still need a presentation/focus target to become visible.
One context cannot silently retarget to a replacement viewport or player generation.

### 4. Routing uses one ordered context stack and consumption ledger

For packaged games, eligible contexts route high to low:

1. host-critical/native system dialog capability;
2. game-instance exclusive Runtime UI modal;
3. player/viewport exclusive Runtime UI modal;
4. active non-exclusive Runtime UI route/layer for that audience;
5. associated gameplay action context;
6. declared application-global non-mutating commands.

Editor play prepends native dialogs, editor modals/popups, focused editor widgets,
active editor tool capture and editor workspace contexts. Embedded Runtime UI is
eligible only when its exact game viewport owns editor focus/capture policy. It
cannot consume editor shortcuts or pointer transitions outside that viewport.

Each input transition/action edge has one `InputTransitionId`. The router writes an
immutable bounded `InputConsumptionLedger` recording snapshot revision, transition,
winning context, audience/player, target/reason and consumed/blocked/passthrough
outcome. A transition is consumed at most once. Held/axis state is projected only
to contexts allowed by action-map and exclusivity policy; it is not globally
consumed like an edge.

Gameplay input-frame construction receives the ledger-filtered action projection
for its exact player and snapshot/tick mapping. It never guesses that a visible UI
means blocked gameplay. UI actions become typed owner commands at safe points, not
direct ECS/application mutation inside the router.

### 5. Exclusive modal scope blocks even unhandled lower input

`UiModalExclusivity` is `None`, `Viewport`, `Player`, or `GameInstance`.

- `Viewport` blocks lower UI/gameplay pointer/navigation contexts for one exact
  viewport attachment;
- `Player` blocks that player's lower contexts across admitted viewports;
- `GameInstance` blocks all local player/gameplay contexts in that game runtime;
- `None` consumes only transitions actually handled by the UI route/control.

An exclusive modal blocks associated lower contexts even when no element handles a
transition. Otherwise an unrecognized button could leak into gameplay behind a
pause/confirmation screen. Only a finite declared passthrough allowlist of host-
owned safety/accessibility/global actions may bypass exclusivity. Content cannot
allow arbitrary gameplay actions by string.

Each audience/viewport has a stable modal stack; only the top eligible modal routes.
Opening an exclusive modal cancels affected pointer/drag/text capture, snapshots the
prior focus-restoration chain, neutralizes gameplay held/edge state according to
Input policy, and then activates the modal context atomically. A dim render layer
alone never creates exclusivity.

### 6. Assignment changes are transactional and neutralize old ownership

Input publishes a generation/revisioned device/user/player assignment snapshot at
its owner-thread cutoff. Runtime UI binds contexts to that exact revision. Device
connect/disconnect, join, leave, reassignment, player removal or profile replacement
builds a candidate mapping; no context observes half an assignment transaction.

When an assignment changes, the old player/context:

1. receives typed device/assignment-lost evidence;
2. releases pointer/text/gesture capture owned through removed devices;
3. synthesizes neutral releases for affected held UI actions under Input policy;
4. retains or clears focus according to route policy, but cannot keep a device
   pointer;
5. stops routing before the new assignment becomes active.

The new owner starts from a neutral snapshot unless an explicit handoff protocol
declares otherwise. Pressed/held input is never transferred implicitly, preventing
a reassigned button/stick from submitting or moving immediately. Late events from
an old device generation are rejected.

### 7. Pointer, navigation and text follow separate targeting rules

Pointer/touch events first resolve the editor/native focus gate and logical viewport
region from the Input/viewport snapshot. Runtime UI then transforms coordinates and
hit-tests only the last successfully presented `UiInteractionSnapshot` for that
attachment. Failed/skipped presentation or a mismatched resize/DPI/layout revision
suppresses pointer eligibility until a matching snapshot is presented.

Pointer capture belongs to one `RuntimeUiInputContextId`, player/input user, pointer,
initiating control/button, target element and interaction revision. It routes
continued movement/release inside its admitted viewport and ends on release,
cancellation, focus/device/assignment loss, modal replacement, target/context/
route/scope/viewport destruction, suspension or shutdown. Capture cannot jump to a
different player's viewport.

Navigation/submit/cancel actions target the audience context's focused element and
do not require a pointer. Text/IME events target only the active text focus/capture
surface after platform/editor gating. Physical key bindings, Unicode text and IME
composition are distinct; a key consumed as editing/navigation cannot also execute
a gameplay/global shortcut unless explicitly allowed before routing.

### 8. Focus is per context/audience, not process global

Each active UI context owns at most one generation-checked focus target plus a
bounded restoration stack of stable element IDs and route/context generations.
Shared-player contexts additionally record the player/input user that last moved
focus or submitted, without transferring element ownership.

Modal open saves the previously active target after validating scope. Modal close/
cancel restores the newest still-valid target; otherwise it applies the route's
declared default/spatial/ancestor/none fallback in that order. Structural/style/
layout replacement reconciles by stable element ID only after the new interaction
generation publishes. A stale handle never remains focused.

Focus ownership transitions are atomic with context activation/deactivation.
Player/viewport/route/context destruction clears its focus/restoration entries.
Game-instance focus does not overwrite a player's private focus; it temporarily
preempts routing and restores each affected context independently.

### 9. Device modality is presentation evidence, not assignment

`UiInputModality` is a closed Horo value: `KeyboardMouse`, `Gamepad`, `Touch`,
`Pen`, `Accessibility`, or `Unknown`. It is tracked per input user/player UI context,
not one global last device. Modality drives glyph/layout/prompt presentation through
immutable evidence; it does not assign a device, move focus automatically or grant
input priority.

Input marks a transition as modality-meaningful after deadzone/noise filtering.
Runtime UI updates modality from the highest routing-eligible meaningful transition
using snapshot sequence, then stable device-class tie order for exact simultaneous
evidence. Pointer hover noise, gamepad drift, synthetic neutral releases, background
devices and transitions blocked before the Runtime UI gate do not switch modality.

A configured finite hysteresis may prevent rapid keyboard/gamepad prompt flicker;
its thresholds and pending evidence are typed policy with a revision, not wall-time
heuristics inside widgets. Disconnect/reassignment selects the next eligible recent
modality or `Unknown` without borrowing another player's state. RUI-005.8 owns glyph
selection details within this boundary.

### 10. Context lifecycle is explicit and owner-safe

Contexts follow:

```text
Created -> Binding
Binding -> Ready | Failed | Retiring
Ready -> Active | Retiring
Active <-> Suspended
Active | Suspended | Failed -> Retiring
Retiring -> Destroyed
```

Binding validates game/scope/audience/player/assignment/viewport/route/action-map/
interaction generations and required capabilities. Activation publishes the context
and priority/modal/focus state atomically at ADR-073's owner-thread cutoff.
Suspension makes it ineligible and releases capture while retaining declared focus
state; it cannot consume or buffer transitions for later replay.

Deactivation/retirement first closes routing admission, records terminal ledger
outcomes/neutralization, cancels capture/composition, removes modal/focus entries and
then releases generation leases. Late async/input completions cannot publish into a
new context slot. Shutdown retires Runtime UI contexts before Input, Platform,
viewports or editor focus services disappear and is idempotent after partial bind.
Every state that may have acquired a lease reaches `Retiring`; a failed bind cannot
jump directly to destruction and leak partial resources.

### 11. Errors, limits and compatibility are typed

Errors follow ADR-008 with stable reason codes for unknown/stale device/user/player/
viewport/context/route/interaction/assignment identity, invalid audience/modal/
priority/passthrough policy, conflicting ownership, missing action map/capability,
double consumption, capture conflict, focus restoration failure, unsupported
modality, ledger/capacity exhaustion, cancellation and shutdown. Context contains
bounded IDs/revisions/reasons without raw text, native keycodes, device serials or
user-sensitive input payloads.

Limits cover contexts per game/player/viewport, audience players, modal depth,
passthrough actions, focus restoration, captures/pointers, transitions/ledger rows,
modality history and routing work. Exhaustion rejects a context/policy activation
candidate or discards only explicitly optional, lowest-priority transitions while
recording bounded overflow evidence and neutralizing affected held state. Routing
never blocks the real-time Input producer, leaks an exclusive transition to gameplay
or drops a required release silently.

Cooked UI/input policies use versioned typed audience, viewport, exclusivity,
action and modality enums. Unknown required semantics reject and request recook.
Editor/platform/native device identifiers remain adapter-private. Headless tests use
Input's virtual devices and the same Horo snapshots/router; they do not bypass
contexts with direct widget calls.

### 12. Verification is part of the contract

Required coverage includes:

- device/user/player/viewport/context non-equivalence, multi-device player, shared
  menu, split screen, multiple viewports and unassigned join;
- packaged and editor-play priority, unfocused embedded viewport, native/editor/UI/
  gameplay/global routing and at-most-once transition consumption;
- non-exclusive handled/unhandled behavior and Viewport/Player/GameInstance modal
  blocking with finite passthrough actions;
- modal stack, capture cancellation, gameplay neutralization and independent focus
  restoration for multiple players;
- assignment connect/disconnect/join/leave/reassign, neutral new owner, held input,
  stale old generation and partial transaction failure;
- last-presented pointer targeting, failed/skipped presentation, resize/DPI/revision
  mismatch, multi-pointer capture and cross-viewport rejection;
- navigation versus physical key/text/IME routing and editing/global shortcut
  exclusion;
- modality deadzone/noise/simultaneous evidence/hysteresis/disconnect and no player-
  global prompt leakage;
- structure/route/modal/player/viewport/context destruction, suspension/resume,
  shutdown and exactly-once releases;
- capacity/ledger overflow safety, malformed/version-skewed policy, bounded/redacted
  diagnostics and deterministic virtual-device fixtures;
- no SDL/native/editor widget/renderer types or transient input payloads in public,
  cooked or runtime context snapshots.

Property tests compare routing/consumption/focus/modal outcomes over generated
context stacks and transition sequences. UI screenshots may verify glyph/modality
presentation later, but cannot replace routing ledger tests.

## Consequences

Local co-op, shared menus, split screen, editor play and global modals gain one
explicit priority model without conflating devices, players or viewports. Gameplay
cannot receive input blocked by an exclusive UI, focus/capture cannot survive owner
destruction, and modality can change prompts without changing assignment.

The cost is generation-scoped audience/context identities, immutable assignment and
interaction snapshots, consumption ledgers, modal/focus/capture lifecycle, neutral
handoff and per-player modality state. Widgets cannot poll input or own global focus.

## Rejected Alternatives

### Treat device, player and viewport index as one identity

Rejected because multi-device players, shared menus, reassignment and multiple
viewports have different lifetimes and cardinality.

### Let gameplay and UI poll the same snapshot independently

Rejected because both may react to one transition and modal blocking becomes an
implicit visibility guess. One router publishes a consumption ledger.

### Block gameplay only when a modal handles an action

Rejected because unrecognized input leaks through exclusive pause/confirmation UI.
Exclusivity blocks associated lower contexts even when unhandled.

### Keep one process-global focus and active modality

Rejected because split-screen players and editor/runtime viewports overwrite each
other. Focus and modality are context/audience scoped.

### Route from raw/native events inside Runtime UI

Rejected because it bypasses normalization, action maps, player assignment,
editor/native priority, replay and headless tests.

### Retarget capture or held input during device reassignment

Rejected because stale presses can submit/move under a new owner. Old ownership is
neutralized and the new owner starts from an explicit neutral snapshot.

### Switch modality on any device activity

Rejected because stick drift, pointer hover noise and background devices cause
prompt flicker. Only routing-eligible meaningful evidence updates modality.

### Use a dim visual layer as the modal input barrier

Rejected because rendering order is not input authority. Typed modal contexts and
their lifecycle enforce exclusivity centrally.
