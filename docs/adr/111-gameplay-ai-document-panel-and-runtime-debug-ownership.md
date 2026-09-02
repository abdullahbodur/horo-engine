# ADR-111: Gameplay AI Document, Panel and Runtime-Debug Ownership

- **Status**: Proposed
- **Date**: 2026-09-02
- **Supersedes**: None
- **Scope**: Blackboard, decision-graph and environment-query asset document routes; graph commands, history, save/conflict, compilation diagnostics, live runtime inspection, debug commands, provider extensions, presentation lifetime and lifecycle
- **Issue**: [GAI-006.1](https://github.com/abdullahbodur/horo-engine/issues/1370)
- **Jira**: [HORO-1370](https://horo-engine.atlassian.net/browse/HORO-1370)
- **Related**: [ADR-013](013-environment-query-ownership-item-and-scoring-model.md), [ADR-021](021-gameplay-ai-ownership-scheduling-and-behavior-boundary.md), [ADR-022](022-ai-fixed-tick-order-authority-and-simulation-budget.md), [ADR-025](025-ai-decision-assets-and-gameplay-behavior-boundary.md), [ADR-056](056-external-editor-ui-boundary.md), [ADR-110](110-navigation-editor-surface-and-command-ownership.md)
- **Normative documents**: [Navigation and AI Architecture](../architecture/runtime/navigation-and-ai-architecture.md), [Editor Document Model](../architecture/editor/editor-document-model.md), [Editor Panel Host](../architecture/editor/editor-panel-host.md), [Gameplay Behavior Authoring](../architecture/extensions/gameplay-behavior-authoring.md)

## Context

Gameplay AI has durable blackboard schemas, behavior-tree/state-machine/utility
graphs and environment-query templates, plus compiled plans and scene-owned live
instances. The editor has shared multi-document and graph-surface contracts, but no
decision maps every AI asset to a route or separates authoring state from live task,
node, blackboard and query state.

A graph widget must not become semantic graph storage. A runtime panel must not own
the task it displays, cancel simulation when closed or copy a live blackboard into
authoring state. Provider extensions must not receive ImGui, mutable ECS/runtime
objects or permission to bypass compile and safe-point commands.

## Decision

### 1. Every AI asset opens an explicit document route

| Asset | Route | Semantic editor | Cooked output |
|---|---|---|---|
| `BlackboardSchemaAsset` | `BlackboardSchemaDocumentRoute` | Typed key/type/default/category table | Immutable schema descriptor |
| `BehaviorTreeAsset` | `DecisionGraphDocumentRoute{BehaviorTree}` | Shared graph surface plus behavior-tree validator | `CookedBehaviorTreePlan` |
| `StateMachineAsset` | `DecisionGraphDocumentRoute{StateMachine}` | Shared graph surface plus transition validator | `CookedStateMachinePlan` |
| `UtilityAiAsset` | `DecisionGraphDocumentRoute{Utility}` | Shared graph surface plus scorer validator | `CookedUtilityPlan` |
| `EnvironmentQueryTemplate` | `EnvironmentQueryDocumentRoute` | Shared staged graph/list surface plus EQS validator | `EnvironmentQueryPlan` |

The editor document host owns one session/revision/history/dirty/save/recovery/
external-conflict lifecycle per open asset. Asset Registry `AssetId` is durable
identity; tab index, path, graph-widget ID and runtime plan pointer are not.
`SceneDocument` separately owns agent-component references to these assets.

Opening the same asset focuses its existing session. Cross-asset edits use the
EDT-003 multi-document transaction contract: pin every session/revision, validate a
complete candidate, commit correlated semantic history entries or change none.
Saving one document never silently saves dependencies.

### 2. Shared graph widgets are presentation adapters

Decision and EQS graph documents reuse `INodeGraphSurface`, `GraphViewSnapshot`,
stable Horo graph/node/pin/link IDs and drained `GraphEditCommand` intent. The
widget owns selection, pan/zoom, drag and context-menu state only. Subsystem document
controllers validate node types, pins, cycles, references, blackboard bindings,
paradigm rules and bounded payloads before an editor command commits.

Node placement, routing bends, comments and view state follow the shared metadata
policy; semantic topology remains in the source asset. Widget/native library types
never enter assets, commands, cook output or runtime. Undo/redo stores semantic
deltas, not widget memory snapshots.

Blackboard tables use shared editor field/table components and the same command
contract. Renaming/removing a key is a schema edit; updating dependent documents is
a deliberate multi-document migration, never a string search or runtime mutation.

### 3. Compilation is derived and revision-correlated

Document edits invalidate compilation currentness but do not modify an active play
instance. Compilation captures immutable source and dependency revisions, runs
through the application/cook capability and publishes diagnostics/cooked plans only
if the capture remains current. Worker completion cannot commit a document command.

Compile success does not clear dirty state; save success does not imply compile
success. A failed/stale compile keeps the last valid cooked plan identifiable but
cannot claim current. Applying a compatible plan to PIE uses the runtime's staged
safe-point replacement policy; incompatibility restarts/rejects according to
ADR-025, never through a graph-tab callback.

### 4. The Gameplay AI panel is read-only by default

A dockable `GameplayAiTab`, closed by default, projects selected world/agent,
blackboard values, active plan/node stacks, task status, perception summary, EQS
requests/results, budgets and typed failures. It receives document-open commands,
bounded diagnostic queries and an `IGameplayAiInspectionQuery`; no mutable runtime,
ECS, blackboard, task or provider pointer crosses the boundary.

Inspection results name Scene/runtime incarnation, fixed tick, agent generation,
plan/schema asset and runtime-plan revisions. Pages/snapshots are immutable, bounded
and carry truncation/staleness evidence. Replacement or slot reuse clears selection
rather than retargeting a native index.

Hidden panels perform no polling, graph-layout, paging or debug extraction. Closing
releases subscriptions and snapshot leases only. It cannot cancel a task, EQS query,
simulation, PIE session or runtime provider. Reopening queries current authority;
the panel is never a runtime-state cache of record.

### 5. Debug commands cross an explicit safe-point seam

Read-only inspection requires no mutation permission. Pause/step selected AI,
request reevaluation, set a development blackboard override, abort a task or run an
EQS probe are distinct typed developer commands, individually capability/build/
session/authority gated. They capture expected world/agent/plan/schema generations,
enter the runtime command queue and commit only at the owning fixed-tick safe point.

UI callbacks, EditorDataBus subscribers and workers cannot apply a command directly.
Results are typed and correlated; stale, forbidden, unsupported, capacity and
shutdown outcomes remain distinct. Shipping/server-private policy may omit all
mutation and redact inspection. A debug override is transient runtime state and is
never written into a source asset unless the user invokes a separate validated
editor command after explicit review.

### 6. Providers and extensions contribute data, not authority

AI node, sense, planner or EQS providers publish inert typed descriptors: stable
type IDs, pins/properties, localization keys, icons, validation/capability metadata
and optional host-rendered bounded diagnostic fields. Creating/validating a
descriptor performs no registration, runtime lookup or lifecycle callback.

External UI follows ADR-056 host-rendered schema/action contracts. It receives no
`IWorkspacePanel`, ImGui context, mutable document, live blackboard/task, provider
object or service locator. Extensions cannot replace core command validation,
publish cooked plans, issue hidden debug commands or retain callbacks after module
detachment. Missing providers preserve opaque source data and report typed
unavailable nodes; they do not reinterpret or delete it.

### 7. Presentation uses shared editor systems

AI document/panel/modal surfaces use the editor design system, stable `UiText`
localization keys, semantic tokens, shared graph adapter and standard focus/input
rules. Asset/node/type IDs and runtime diagnostics remain technical data. New copy
updates aligned `en-US`/`tr-TR` catalogs and covers long/localized text, narrow
docks, zoom, selection, disabled/loading/stale/error and keyboard states.

Creation, migration/conflict and destructive confirmation are transient modals.
Routine authoring is a document; live inspection is a panel. Modal/tab/panel closure
never controls simulation lifetime.

### 8. Notifications follow committed authority

Document owners publish bounded source-revision invalidation after commit. Cook
owners publish plan/diagnostic revisions. Runtime exposes inspection-generation
invalidation. Surfaces query the authority for full state; EditorDataBus is not a
command channel and subscriber order is irrelevant.

PIE owns all live brains, blackboards, tasks, perception memories and EQS requests.
Editor shutdown detaches surfaces/subscriptions and leases before destroying their
query services; runtime shutdown independently closes command admission, cancels
runtime work and destroys scene-owned state. No UI destructor performs runtime
cleanup.

### 9. Qualification covers ownership and lifecycle

Required evidence includes:

- each asset route, duplicate-open focus, independent dirty/save/recovery/conflict
  and missing-provider opaque-data retention;
- graph/table command apply/revert, semantic validation, cross-asset transaction
  all-or-nothing behavior and no widget-type persistence;
- current/stale compile and plan replacement with document/runtime revisions;
- live inspection paging/redaction/truncation, agent/world reuse and stale leases;
- hidden/closed panel doing no work and never cancelling task/query/simulation;
- every debug command permission, safe-point, stale/capacity/shutdown result and no
  direct widget/worker mutation;
- extension attach/detach, denied action, unavailable provider and no ImGui/native/
  runtime object leakage; and
- shared graph/design/localization behavior across long text, focus and error states.

## Consequences

### Positive

- Every AI source asset, cooked plan and live instance has one owner.
- Decision/EQS editors reuse the shared graph and multi-document contracts.
- Runtime debugging is useful without transferring task/node lifetime to UI.
- Provider extensions remain typed, bounded and safe for headless products.

### Costs

- Each asset kind needs a semantic document controller and validator adapter.
- Runtime inspection/debug requires generation-safe snapshots and commands.
- Cross-schema migrations require real multi-document transaction support.

## Rejected Alternatives

### One monolithic AI editor tab owns every asset and runtime instance

Rejected because document/history/conflict and scene-runtime lifetimes differ.

### Let graph widgets validate or serialize gameplay semantics

Rejected because the shared widget is presentation-only and cannot own subsystem
schemas, cook compatibility or runtime rules.

### Bind live task/node pointers to tree widgets

Rejected because PIE replacement, parallel tasks and panel closure would create
dangling identity and accidental cancellation.

### Apply blackboard edits directly while inspecting

Rejected because runtime mutation requires permissions, generations and a fixed-
tick safe point; authoring changes require document commands.

### Allow arbitrary provider ImGui panels

Rejected because they bypass host design/localization, permissions, lifecycle and
backend-neutral boundaries.
