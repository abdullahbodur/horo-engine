# ADR-056: External Editor UI Boundary

- **Status**: Proposed
- **Date**: 2026-09-02
- **Supersedes**: None
- **Scope**: Third-party editor UI rendering, input, state, localization, accessibility, compatibility, isolation and teardown across declarative, C ABI and out-of-process extension models
- **Issue**: [EXT-003.1](https://github.com/abdullahbodur/horo-engine/issues/103)
- **Jira**: [HORO-103](https://horo-engine.atlassian.net/browse/HORO-103)
- **Parent**: [EXT-003](https://github.com/abdullahbodur/horo-engine/issues/58)
- **Related**: [ADR-055](055-extension-manifest-v1-typed-model.md), [ADR-015](015-accessibility-ownership-typed-transport-and-non-gating-policy.md)
- **Normative documents**: [Extension System](../architecture/extensions/plugin-system.md), [GUI Design System](../architecture/editor/ui-design-system.md), [Editor Panel Host](../architecture/editor/editor-panel-host.md), [Editor Modal Host](../architecture/editor/editor-modal-host.md), [Extension Module Development Guide](../guides/extension-module-development.md)

## Context

Horo needs third-party tabs, panels, settings pages, modal pages, commands and
tool views without making Dear ImGui, SDL, a concrete renderer or editor internals
part of its extension SDK. The GUI design contract already says that feature code
composes Horo-owned components and that ImGui remains an implementation detail.
The extension architecture nevertheless describes a custom drawer path that hands
an in-process module an ImGui callback and lets it draw arbitrary content. That
path would expose toolkit versions, allocators, frame lifetime, global widget
state, input capture and renderer assumptions through an external binary boundary.

The implemented extension C ABI currently exposes asset-importer registration
only. The implemented workspace panel interfaces are internal C++ composition
contracts, not a published third-party ABI. This is the last safe point to choose
one external UI boundary before accidental examples become compatibility promises.

Three models are plausible:

1. host-rendered declarative surfaces;
2. a versioned C ABI that gives modules direct rendering callbacks; or
3. UI/controller work in a separate process.

The decision must preserve theme and DPI behavior, scoped input, localization,
accessibility semantics, headless inspection, deterministic tests and safe
teardown. It must also support interactive tools without calling unknown module
code inside the editor frame or pretending that native in-process code is
sandboxed.

## Decision

### 1. External editor UI is host-rendered typed presentation

Third-party editor surfaces use a versioned declarative Horo UI schema. The host
validates and copies the schema and bounded view state, maps them to Horo design-
system components, owns all rendering and input dispatch, and emits typed actions
back to the extension capability.

```text
validated editor contribution
  -> EditorUiSchemaV1 + declared actions/data bindings
  -> host-owned ExtensionUiSession
  -> Horo design-system component tree
  -> private Dear ImGui/platform/renderer adapters

scoped user interaction
  -> typed ExtensionUiActionRequest
  -> approved backend capability/operation
  -> bounded result or view-state revision
  -> host validates and applies the next snapshot/patch
```

The public schema is backend-, toolkit- and process-neutral. It contains typed
layout nodes, semantic controls, stable IDs, localization keys, accessibility
metadata, value bindings, validation rules, action IDs and bounded specialized-
view descriptors. It contains no drawing function, widget pointer, native window,
GPU resource, dock ID, borrowed editor object or arbitrary property map.

The same schema and action protocol are used by trusted in-process modules and
isolated helpers. Isolation changes the transport, not the UI contract.

### 2. Supported models have distinct responsibilities

| Model | Policy | Responsibility |
|---|---|---|
| Host-rendered declarative/typed UI | Supported and required for embedded third-party editor surfaces | Defines semantics, layout intent, view state and typed actions; Horo owns pixels and interaction. |
| Versioned extension C ABI | Supported for in-process schema publication, state snapshots, actions and lifecycle | Transports bounded copied data and opaque handles; it is not a drawing or C++ widget ABI. |
| Out-of-process controller/helper | Supported when isolation, dependency separation or crash containment is required | Uses the same schema/action model over bounded authenticated IPC; Horo still renders embedded UI. |
| Separate external application window | Explicit opt-in tool launch only | The external application owns its window and accessibility; it is not embedded, docked or represented as a Horo editor surface. |
| Direct ImGui/SDL/native-renderer callback | Forbidden for external packages | Toolkit/global state, ABI, input, accessibility and teardown cannot be contained or kept compatible. |

First-party GUI code compiled into the product may use internal Horo component and
panel interfaces. Those interfaces remain target-private and carry no third-party
compatibility promise. Built-in and external surfaces share visual and behavioral
rules, not a binary widget ABI.

### 3. The UI schema is closed, semantic and capability-negotiated

`EditorUiSchemaV1` is a validated typed tree with bounded depth, node count, text,
choice count, table rows, binary/resource references and serialized state size.
Core node alternatives cover layout, text, buttons, toggles, numeric/text fields,
choices, lists, tables, progress, diagnostics, images and host-owned navigation.
Every interactive node references a declared typed action; it cannot contain code.

Complex tools use registered specialized view schemas such as plot, timeline,
node graph, image/canvas annotation or property inspector. Each schema owns its
data limits, semantic accessibility projection, input vocabulary and performance
budget. An extension cannot request a generic immediate-mode command stream or
upload arbitrary executable shaders as a substitute for a missing view schema.

The host advertises supported UI schema and specialized-view versions during
activation. Required unsupported nodes or features reject the contribution.
Optional unsupported features produce an explicit unavailable/fallback outcome;
they are never silently dropped when doing so could change an action or meaning.

### 4. Rendering, theme and layout are host authority

The GUI host owns:

- design-token and theme resolution, including high-contrast and user overrides;
- DPI scaling, font selection, shaping, bidirectional text and icon rendering;
- component implementation, hover/active/disabled/focus visuals and tooltips;
- docking, panel chrome, modal layering, clipping, scroll and popup placement;
- renderer resources, texture upload, draw ordering and frame lifetime; and
- narrow-width behavior, localization expansion and component test coverage.

Extensions express semantic intent and bounded placement preferences. They do not
select colors, fonts or absolute screen coordinates except where a registered
specialized view explicitly accepts theme-relative data. Package resources such
as icons and images are verified, decoded and uploaded by the host; modules never
receive or return renderer handles.

No extension callback runs during ImGui traversal, draw-list construction or
renderer submission. The frame consumes a host-owned immutable UI snapshot. State
refresh and action handling run through scheduled extension operations outside the
render traversal, with deadlines, cancellation and stale-revision rejection.

### 5. Input is scoped and actions are typed

The host performs hit testing, pointer capture, focus traversal, shortcut
arbitration, drag/drop admission, clipboard policy and modal exclusion. A surface
receives only semantic actions for its active controls and registered specialized
view. It does not receive the global input stream, keys typed in other surfaces,
raw SDL events or unrestricted native window messages.

Action requests contain the contribution/session/node/action IDs, the schema and
view-state revisions, a typed bounded payload, cancellation and an invocation ID.
The host rejects stale, duplicate, unauthorized or structurally invalid actions.
Actions call declared application capabilities or module operations; they do not
mutate project files, registries, selection, layout or modal state directly.

Global shortcuts, file drops, clipboard reads, external URL/process launch and
credential prompts require separate declared host capabilities. Modal pages
cannot close or commit their owning workflow except through the owning modal's
typed result contract.

### 6. Localization and accessibility remain host-verifiable

Visible strings, descriptions, tooltips, errors and accessibility labels use
verified package localization message IDs with typed arguments. The host resolves
locale, fallback, shaping and truncation. Technical text is explicitly tagged and
cannot be used to bypass localization for ordinary user-facing copy.

Every UI node has a semantic role, stable identity, accessible name source,
state/value metadata and focus policy where applicable. Registered specialized
views provide an equivalent semantic tree, keyboard operations and non-color
state cues. The host builds test-visible accessibility snapshots even when the
current native backend cannot expose a complete OS accessibility tree.

An extension cannot hide essential controls inside an unlabeled image, depend on
color alone, trap focus, bypass the active modal scope or replace host focus
restoration. Invalid accessibility structure rejects the surface schema rather
than degrading silently.

### 7. State ownership is explicit

| State | Owner |
|---|---|
| Layout, docking, focus, open/closed state and active modal scope | GUI/editor hosts |
| Control draft values, validation display and bounded transient presentation state | Host-owned `ExtensionUiSession` under the contribution ID |
| Domain/business state, jobs, cache keys and operation results | Backend capability or authoritative application service |
| User/workspace/project settings | Typed configuration authorities |
| Extension-owned durable data | Declared package/application storage capability, never the UI tree |

The UI snapshot is a projection, not a second domain authority. Events only
invalidate projections; the host or extension controller re-queries the owning
store. Workspace presentation state is versioned and bounded. It contains no
permission, trust decision, executable payload, pointer, credential or renderer
resource and cannot activate an unavailable provider when restored.

### 8. The C ABI carries copied values, handles and lifecycle only

The external UI C ABI follows the extension ABI rules: sized append-only tables,
fixed-width types, byte-counted UTF-8, explicit schema versions, opaque generation-
checked handles, status values and host-owned output sinks. The host copies and
validates module-owned schema/state/action-result data before returning from the
call that borrowed it.

The ABI does not expose C++ interfaces, STL containers, exceptions, RTTI,
allocators, `std::function`, ImGui contexts, draw lists or Horo internal component
objects. Callback context is opaque and callable only under a live module/session
lease. ABI version negotiation and UI-schema negotiation are separate: binary
transport compatibility does not imply support for every surface feature.

Unknown required ABI/table fields, schema nodes, action payloads or specialized
view versions fail before attach. Compatible optional tail fields use `structSize`
and explicit capability bits; consumers never guess based on engine version text.

### 9. Out-of-process helpers use the same contract with stronger isolation

Packages that need crash containment, conflicting dependencies or lower trust run
their controller/backend in a host-supervised helper process. Activation supplies
one authenticated bounded channel scoped to the package, contribution and granted
capabilities. Messages carry canonical schema/state/action protocol values with
length, depth, rate and in-flight-operation limits.

The host owns process launch, environment filtering, credentials, cancellation,
timeouts, restart policy and termination. The helper cannot map editor memory,
receive renderer handles, inject native child windows or use IPC as a service
locator. Loss of the helper replaces its surfaces with a host-owned unavailable/
recovery state without corrupting layout or blocking the GUI frame.

An extension may explicitly launch a separate external application through the
process capability. That application's native/web UI is outside Horo: it is not
docked, cannot claim Horo theme/accessibility equivalence and communicates only
through approved application APIs. Embedded arbitrary HTML/Electron/WebView UI is
not a supported shortcut around the typed surface contract.

### 10. Teardown is admission-first and callback-safe

Surface lifetime is an explicit host state machine:

```text
Declared -> Validated -> Attached -> Active
                         |            |
                         +-> Detaching -> Detached -> Released
```

Detach first closes interaction and state-update admission, invalidates the
session generation and renders no further module-provided snapshot. The host then
cancels outstanding actions/refreshes, releases subscriptions, removes registry
entries transactionally, drains queued callbacks, destroys copied presentation
state and releases the module/helper lease. Late results are rejected by session
generation and cannot resurrect a surface.

In-process native disable, update and removal remain restart-applied by default.
The editor must not unload a dynamic library until every callback, job, borrowed
view, deleter and lease is proven gone. Out-of-process helpers may be terminated
after bounded graceful shutdown because no executable helper pointers exist in
the GUI host. A failed/crashed helper never requires editor-process teardown.

### 11. Migration removes the documented raw-ImGui external path

No supported public UI ABI exists in the current implementation, so this policy
does not break a released extension contract. Migration proceeds as follows:

1. Mark `IWorkspacePanel`, internal component types and ImGui adapters as
   first-party/private; public extension headers do not expose them.
2. Replace the documented custom ImGui drawer path with declarative fields and
   registered specialized typed views.
3. Model UI contributions in ADR-055 with explicit UI schema/resource/action and
   required-feature versions; reject old arbitrary callback payloads.
4. Add a versioned C ABI adapter and out-of-process protocol over the same
   validated model, with headless schema/action contract tests.
5. Convert first-party packaged examples to typed UI projections. First-party
   product panels may remain internal, but cannot be cited as external ABI.
6. Migrate a third-party custom tool to existing typed nodes, request a focused
   specialized-view extension point, or run as a separately launched application.
   It cannot retain raw ImGui rendering as a compatibility fallback.

Compatibility is additive within one schema major version. Removing or changing
node/action semantics requires a new schema version and an explicit pure migration
of stored presentation state. Packages declare required versions/features in the
manifest; the host never silently emulates unsupported direct drawing behavior.

## Verification

Contract coverage must include:

- schema bounds, duplicate IDs, invalid references and required-feature rejection;
- deterministic render-model snapshots in normal, hovered, active, disabled,
  focused, modal, narrow, long-localized and high-contrast states;
- keyboard-only traversal, semantic accessibility snapshots and non-color cues;
- theme/DPI/locale changes without module callbacks inside frame rendering;
- stale action, permission denial, cancellation, timeout and replay rejection;
- GUI-only, mixed-role and out-of-process surfaces using the same schema/actions;
- helper crash/restart and unavailable-surface layout/state preservation;
- detach with queued events/actions and proof of no callback after release; and
- compile-only public-header checks with no ImGui, SDL, renderer or editor-private
  dependency.

Performance evidence records schema size, update frequency, frame cost, action
latency, IPC payload/rate and memory bounds. An extension cannot require synchronous
IPC or module work on the GUI frame to meet correctness.

## Consequences

Horo can theme, localize, test and reason about accessibility for third-party
surfaces while keeping toolkit and renderer choices private. In-process and
isolated modules share one semantic contract, and unknown code never executes in
the draw traversal. The cost is that arbitrary custom widgets require a reviewed
specialized view schema or a separate application. The host must build a capable
typed UI catalog, bounded state/action protocol and tooling for authoring and
previewing schemas.

## Rejected Alternatives

| Option | Decision and reason |
|---|---|
| Publish raw ImGui callbacks/contexts | Rejected: toolkit/global state, memory ABI, input, accessibility, renderer and frame lifetime become an unsafe compatibility surface. |
| Publish C++ panel/widget interfaces | Rejected: compiler, STL, RTTI, exception, allocator and ownership compatibility cannot be maintained for third parties. |
| Treat the C ABI as an immediate-mode draw-command API | Rejected: it preserves per-frame foreign execution and recreates a poorly typed toolkit ABI. |
| Embed arbitrary HTML/Electron/WebView packages | Rejected: it duplicates theme, input, accessibility, resource and security authorities and adds a second GUI platform. |
| Allow native child windows inside editor docking | Rejected: focus, DPI, renderer composition, modal exclusion and cross-platform teardown are not portable or host-verifiable. |
| Require every extension UI to be out-of-process | Rejected: isolation is valuable but transport location does not remove the need for one semantic host-rendered UI contract. |
| Allow only basic forms forever | Rejected: registered specialized typed views support advanced tools without exposing a generic drawing escape hatch. |
| Promise live native unload for declarative UI | Rejected: host-owned pixels do not prove that backend callbacks, jobs and leases into module code are gone. |
