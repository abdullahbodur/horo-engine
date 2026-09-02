# ADR-080: Runtime UI Presentation Scope, Layer and Route

- **Status**: Proposed
- **Date**: 2026-09-02
- **Supersedes**: None
- **Scope**: Runtime UI presentation dimensions, persistent/player/scene/viewport ownership projection, world/HUD/screen/overlay/modal/loading/debug bands, route/stack identity and transactions, visibility/coverage/input/transition semantics, loading/debug policy, rendering, errors, compatibility, and shutdown
- **Issue**: [RUI-008.1](https://github.com/abdullahbodur/horo-engine/issues/768)
- **Jira**: [HORO-768](https://horo-engine.atlassian.net/browse/HORO-768)
- **Parent**: [RUI-008](https://github.com/abdullahbodur/horo-engine/issues/767)
- **Related**: [ADR-033](033-presentation-and-display-ownership.md), [ADR-073](073-runtime-ui-ownership-scope-and-update-order.md), [ADR-077](077-runtime-ui-animation-clock-and-time-domain.md), [ADR-078](078-runtime-ui-input-context-and-player-routing.md)
- **Normative documents**: [Game UI and HUD](../architecture/runtime/game-ui-and-hud.md), [Runtime Lifecycle](../architecture/runtime/runtime-lifecycle.md), [Rendering Architecture](../architecture/runtime/rendering-architecture.md)

## Context

Runtime UI includes persistent menus and transition covers, per-player HUDs,
scene-owned world/screen overlays, viewport-local masks, modal dialogs, loading
presentation and development diagnostics. These concepts overlap but are not one
hierarchy. A persistent player HUD can render in the HUD band, a scene-owned route
can present as a modal, and a game-instance loading cover can hide all player stacks
without owning or destroying them.

If one `layer` field controls lifetime, z-order, input and route navigation, moving
content visually can accidentally transfer ownership or destroy state. Arbitrary
integer z values and backend draw order would make modal/loading priority
non-portable. A process-global route stack would let split-screen players replace
each other's screens, while a separate uncoordinated stack per viewport could fail
to enforce a game-instance modal or loading cover.

ADR-073 owns GameInstance/Player/Scene/Viewport semantic lifetime. ADR-078 owns
input audiences/modal exclusivity. ADR-077 owns screen-transition clocks. This
decision composes those axes into one presentation/route contract without changing
their authorities. RUI-008.2 through RUI-008.10 implement stack operations,
modal/focus, async loading/recovery, HUD association, persistence, loading,
cinematic suppression, split screen and qualification within it.

## Decision

### 1. Ownership, audience, route, band and visibility are separate dimensions

Every presented route/instance records five independent typed facts:

1. ADR-073 `UiOwnerScope`: who owns semantic state/lifetime;
2. ADR-078 `UiInputAudience`: which player(s)/game instance may interact;
3. `UiRouteInstanceId`/stack: navigation identity and transactional history;
4. `UiPresentationBand`: ordered composition/input role;
5. `UiRouteVisibilityState`: current presentation eligibility without ownership
   transfer.

`Persistent` is not a magic visual layer. GameInstance-owned UI persists across
scene/player/viewport replacement; Player-owned UI persists across scenes only with
that player; Scene/Viewport UI ends with its exact owner. A route cannot extend a
scope's lifetime by setting a persistent flag, and attaching it to another viewport
does not transfer ownership.

Changing band, audience, owner scope or stack is a typed replacement transaction,
not mutation of one ambiguous integer. Renderer, Input and editor widgets consume
the resolved plan; none infer semantic ownership from draw order or visibility.

### 2. Route and stack identities are stable and generation checked

The model uses:

- `UiRouteId`: stable authored route definition identity;
- `UiRouteInstanceId`: generation-checked activation of one route/document/owner;
- `UiRouteStackId`: one stack owned by an exact game/player/audience scope;
- `UiRouteOperationId`: one idempotent transactional navigation request;
- `UiPresentationLayerId`: one band-local route/layer generation;
- `UiPresentationPlanId`: immutable per-view resolved composition generation.

Serialized route definitions reference stable route/document/style/binding/transition
IDs and typed policies. They never store stack indexes, live route instances,
viewport slots, editor tabs, ImGui IDs, native window/surface handles, backend z
values or transition cursors.

Stack positions/handles are process-local and generation checked. A pop/replace/
completion names expected stack/top/route generations; stale or duplicate operations
fail/idempotently return their prior result and cannot target a reused slot.

### 3. Core presentation bands have fixed order and roles

Core bands, low to high, are:

```cpp
enum class UiPresentationBand : std::uint8_t {
    World,
    Hud,
    Screen,
    Overlay,
    Modal,
    Loading,
    Debug,
};
```

| Band | Role | Default owner/audience/input |
|---|---|---|
| `World` | World-space canvases and scene/view annotations | Usually Scene/Player; view/depth policy; no screen-stack takeover |
| `Hud` | Persistent player/game status presentation | Player or GameInstance; non-exclusive unless declared control handles input |
| `Screen` | Full-page routes such as menu, inventory, map, pause | GameInstance/Player/Scene; stack navigation and route focus |
| `Overlay` | Toasts, prompts, subtitles, temporary panels | Any compatible owner; non-exclusive by default |
| `Modal` | Confirmation/blocking route above associated stacks | Any compatible owner plus ADR-078 Viewport/Player/GameInstance exclusivity |
| `Loading` | Boot/scene transition/fatal recovery cover | GameInstance owner; host/application controlled; global cover policy |
| `Debug` | Development/diagnostic overlays | GameInstance/Viewport in admitted non-shipping/diagnostic profiles; non-interactive by default |

Native/host-critical dialogs remain outside Runtime UI and above its input stack.
The fixed band order is product semantics, not renderer/backend convenience. A
package may register a finite sublayer within an allowed band through a versioned
namespace/permission, but cannot insert above Loading/Debug, redefine core order or
gain modal/global input authority merely by choosing a band.

Within one band/view, stable order is game-instance policy group, audience/player
order, route stack depth, declared bounded band-local order, element paint order and
stable IDs. Pointer address, hash/load completion, native window order and backend
batch order never break ties.

### 4. Route stacks are scoped, with explicit cross-stack arbitration

One game runtime may own:

- a game-instance route stack for global/persistent screens;
- one or more per-player route stacks;
- scene-local stacks tied to exact `SceneRuntimeId` generations;
- viewport-local stacks tied to exact attachments;
- a host-controlled Loading stack and profile-gated Debug layers.

A stack cannot directly pop/replace a route in another stack. Cross-stack effects
are explicit arbitration records:

- a game-instance modal may cover/block selected or all player/viewport stacks;
- Loading may cover every production stack while leaving them alive;
- a player modal covers only that player's admitted stacks unless authorized as
  game-instance scope;
- scene unload retires its stacks/routes but does not pop game/player routes;
- viewport replacement retires only viewport-owned stacks and reattaches foreign-
  owned routes through explicit attachment reconciliation.

Covered stacks retain semantic state/declared update policy but lose input and
presentation eligibility according to the covering route. They are not removed or
reparented. Arbitration is part of one immutable presentation plan generation.

### 5. Route operations prepare then commit atomically

Version 1 operations are `Push`, `Pop`, `ReplaceTop`, `Reset`, `ShowOverlay`,
`Dismiss` and typed cover/suppression requests. RUI-008.2 may implement them but
must preserve this transaction:

```text
Requested -> Preparing -> Ready -> Committing -> Entering -> Active
                 \-> Failed/RolledBack
Active -> Exiting -> Retiring -> Destroyed
```

Preparation resolves the route definition, owner/audience/stack/band policy,
document/style/font/image/binding/input/transition dependencies and capacity in a
private candidate. It does not remove or hide the current route. Commit validates
expected stack/top/owner generations and atomically publishes the new stack plus
presentation/input/transition plan at an ADR-073 safe point.

Failure/cancellation before commit preserves the previous stack/focus/input/
presentation generation. After commit, entry failure follows the route's explicit
rollback, fallback-route or fail-closed policy; no half-visible stack mutation is
published. Operations arriving after the cutoff commit in a later generation.

### 6. Visibility and lifecycle states are not aliases

`UiRouteVisibilityState` distinguishes:

- `Entering`: committed route preparing first eligible presentation;
- `Visible`: active in a presentation plan;
- `Covered`: retained beneath a covering route/band and ineligible for ordinary
  input;
- `Suppressed`: explicitly hidden by product/cinematic/accessibility policy while
  owner/route state remains;
- `Suspended`: host/background/attachment policy holds update and presentation;
- `Exiting`: committed exit transition/lifecycle;
- `Retiring`: no new update/input/presentation; waiting for leases.

Visibility does not create/destroy semantic state, imply modal input, transfer
ownership or reset route history. Update policies while Covered/Suppressed are
explicit: `HoldPresentationOnly`, `UpdateBindings`, or `UpdateAllNonInteractive`.
No covered route receives input or emits ordinary actions. Simulation-bound UI may
observe later committed data when uncovered; it cannot replay hidden input.

HUD/cinematic suppression is reference-counted/owner-token policy rather than a
global boolean. Removal/expiry restores only the affected band/route generation and
does not override a newer suppression owner.

### 7. Input activation follows the resolved route plan and presentation

Each interactive route creates an ADR-078 context naming exact route/stack/band/
audience/viewport/interaction generations. `Hud`, `Screen` and `Overlay` default to
non-exclusive handled-only input; `Modal` declares Viewport/Player/GameInstance
exclusivity; Loading admits only its host-approved cancel/retry/accessibility
actions; Debug is non-interactive unless a development capability explicitly
creates a separate editor/diagnostic context.

Entering routes do not become input eligible until their first matching
`UiInteractionSnapshot` is successfully presented. This prevents navigation/
submission against invisible geometry. Covered/Suppressed/Suspended/Exiting/
Retiring routes are ineligible, release affected capture/text composition, and
preserve/restore focus through ADR-078 route-generation tokens.

A visual band or high draw order never grants exclusivity. Conversely an ADR-078
exclusive modal blocks associated lower contexts even if its visible element does
not handle an action. Route plan and input context publication are one transaction.

### 8. Transitions are bound to exact route generations

Entry/exit/cover/uncover/suppression transitions use ADR-077 `ScreenTransition`
timelines owned by one exact route/stack/layer generation. Transition descriptors
do not own navigation or visibility; route lifecycle selects their gate and consumes
their typed completion/cancellation evidence.

A required finite entry transition may delay `Visible`/input eligibility and a
required finite exit transition may delay `Retiring` only within declared deadlines.
Optional/infinite decorative motion never blocks stack progress. Reduced-motion
policy may resolve a transition to zero duration while completing the same route
transaction in that VariableUpdate.

Replace/pop/owner destruction, route operation rollback, dependency failure,
suspension policy and shutdown cancel old timelines exactly once. A late completion
cannot activate or retire a route generation that has been replaced.

### 9. Loading is a game-instance transition cover, not scene state

Loading routes that must span scene replacement are GameInstance-owned and live in
the host-controlled Loading stack. They may bind immutable progress/status/error/
retry snapshots through explicit providers but cannot poll jobs, block streaming
threads, own scene lifetime or invoke renderer/native presentation directly.

Loading commit can cover current scene/player/viewport stacks before destructive
transition work begins. Removing the cover requires the target route/scene/UI
generation to be active and successfully presented according to policy; otherwise
the last-good loading/recovery route remains. Scene unload cannot destroy it.

Boot/fatal recovery may use a minimal declared dependency profile. Missing required
loading UI in a product profile fails startup/transition according to host policy;
it does not silently reveal partially initialized gameplay. RUI-008.7 owns detailed
startup/progress/retry behavior within this boundary.

### 10. Debug presentation is isolated and non-authoritative

Debug routes/layers are admitted only in development/diagnostics profiles or an
explicit authenticated developer capability. They consume bounded redacted
observability/snapshot data and cannot mutate gameplay/UI/renderer state without a
separate typed authorized command.

Debug draws above Runtime UI production bands for inspection but is non-interactive
and click-through by default. If interactive, it receives a distinct high-priority
editor/diagnostic context and cannot masquerade as a production modal/loading route.
It does not participate in packaged product route history, accessibility claims or
save/restore state unless a product explicitly promotes a feature to a production
band.

Omitted Debug support is a valid shipping composition. It does not create a hidden
ImGui/console overlay, native surface or renderer backend dependency.

### 11. Rendering consumes one immutable per-view presentation plan

Runtime UI resolves owner/audience/stack/band/visibility/attachment state into one
immutable `UiPresentationPlan` per view. The plan contains ordered Horo route/layer/
layout/style/resource snapshot identities, world/screen composition roles, clip and
interaction revisions. It contains no mutable stack, editor widgets, ImGui draw
lists, native surfaces/swapchains, command buffers or backend z handles.

Render extraction uses the plan once; backend batching may combine equal paint
state but cannot reorder across semantic bands/layers, uncover routes, choose a
loading/debug fallback or advance transitions. Successful presentation correlates
the exact plan/interaction revisions before newly entering routes become input
eligible. Failed/skipped presentation leaves prior last-presented interaction
evidence and route input suppressed.

World band participates in declared view/depth passes; screen-space bands compose
at frontend-owned points. Renderer/device/output replacement builds a new plan/
resource generation while old frames retain leases; route semantic state survives
unless its actual owner ends.

### 12. Persistence, save/restore and compatibility are explicit

Route definition/stack policy/cooked descriptors are versioned semantic data.
Runtime stack/route instances, focus/capture, transition cursors, visibility covers,
loading progress and debug layers are transient unless a later save contract
explicitly serializes stable route state.

Restoration resolves stable `UiRouteId`, owner/audience role and declared semantic
state into fresh generation-checked instances; it never restores stack slots,
pointers or old viewport/native identity. Missing/incompatible routes apply explicit
required/optional/fallback migration policy.

Unknown required band, owner/audience, operation, visibility/update, input,
transition or cover policy rejects and requests recook/migration. Package sublayers
must name a compatible registered namespace/permission and cannot alter core band
order.

### 13. Errors, limits and shutdown are typed

Errors follow ADR-008 with stable reason codes for unknown/duplicate/stale route/
stack/operation/layer/plan, owner/audience/viewport mismatch, forbidden band/scope,
invalid order/cover/modal/visibility/transition, missing dependency/fallback,
capacity/deadline, cancellation, presentation failure, version mismatch and
shutdown. Context includes bounded route/stack/owner/audience/band/generation
evidence without user text or native/editor/backend state.

Limits cover stacks/routes/layers per owner/view, operation queue, preparation work,
band-local order, simultaneous covers/modals/loading/debug layers, transition
blockers, retained plans/generations and diagnostics. Exhaustion rejects/backpressures
before commit or retains the last-good route/cover; it never partially mutates a
stack or drops a required loading/modal barrier.

Shutdown closes route operations/input, cancels preparation/transitions, commits
all stacks/routes to Exiting/Retiring without admitting callbacks, publishes final
empty plans, waits input/render/resource/provider leases, then destroys route state
before Runtime UI dependencies disappear. Repeated/partial shutdown is idempotent.

### 14. Verification is part of the contract

Required coverage includes:

- GameInstance/Player/Scene/Viewport ownership crossed with every compatible band,
  no persistence flag lifetime extension and no attachment ownership transfer;
- stable route/stack/operation/layer/plan IDs, stale/duplicate operations and slot
  reuse;
- exact core band/order and deterministic game/audience/stack/local/paint tie order;
- independent game/player/scene/viewport stacks and game modal/loading cross-stack
  cover without destroying covered routes;
- Push/Pop/Replace/Reset/overlay/dismiss prepare/commit/rollback/fallback and
  dependency/capacity/cancellation failure;
- Entering/Visible/Covered/Suppressed/Suspended/Exiting/Retiring update/input/focus/
  capture semantics and reference-counted suppression restoration;
- first-successful-presentation input gate, exclusive modal blocking and
  failed/skipped presentation;
- required/optional/zero-duration/reduced-motion entry/exit transitions, deadlines,
  replacement and stale completion;
- game-instance loading across scene unload/replacement, last-good recovery and no
  blocking job/native/renderer ownership;
- Debug omitted/non-interactive/authorized-interactive profiles and no production
  authority or hidden ImGui path;
- multi-player/split-screen/multi-view route plans, viewport replacement, world/
  screen composition and backend/device/output replacement leases;
- malformed/version-skewed policies, package sublayer permissions, limits,
  redacted diagnostics, save/restore migration and repeated shutdown.

Property tests generate route stacks/operations/covers and verify owner lifetime,
plan ordering and rollback. Screenshot/video tests may qualify presentation later,
but cannot replace semantic stack/plan/input/lifecycle tests.

## Consequences

Persistent menus, per-player HUDs, scene/viewport overlays, modals, loading covers
and debug presentation coexist without conflating lifetime, z-order, input and
navigation. Split-screen stacks remain independent while global modal/loading
policy can cover them transactionally, and Renderer receives one portable plan.

The cost is multiple explicit dimensions, generation-scoped route/stack/operation/
plan identity, prepare/commit/rollback, fixed bands, cover/suppression policy and
lease-based retirement. Content cannot rely on arbitrary z integers or one global
stack.

## Rejected Alternatives

### Put persistence, player/scene/viewport scope and visual layer in one enum

Rejected because lifetime, audience and composition are orthogonal. Combining them
prevents valid pairs and lets visual moves change ownership.

### Use arbitrary integer z-order across all Runtime UI

Rejected because modal/loading/debug priority and package permissions become
content/backend order rather than typed product policy.

### Keep one process-global route stack

Rejected because split-screen players, scenes, viewports and multiple game runtimes
need independent navigation/lifetime with explicit global arbitration.

### Let each viewport own every route it displays

Rejected because persistent game/player UI may attach to multiple/replacement
viewports without transferring semantic state.

### Mutate the active stack before dependencies are ready

Rejected because failure leaves partial visibility/input/focus state. Operations
prepare privately and commit atomically with rollback/fallback.

### Make visibility equivalent to route lifetime

Rejected because covered/suppressed HUD/screens must retain owner state and restore
without recreation, while destroyed owners must retire even if still visible.

### Activate route input before first presentation

Rejected because invisible geometry/focus could consume navigation or submit. Input
starts only after a matching interaction revision is presented.

### Make Loading scene-owned

Rejected because it must survive scene unload/replacement and cover failure/recovery.
Transition loading is GameInstance-owned host-controlled presentation.

### Implement Debug as an always-present ImGui overlay

Rejected because packaged/headless/runtime renderer compositions cannot depend on
editor GUI state and debug must not gain hidden input or mutation authority.
