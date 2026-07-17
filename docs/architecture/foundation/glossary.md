# Architecture Glossary

## Purpose

This glossary defines recurring terms used by Horo Engine architecture
documents. A subsystem document may refine a term but must not contradict its
meaning here.

## Terms

### Application Use Case

A typed business operation shared by GUI, CLI, and MCP adapters. A use case
validates input, coordinates owning services, returns a result, and commits
authoritative state.

### Authority

The single owner whose committed state answers a category of query. A data bus,
GUI surface, cache, or copied snapshot is not an authority unless its contract
explicitly says so.

### Backend

A concrete implementation behind a stable engine interface, such as OpenGL,
Vulkan, Null rendering, or an operating-system platform service.

### Borrow

Non-owning access whose lifetime is bounded by an owning object, snapshot, or
operation.

### Capability

A narrow interface granting permission to perform one class of operation. A
capability is passed explicitly and is not discovered through a service locator.

### Command

A typed request to mutate editor-local authoritative state. Undoable editor
commands execute through `EditorHistory`. Commands are direct calls, not
publish/subscribe events.

### Composition Root

The process or session owner that constructs concrete services, connects their
dependencies, and destroys them in the required order.

### Configuration Snapshot

An immutable, validated set of resolved settings with a revision and source
provenance.

### Diagnostic

A structured human- and tool-readable finding with stable code, severity,
location, and optional suggested action.

### Contribution

A manifest-declared item added to a typed extension point, such as an asset
importer, editor panel, MCP tool, or network transport provider. A contribution
is validated and committed by the host; it is not allowed to mutate registries
directly.

### Extension Surface

A GUI or IDE surface contributed by a built-in module or extension package, such
as an editor tab, dedicated panel, modal page, Settings page, status item, menu
item, toolbar action, or diagnostics view. The host owns placement, focus,
capabilities, persistence, and teardown.

### Backend Extension Contribution

A non-GUI contribution that provides an application capability, asset importer,
cooker, validator, pipeline step, toolchain provider, process observer, runtime
slot, or other headless service. It must work without an editor surface and is
observed through typed results, operation stores, diagnostics, metrics, and
revision notifications.

### Hybrid Extension Package

An extension package that contains both backend contributions and frontend
surfaces. The backend contribution owns behavior and state transitions; the GUI,
CLI, MCP, or command contributions are adapters over that backend capability.

### Descriptor

Declarative metadata registered or discovered before activation, such as a
setting, system, extension contribution, asset importer, or behavior type. A
descriptor is not an active runtime instance.

### Error Domain ID

A stable namespaced module identifier used to group machine-readable error
codes. It is serialized as text across logs, CLI JSON, MCP payloads, Python
exceptions, and diagnostic bundles.

### Document

Durable authoring state edited by the user, such as `SceneDocument`. A document
is distinct from workspace presentation state and runtime ECS state.

### Engine Data Bus

The process-scoped typed notification bus. It reports committed lifecycle and
state changes across process-level services.

### Editor Data Bus

The editor-session-scoped typed notification bus. It connects editor surfaces
to editor authorities without direct surface-to-surface references.

### Event Or Notification

A bounded message stating that something committed or became available. It is
not a request for an operation that must return a result.

### Extension

Avoid as a standalone term when the distinction between an extension package and
a gameplay module matters. Use **extension package**, **gameplay module**, or
**contribution** instead. When the broader concept is required, an extension is
any code or descriptor contribution that enters the engine through an explicit,
validated boundary rather than becoming a hidden internal.

### Extension Package

An installable distribution artifact with a manifest, modules, resources,
licenses, and contribution descriptors. A project may request an extension
package, but trust and resolved package paths are local user or workspace state.

### Gameplay Module

A project-owned code and descriptor boundary built with the game project, such as
native gameplay code, script runtimes, generated behavior descriptors, and
project-owned systems. Gameplay modules are not installable extension packages;
they use the SDK-generation C++ boundary and project gameplay module contracts.

### Extension Point

A host-owned typed slot with a public descriptor contract and registry
validation rules, such as `asset.importer`, `editor.panel`, or
`network.transport`.

### Handle

A typed, commonly generation-checked reference into an owning registry. A handle
is not serialized persistent identity.

### Host

An executable composition of engine capabilities, such as `HoroEditor`,
`horo-engine`, or `horopak`.

### Job

Accepted asynchronous work with identity, state, cancellation, progress, and
exactly one terminal result.

### Operation

A user-facing aggregate of one or more jobs or continuations, such as importing
an asset, cooking a project, building a release, or validating a project.
Operations are the default GUI observation unit; raw internal jobs are developer
or profiler detail.

### Lease

Temporary access that keeps a managed resource alive without transferring its
primary ownership.

### Modal Workflow

A temporary screen-like GUI surface above the editor workspace that exclusively
owns interaction while open. It does not replace the application route.

### Module

A CMake target or target family with an explicit public contract and dependency
direction. A module may expose API, runtime, backend, presentation, or extension
point surfaces, but those surfaces remain separate when their dependencies or
performance contracts differ.

### Module Context

A construction-time bundle of explicit capabilities granted to an activated
module by the composition root. It is shaped by validated module descriptors and
is not a service locator.

### Module Descriptor

An inert declaration of a module's stable ID, contract version, dependencies,
capability requirements, settings, commands, extension contributions, resource
budget hints, and observability descriptors.

### Owner Thread

The thread on which an authority may be mutated or a thread-affine resource may
be accessed.

### Panel

A persistent region of the editor workspace layout. A panel may contain one
fixed surface or a tab stack.

### Plugin

Legacy term for an editor/tool extension package discovered from a manifest and
loaded only after compatibility, permission, and trust checks. New architecture
documents should prefer **extension package**, **module**, **contribution**, and
**extension point** when the distinction matters.

### Presentation State

Local UI state used to render or interact with a surface. It is not domain or
document authority.

### Process Event Bridge

An explicit allowlisted adapter that translates selected process-level engine
notifications into editor-session notifications.

### Project

The durable game authoring root, identified by `.horo/project.json` and a stable
project ID.

### Resource

An owned object with an explicit lifetime, such as a file mapping, asset lease,
physics body, GPU texture, window, process, or subscription.

### Renderer Component

An independently installable, signed first-party product component implementing
one stable renderer backend identity. Installation does not imply host support,
probe success, availability, selection, or activation. Renderer components are
product state rather than project `.horopkg` dependencies.

### Result

A typed success or failure returned by a fallible operation. Callers branch on
the stable error contract, not log text.

### Runtime Scene Definition

Validated, typed, backend-neutral data used to instantiate a runtime scene from
an authoring document or cooked scene.

### Screen

A top-level GUI route that owns the application content area, such as Welcome,
Project Browser, or Editor Workspace.

### Session

A bounded lifetime context such as one process, MCP connection, active project,
editor workspace, play run, or profiler capture.

### Snapshot

Immutable state captured at one revision or synchronization point. A snapshot
does not become a second mutable authority.

### Stable ID

Persistent logical identity that survives process restarts and serialization.
It is distinct from a runtime handle or memory address.

### Synchronization Point

A declared boundary where queued mutations, continuations, structural changes,
or resource transitions may safely commit.

### Tab

A persistent editor workspace surface hosted in a tab stack. Tabs share
workspace interaction and are distinct from modal workflows.

### Transaction

A group of operations that commits atomically or rolls back without exposing
partial authoritative state.

### Widget Popup

A short-lived menu, combo, tooltip, or context popup owned by one screen, panel,
tab, or modal. It is not a root workflow modal.

### Workspace

The active editor environment containing document/session authorities, panel
layout, tabs, viewport state, and editor-local notifications.
