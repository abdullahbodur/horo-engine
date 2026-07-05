# GUI Screen Host Architecture

## Purpose

This document defines top-level screen navigation and lifetime inside
`HoroEditor`, including Welcome, Project Browser, project creation, onboarding,
and the Editor Workspace.

Screens are distinct from editor panels, tabs, modal workflows, and widget
popups.

## Core Decisions

- `HoroEditor` is one process and one GUI application.
- A screen owns the application content area for one route.
- Screen navigation is direct typed host coordination, not a data-bus command.
- Only the active screen receives interactive input.
- Screens receive narrow application capabilities and do not own business
  state already owned by application services.
- Editor Settings and Build & Release are editor modals, not top-level screens.
- Navigation with dirty or running work must pass an explicit leave decision.

## Surface Taxonomy

| Surface | Placement | Lifetime |
|---|---|---|
| Screen | Replaces the application content route | Until navigation |
| Editor panel/tab | Inside persistent editor layout | Editor session |
| Modal workflow | Above active editor workspace | Temporary exclusive workflow |
| Widget popup | Attached to one surface | Short interaction |

Screens never appear in `EditorPanelHost`. Editor workflow modals never replace
the application route.

## Ownership

```text
HoroEditorApp
  +-- GuiScreenHost
  |     +-- WelcomeScreen
  |     +-- ProjectBrowserScreen
  |     +-- ProjectCreationScreen
  |     +-- EditorWorkspaceScreen
  |
  +-- Application Services
  +-- Engine Data Bus
```

`GuiScreenHost` owns the active screen object and navigation state.
`EditorWorkspaceScreen` composes `EditorLayer`, which owns panel and modal hosts.

## Route Model

```cpp
enum class GuiRouteKind {
    Welcome,
    ProjectBrowser,
    ProjectCreation,
    EditorWorkspace
};
```

`RouteParameters` is a closed variant of route-specific parameter structs, not a
string map:

```cpp
struct WelcomeRouteParameters {};
struct ProjectBrowserRouteParameters {};

struct ProjectCreationRouteParameters {
    std::optional<TemplateId> initialTemplate;
};

struct EditorWorkspaceRouteParameters {
    ProjectId projectId;
    std::optional<ProjectPath> initialScene;
};

using RouteParameters =
    std::variant<WelcomeRouteParameters,
                 ProjectBrowserRouteParameters,
                 ProjectCreationRouteParameters,
                 EditorWorkspaceRouteParameters>;

struct GuiRoute {
    GuiRouteKind kind;
    RouteParameters parameters;
};
```

The host validates that the parameter alternative matches `GuiRouteKind` and
that IDs and paths are structurally valid before constructing a destination
screen. The destination then performs semantic validation through application
services during `OnEnter()`, such as checking project existence, trust, access,
or format compatibility. Invalid parameters return a typed navigation error and
leave the active route unchanged.

## Screen Contract

```cpp
enum class LeaveRequirementKind {
    DirtyDocument,
    UnsavedDraft,
    RunningOperation,
    RecoveryDecision,
    NativeDialogCompletion
};

enum class LeaveAction {
    Save,
    Discard,
    CancelOperation,
    KeepRunning,
    Wait,
    Stay
};

struct LeaveRequirement {
    LeaveRequirementKind kind;
    LeaveSubjectId subject;
    LeaveRequirementRevision revision;
    std::vector<LeaveAction> allowedActions;
};

enum class LeaveDisposition {
    Allow,
    Deny,
    RequireResolution
};

struct LeaveDecision {
    LeaveDisposition disposition;
    std::optional<LeaveRequirement> requirement;
};

struct ApplicationCloseTarget {};

struct LeaveTarget {
    std::variant<GuiRoute, ApplicationCloseTarget> value;
};

struct LeaveResolution {
    LeaveSubjectId subject;
    LeaveRequirementRevision revision;
    LeaveAction action;
};

enum class LeaveErrorCode {
    StaleSubject,
    ActionNotAllowed,
    OperationFailed
};

struct LeaveError {
    LeaveErrorCode code;
    std::vector<Diagnostic> diagnostics;
};

class GuiScreen {
public:
    virtual ScreenId Id() const = 0;
    virtual Result<void> OnEnter(const GuiRoute&) = 0;
    virtual void OnUpdate(float dt) = 0;
    virtual void Draw() = 0;
    virtual LeaveDecision CanLeave(const LeaveTarget& target) const = 0;
    virtual Result<LeaveDecision, LeaveError>
    ResolveLeave(const LeaveTarget& target,
                 const LeaveResolution& resolution) = 0;
    virtual void OnLeave() = 0;
};
```

An `Allow` decision permits the transition. `Deny` keeps the current route
active without presentation. `RequireResolution` identifies one typed
requirement and the allowed actions, such as save, discard, cancel operation,
keep durable work running, wait for a native dialog, or stay on the current
screen. The requirement contains stable identities, not owning pointers or
presentation callbacks.

`OnEnter()` succeeds before the route becomes active. Destination construction
and entry are staged while the current screen remains active. A candidate
screen owns all partially initialized resources through RAII and must not
invalidate the current screen during entry. Application operations that require
exclusive state return prepared handles whose commit occurs only at route
commit.

If construction or `OnEnter()` fails, the candidate is destroyed, prepared work
is rolled back or abandoned through its owning service, `OnLeave()` is not
called on the current screen, and the active route remains unchanged. A failure
that invalidates process-wide window or renderer operation uses the fatal host
path rather than ordinary navigation recovery.

`OnLeave()` executes once after leave permission and before destruction.

## Navigation

```cpp
class GuiScreenHost {
public:
    Result<void, NavigationError> Navigate(GuiRoute destination);
    Result<void, NavigationError> RequestCloseApplication();
    const GuiRoute& ActiveRoute() const;
};

enum class NavigationErrorCode {
    InvalidRouteParameters,
    Busy,
    LeaveDenied,
    Cancelled,
    LeaveResolutionLimitExceeded,
    DestinationConstructionFailed,
    DestinationEntryFailed
};

struct NavigationError {
    NavigationErrorCode code;
    std::vector<Diagnostic> diagnostics;
};
```

`LeaveSubjectId` is a validated opaque identity that correlates the requirement
with the document, draft, job, recovery flow, or native dialog that produced it.
It is stable only while that blocker exists within one screen instance; it is
not a process-global or persistent identity. The host binds each requirement to
the current `ScreenInstanceId`, `NavigationAttemptId`, and
`LeaveRequirementRevision`, then passes the subject and revision back to the
owning screen without interpreting them.

`ScreenInstanceId` is unique for one constructed screen lifetime.
`NavigationAttemptId` is unique for one accepted navigation or application-close
request and expires when that request commits, fails, or is cancelled.
`LeaveRequirementRevision` increases when the state or allowed actions of the
same blocker change.

`RequireResolution` must contain one requirement; `Allow` and `Deny` do not.
`CanLeave()` is a non-mutating query. After presenting an allowed action, the
host calls `ResolveLeave()`. The screen verifies the subject and action, commits
the corresponding save, discard, cancellation, detach, or recovery operation
through its owning service, and returns `Allow`, `Deny`, or a new requirement.
Failed resolution returns a typed `LeaveError` and keeps the current route
active.

A resolution chain is allowed because one blocker may reveal another, such as
saving a dirty document before resolving a running operation. Each successful
`ResolveLeave()` must return `Allow`, `Deny`, or a requirement with a new subject
or higher requirement revision. Repeating the same unresolved requirement
without progress is an error. The host also enforces a small configured maximum
resolution count per navigation attempt; exceeding it cancels the attempt with
a structured diagnostic instead of presenting an unbounded loop.

Leave denial and user cancellation are typed non-success outcomes and do not
masquerade as entry failures. Callers branch on `NavigationErrorCode`, never on
diagnostic text.

Navigation requests commit at a GUI frame boundary. Reentrant route replacement
during `Draw()` is not allowed. `Navigate()` called during update, draw, or a
screen callback validates and records one deferred request; it never replaces
the active screen on the current call stack. A second request while navigation
or leave resolution is pending returns `NavigationErrorCode::Busy`.

At the frame boundary, the host resolves leave policy, prepares the destination,
and commits one route change. Failed validation, denied leave, failed entry, and
cancelled confirmation clear the pending request without changing the active
route.

A successful route commit is ordered:

1. validate that the pending request and active route are still current
2. obtain final leave permission
3. construct the candidate and complete staged `OnEnter()`
4. call `OnLeave()` on the previous screen
5. commit prepared exclusive application state
6. replace the active route and establish destination focus
7. destroy the previous screen
8. publish `GuiRouteChangedEvent`

No observer sees the destination route before its entry and required application
state commit succeed.

All fallible preparation and validation completes before step 4. A prepared
exclusive-state commit is no-fail by contract. If a destination cannot provide
that guarantee, preparation fails and the transition is rejected before
`OnLeave()` runs.

Navigation is not published as a command on `EngineDataBus` or
`EditorDataBus`. After commit, the host publishes one bounded route-changed
notification for observers such as telemetry or window title presentation.

```cpp
struct GuiRouteChangedEvent {
    GuiRouteKind previousKind;
    GuiRouteKind currentKind;
    GuiRouteRevision previousRevision;
    GuiRouteRevision currentRevision;
};
```

`GuiRouteChangedEvent` is a process-level presentation/lifecycle notification
defined with the GUI host event types and published on `EngineDataBus`. It does
not carry route parameters, project paths, or command semantics.

Route identity is the route kind plus canonical typed parameters. Navigating to
the identical active route is a successful no-op: it does not run leave guards,
replace the screen, increment route revision, or publish an event. Navigating
between routes with the same kind but different parameters is a real
transition. In that case `previousKind == currentKind`, while the route
revisions differ so observers can distinguish the committed route instances
without receiving sensitive parameters.

`GuiRouteRevision` is a host-owned monotonic value incremented once for every
committed route instance. It is runtime correlation metadata, not persisted
navigation state.

## Leave Guards

A screen may require a decision before leaving because of:

- dirty scene documents
- project creation drafts
- running operations tied to the screen lifetime
- unresolved recovery flow
- active native dialog

The screen returns a typed leave requirement. The host presents the required
confirmation through the appropriate modal/dialog mechanism and retries
navigation with the resolved decision.

The navigation host does not guess whether work should be saved, cancelled, or
kept running.

`RequestCloseApplication()` uses the same `CanLeave()` and leave-resolution path
with `ApplicationCloseTarget`. Native window-close events call this method
rather than terminating the process directly. The application exits only after
the active screen allows leave and required save, discard, cancellation, detach,
or recovery decisions commit successfully.

An active native dialog produces `NativeDialogCompletion`. Ordinary navigation
and application close remain pending until the dialog returns a typed result or
cancellation; the host does not destroy or programmatically dismiss a platform
dialog that may still call back into the active screen. The request is then
revalidated before leave resolution resumes. Process-fatal or emergency
termination follows the platform emergency policy and does not attempt ordinary
interactive navigation.

## Editor Workspace Screen

Entering the editor workspace:

1. validate and open the project through application services
2. create the editor workspace controller
3. restore workspace state
4. construct `EditorLayer`, panel host, tabs, and modal host
5. activate the route after required initialization succeeds

Leaving:

1. resolve open modal and dirty-document policies
2. stop or detach durable jobs according to their task-group, cancellation, and
   operation-handle contracts
3. persist workspace state
4. destroy GUI surfaces and subscriptions
5. close the editor session
6. close or retain the project according to destination requirements

The screen does not duplicate project-open or scene-save business logic.
Job ownership, cancellation, detach behavior, and shutdown joins follow
[Concurrency And Job System](../foundation/concurrency-and-jobs.md).

## Screen State

Screen-local presentation state includes:

- search text
- selected project-browser item
- local wizard step
- transient validation presentation
- focus target

Durable project and user state is committed through owning services.
Navigation state is not used as an untyped global store.

## Input And Focus

The active screen owns the base GUI interaction scope. Inactive screens are not
drawn interactively and do not receive input.

When the editor route has an open modal, modal scope supersedes workspace scope
without changing the active route.

Focus restoration occurs within the active screen after child modal or native
dialog closure. Route changes establish a new initial focus target.

## Data Bus Participation

Screens may subscribe to `EngineDataBus` for process-level lifecycle
notifications relevant to presentation. The editor workspace obtains its
session-local bus from `EditorWorkspaceController`.

Screens do not publish application commands. They call typed use cases and
observe committed results.

## Failure Presentation

Entry failures are mapped from typed errors:

- invalid or missing project remains in Project Browser with diagnostics
- recoverable editor initialization failure offers retry or return
- fatal renderer/window failure enters the host fatal presentation path

Logs are supporting evidence and are not parsed to choose navigation.

## Testing

Required tests cover:

- screen enter/leave exactly once
- failed destination entry preserving current route
- failed or partial destination entry releases candidate resources and does not
  call `OnLeave()` on the active screen
- deferred navigation requested during draw
- a second navigation request returns `Busy` while transition or leave
  resolution is pending
- invalid route parameter alternatives and semantic validation failures preserve
  the active route
- dirty-document leave guard
- invalid leave subject or disallowed action cannot resolve a leave requirement
- stale subject revisions and subjects from another screen instance or
  navigation attempt are rejected
- chained leave requirements must make progress and stop at the configured
  resolution limit
- running-job leave policy
- application close passes through the same leave guards as route navigation
- active native dialog defers navigation and application close until completion
- stale deferred navigation is revalidated after native dialog completion
- editor workspace construction and destruction order
- inactive screen input exclusion
- modal scope without route replacement
- one route-changed notification is published after commit and none is
  published for rejected or failed navigation
- identical-route navigation is a no-op, while same-kind navigation with
  different parameters commits with distinct route revisions
- focus initialization and restoration
- project operations shared with CLI and MCP

## Related Documents

- [System Design](../foundation/system-design.md)
- [Editor Panel Host](./editor-panel-host.md)
- [Editor Modal Host](./editor-modal-host.md)
- [Editor Document Model](./editor-document-model.md)
- [Input Architecture](../runtime/input-architecture.md)
- [Project Model](./project-model.md)
- [Platform Abstraction](../foundation/platform-abstraction.md)
- [Concurrency And Job System](../foundation/concurrency-and-jobs.md)
