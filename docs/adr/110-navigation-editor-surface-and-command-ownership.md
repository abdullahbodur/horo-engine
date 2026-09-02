# ADR-110: Navigation Editor Surface and Command Ownership

- **Status**: Proposed
- **Date**: 2026-09-02
- **Supersedes**: None
- **Scope**: Navigation definition and Scene authoring documents, commands/history, bake submission and operation projection, dockable authoring/diagnostic/runtime inspection surfaces, transient modals, viewport overlays, provider/extension contributions, localization, design-system use, persistence and lifecycle
- **Issue**: [NAV-007.1](https://github.com/abdullahbodur/horo-engine/issues/1291)
- **Jira**: [HORO-1291](https://horo-engine.atlassian.net/browse/HORO-1291)
- **Related**: [ADR-010](010-job-waiting-and-operation-store-ownership.md), [ADR-018](018-command-registration-permissions-threading-and-packaged-build-policy.md), [ADR-054](054-extension-and-package-authority-boundary.md), [ADR-056](056-external-editor-ui-boundary.md), [ADR-104](104-default-navigation-provider-and-recast-detour-adoption.md), [ADR-105](105-navigation-asset-and-scene-ownership-boundary.md), [ADR-106](106-navigation-bake-ownership-transaction-and-cache.md), [ADR-107](107-navigation-query-consistency-and-snapshot-ownership.md), [ADR-108](108-dynamic-overlay-carving-and-tile-rebuild-policy.md), [ADR-109](109-avoidance-crowd-and-renderer-independent-budget.md)
- **Normative documents**: [Navigation and AI Architecture](../architecture/runtime/navigation-and-ai-architecture.md), [Editor Document Model](../architecture/editor/editor-document-model.md), [Editor Panel Host](../architecture/editor/editor-panel-host.md), [Editor Modal Host](../architecture/editor/editor-modal-host.md), [Editor Data Bus](../architecture/editor/editor-data-bus.md), [Editor UI Design System](../architecture/editor/ui-design-system.md), [Localization](../architecture/editor/localization.md)

## Context

Navigation editing crosses several existing authorities. ADR-105 separates durable
`NavigationDefinition` and Scene intent from immutable cooked artifacts and runtime
topology. ADR-106 gives every bake entry point one application-owned operation.
ADR-107 makes runtime inspection read immutable generation-scoped snapshots. The
editor architecture separately distinguishes document tabs, dockable panels,
exclusive transient modals, typed commands and post-commit notifications.

Without a surface decision, a large “Navigation Bake” window could become a second
owner for definition fields, Scene components, jobs, generated artifacts and live
runtime state. Closing it could accidentally cancel work; an Apply button could
mix an undoable authoring change with non-undoable artifact publication; a provider
plugin could receive ImGui/runtime pointers and bypass backend-neutral contracts.

Navigation also needs two different views: authoring/bake readiness in edit mode
and bounded live inspection during play. They may share presentation primitives,
but they cannot share mutable authority or infer currentness from whatever a panel
last displayed.

This ADR assigns every navigation editor surface and command to the existing
document, application-operation, runtime-query and GUI hosts.

## Decision

### 1. Navigation uses existing editor surface categories

| Concern | Surface | Lifetime owner | State authority |
|---|---|---|---|
| `NavigationDefinition` profiles, areas, build/tile and scope policy | Persistent document tab | Editor document/session host | `NavigationDefinitionDocument` command/history/save service |
| Scene sources, modifiers, links, blockers and definition reference | Existing Scene document/Properties/viewport tools | `EditorWorkspaceController` and `SceneDocument` | Typed Scene editor commands/history |
| Readiness, selected definition/scope, bake actions, diagnostics and runtime inspection | Dockable `NavigationTab`, closed by default | `EditorPanelHost` | Query services and application use cases; tab owns presentation only |
| Accepted bake progress/history | Shared Operations tab, with correlated Navigation view | Operation UI projection | Application `OperationStore` / `NavigationBakeService` |
| Create definition, resolve conflict/migration or confirm explicit cancellation | Modal workflow | `EditorModalHost` | Modal draft only; commit through commands/use cases |
| NavMesh/path/agent/obstacle debug drawing | Viewport overlay contribution | Editor viewport presentation owner | Immutable navigation inspection snapshot |

A document tab represents durable editable content. A panel represents a reusable
workspace view. A modal represents a temporary decision/workflow. None is promoted
to an application screen, and the non-normative `navigation-bake.html` mockup does
not override these categories.

### 2. Two authoring documents retain separate truth

`NavigationDefinitionDocument` is an asset-document session rooted at the stable
ADR-105 definition `AssetId`. It owns an immutable-current document state,
`DocumentSessionId`, monotonic revision, semantic history, dirty/saved state,
atomic save, autosave/recovery and external-change conflict handling equivalent to
the Editor Document Model. It edits only definition-owned grounded profiles, area
tables, build/tile parameters and bake-scope policy.

`SceneDocument` remains the only owner of Scene navigation intent: definition
references, explicit collision/source contributors, modifier regions, grounded
links and dynamic-obstacle components with stable object/component/contribution
IDs. Properties, Hierarchy and viewport tools submit ordinary typed Scene commands.
They never mutate the definition asset, generated polygons or live topology.

One gesture cannot secretly write both documents. A deliberate cross-document
change uses an application-coordinated staged transaction that pins both sessions/
revisions, validates all deltas, commits atomically where supported and records one
semantic history entry per affected document with shared correlation. Failure or
staleness changes neither document. If atomic multi-document publication is not
available, the workflow is not offered.

### 3. Authoring changes and bake publication are different commands

Undoable authoring actions use `EditorCommandDispatcher`:

```cpp
SetNavigationDefinitionFieldCommand
SetSceneNavigationDefinitionCommand
AddNavigationSourceCommand
SetNavigationModifierCommand
AddGroundedNavigationLinkCommand
SetDynamicObstacleIntentCommand
```

Commands carry stable document/asset/object identities, expected revisions and
typed values. Apply/revert is all-or-nothing, changes dirty/history state only after
commit and emits the owning document's bounded changed event. UI callbacks return
component results and submit commands; they receive no mutable document reference.

`StartNavigationBake` is not an `EditorCommand` and is not undoable. It submits an
ADR-106 `NavigationBakeRequest` through the narrow application capability and
returns an `OperationId` or typed admission error. Explicit cancel requests use
`IOperationControl`; opening, closing, hiding, docking or destroying a surface does
not submit cancellation.

A successful bake publishes an immutable derived artifact/cache generation and
`current.json` receipt through AssetCook. It does not edit either authoring
document, advance history, clear dirty state or insert generated tiles/polygons into
Scene. Undo/redo does not delete artifacts. Current/stale status is derived by
comparing exact captured source/dependency fingerprints, never by an “Apply” flag in
the panel.

### 4. The Navigation tab is a projection and action launcher

The first-party `NavigationTab` registers through the normal panel registry with a
stable ID, localized label/icon token and bottom-dock fallback placement. It is
closed by default. Its host factory receives only:

- document/selection query and command capabilities;
- definition asset-document open/query capability;
- `INavigationBakeSubmission`, `IOperationQuery` and `IOperationControl`;
- artifact/readiness and bounded diagnostic queries;
- `INavigationInspectionQuery` and viewport-overlay control; and
- `EditorDataBus` subscriptions plus localization/design-system services.

It presents authoring readiness, selected definition/scope/profile, captured versus
current revisions, last-valid artifact identity, bake submission, operation link,
bounded diagnostics and a live-inspection section when a compatible play world
exists. It never opens source/cache files, invokes a builder, writes `OperationStore`,
publishes an artifact, installs topology or calls provider/runtime objects.

Hidden tabs remain attached only for cheap invalidation. They perform no polling,
debug-geometry extraction, provider paging or per-frame metric query. Becoming
visible requests a fresh bounded snapshot. Hiding/closing releases presentation
leases and overlay interest but cannot change authoring, operation or runtime
lifetime.

The shared Operations tab remains the canonical cross-feature operation list. A
Navigation tab may deep-link/select the same `OperationId` and show a focused
projection, but it cannot own a second progress record or infer terminal state from
logs.

### 5. Runtime inspection is immutable and generation-safe

The application composition root exposes a backend-neutral
`INavigationInspectionQuery` over the active runtime's bounded diagnostic snapshot.
It returns Horo world/Scene/topology/overlay/profile generations, coverage, paths,
logical agents/obstacles/links, typed failures, budget counters and neutral debug
primitives. It returns no `dt*` objects, native refs, mutable containers, callbacks
or provider memory views.

Every query names world incarnation, requested sections, paging/output limits and
optional expected generation. Results are immutable leases or owned pages with
truncation/currentness evidence. World replacement, play stop or generation change
returns typed stale/unavailable state; the panel clears selection rather than
retargeting the same native index.

Inspection is read-only by default. Developer actions such as request-path probe,
repath, obstacle test or debug capture, when separately authorized, submit typed
commands through an application/runtime command capability and commit at the
runtime's declared safe point. A widget callback cannot mutate agent, overlay,
topology, provider or Physics state directly.

### 6. Viewport overlays are editor presentation state

The Navigation tab may publish an editor-owned overlay-interest model for walkable
surface, tile bounds, links, paths, agents and logical obstacles. The viewport
adapter queries bounded neutral debug primitives from the same inspection snapshot
and submits ordinary renderer debug geometry after simulation commit.

Overlay enabled state, colors/categories, selected profile and local filters are
bounded workspace presentation state. They are not Scene fields, navigation runtime
configuration or provider flags. Overlay visibility may be persisted in editor
workspace state, but not in the authored Scene/definition or packaged runtime.

Closing/hiding the tab disables its visibility-scoped overlay interest and releases
leases. It does not disable runtime diagnostics globally, alter simulation budget,
stop play or destroy navigation. Headless/NullRenderer products keep identical
runtime state without an overlay surface.

### 7. Modals are limited to transient decisions

`CreateNavigationDefinitionModal` may collect a destination, initial profile set
and scope template, then submit one validated Asset/Application create use case.
Conflict/migration and explicit operation-cancellation confirmation may use focused
root/child modals under `EditorModalHost`.

Routine definition editing, bake settings, operation progress and runtime inspection
are not modal state. A modal draft is transient and cannot mark a document dirty
until a typed command/use case commits. Closing/cancelling the draft has no domain
effect. If a modal requests cancellation of an accepted bake, it must present the
explicit keep-running/cancel/return decision; merely closing the modal keeps the
operation running.

Modals receive narrow capabilities, obey the central interaction gate and cannot
hold document/provider/runtime pointers. Their result callbacks defer any panel/
document navigation until the modal frame boundary.

### 8. Notifications invalidate views after the owner commits

Document owners publish definition/Scene changed events. AssetCook/application
publishes artifact/readiness and operation-revision notifications. Runtime exposes
inspection-generation invalidation. `EditorDataBus` carries bounded editor-session
projections of these facts after commit.

Events contain stable IDs, revision/generation and change kind only. NavigationTab,
Properties and viewport overlays query their owning source for full state. They do
not publish duplicate success events, rely on subscriber order or use the data bus
as a command channel. A late/stale event may cause a redundant query but cannot
overwrite newer presentation.

Workers and transport threads enqueue through existing owner-thread bridges. UI
surfaces update only on the editor thread. Operation or runtime completion never
invokes a tab callback directly.

### 9. Provider UI is data-only and strictly optional

Navigation providers cannot register an ImGui panel, return widget callbacks, access
`EditorLayer`, receive document/runtime mutable state or expose native diagnostic
objects. Their inert descriptors may advertise backend-neutral capability names,
limits, fingerprints and bounded typed diagnostic fields. The host-owned Navigation
tab renders those fields with generic controls/components.

A first-party or external extension may contribute a validated host-rendered UI
schema under explicit navigation extension points such as definition read-only
details or provider diagnostics. ADR-056 applies: the contribution declares stable
module/contribution IDs, localization keys, schema version/size, required read
capabilities and allowlisted action IDs. The host resolves permissions and renders
copied schema/state; external code receives no C++ `IWorkspacePanel`, ImGui,
renderer, provider or service-locator object.

Extensions cannot replace core definition fields, alter Scene command validation,
write bake progress, publish artifacts, mutate runtime state, bypass operation
admission or keep a panel alive after provider/module detachment. Unsupported or
removed contributions disappear with typed diagnostics while the core surface and
stored document data remain valid. Provider absence leaves generic authoring and
last-valid artifact inspection available where their own capabilities permit.

### 10. Shared design and localization systems are mandatory

Navigation surfaces compose shared editor fields, tables, tree/list rows, badges,
buttons, split panes, status/progress and modal shells from the UI design system.
They use semantic tokens and font roles; feature code does not draw custom pills,
hard-code spacing/color/font values or bypass an existing component with raw ImGui.

All visible labels, actions, tooltips, empty/loading/stale/error states, validation
messages and accessibility names use stable `UiText` localization keys in the
editor namespace. Asset names, profile IDs, provider fingerprints, file paths and
error codes remain technical data. Complete messages use typed named arguments,
not concatenated translated fragments.

Implementation changes add structurally aligned `en-US` and `tr-TR` entries and
localization coverage tests in the same change. Layout supports text expansion,
supported UI scales, keyboard/focus traversal, long technical identifiers and
fallback fonts. The old static HTML reference is visual inspiration only and its
hard-coded strings/styles/actions are not implementation contracts.

### 11. Lifecycle order preserves documents, operations and runtime

On document close, dirty/conflict policy runs through the ordinary document host.
Closing a Scene/definition document does not cancel a bake already holding its own
immutable ADR-106 snapshot. Reopening reconstructs currentness from authority
queries and fingerprints.

On play start/stop or world replacement, the Navigation tab retains only workspace
presentation state. It releases inspection leases/subscriptions, clears generation-
bound selections and reconnects only after a new compatible world is active.

Editor shutdown stops new UI action admission, detaches Navigation surfaces and
subscriptions, resolves document save/dirty workflows, requests application
operation shutdown policy, drains runtime inspection leases, then destroys provider
and GUI services in their established owner order. A surface destructor never
performs domain cancellation or publication.

### 12. Qualification covers surface and authority boundaries

Required evidence includes:

- definition and Scene command apply/revert, dirty/save/recovery/external-conflict
  semantics without cross-document or generated-data mutation;
- bake submission before/after unsaved changes, exact captured/current fingerprints,
  success/failure/cancel/supersede and artifact publication without history edits;
- Navigation and Operations tabs projecting the same `OperationId`, surviving
  Navigation tab hide/close/reopen and explicit-only cancellation;
- edit/play/no-world/Null/unsupported-provider runtime inspection with world/
  topology/agent generation replacement, bounded paging and stale lease rejection;
- viewport overlay visibility, hidden-tab no-work behavior and no runtime/lifetime
  effect from panel layout restoration;
- UI callback, worker completion, CLI/MCP and extension action paths all passing
  through the same command/application safe points;
- provider/extension descriptor validation, denied actions, missing/unloaded
  contributor and proof no native/provider/ImGui object crosses the boundary;
- modal draft cancel, create success/failure, conflict and keep-running versus
  explicit-cancel flows;
- `en-US`/`tr-TR` key parity, locale switch, long/expanded text, narrow dock,
  keyboard/focus, disabled/loading/stale/error and supported UI-scale states; and
- shutdown with dirty documents, active bake, active play inspection and pending
  notifications without callback, lease, operation or provider lifetime leaks.

## Consequences

### Positive

- Durable definition, Scene intent, derived artifacts and runtime inspection retain
  separate owners in the editor.
- Bake operations survive surface lifetime and remain shared across GUI/CLI/MCP.
- Runtime inspection is useful without exposing or mutating provider state.
- Navigation UI follows existing document/panel/modal, design and localization
  contracts.
- Provider and extension contributions remain backend-neutral, bounded and
  permissioned.

### Costs

- Navigation needs an asset-document session, panel projection and inspection
  adapter rather than one monolithic bake window.
- Currentness requires exact revision/fingerprint correlation in every projection.
- Host-rendered provider diagnostics are less flexible than arbitrary plugin ImGui.
- Visual and localization qualification spans several surface/lifecycle states.

## Rejected Alternatives

### Put all authoring and bake controls in one Navigation panel

Rejected because a panel would become a competing document, history, operation and
artifact owner, with ambiguous dirty/Apply/close behavior.

### Make bake an undoable editor command

Rejected because bake publishes external immutable derived data, may outlive a
document surface and has operation/cache/locking semantics unlike document history.

### Cancel bake or runtime work when the panel closes

Rejected because accepted work belongs to application/runtime authorities. Surface
lifetime controls only presentation subscriptions and leases.

### Let UI callbacks edit provider or runtime containers

Rejected because it bypasses safe points, generations, threading, rollback and
backend-neutral ownership.

### Let every provider ship an arbitrary ImGui panel

Rejected because it leaks implementation/native lifetime into GUI, bypasses shared
design/localization/permissions and prevents safe unload or headless parity.

### Store generated NavMesh in Scene history after bake

Rejected because cooked artifacts are immutable derived products under ADR-105/106,
not authored Scene truth or undo deltas.

### Make the bake/inspection workflow a permanent modal

Rejected because persistent authoring and monitoring must coexist with the editor
workspace; only temporary creation/conflict/confirmation decisions require an
exclusive modal.
