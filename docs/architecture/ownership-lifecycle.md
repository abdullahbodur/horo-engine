# Ownership and Lifecycle Rules

## Ownership Model

Use these terms consistently:

- Owning handle: the type is responsible for creation, release, and shutdown of the resource it holds.
- Borrowed reference: the type may use the resource temporarily but must not release it.
- Published snapshot: immutable copied state handed across a boundary for observation.
- Queued mutation: a request that is stored and later executed by the owning thread/context.

## Resource Categories

### Runtime and App Resources

- `Application` owns:
  - the main window object
  - application startup/shutdown sequencing
  - invocation of game hooks (`OnInit`, `OnUpdate`, `OnFixedUpdate`, `OnRender`, `OnShutdown`)
- Game/app subclasses borrow engine services through the application lifecycle and must release their own higher-level state inside `OnShutdown`.

### Scene and ECS State

- Scene documents and ECS/registry state are owned by scene/editor orchestration code.
- Runtime systems may borrow scene state during update/render steps but should not silently take over ownership.
- Serialized scene files are durable data, not live-owned runtime resources.

### Renderer and GPU Resources

- Renderer/context objects own GPU-backed state and must be initialized and shutdown deterministically.
- Mesh/shader/texture wrappers are owning handles unless a type explicitly says otherwise.
- Non-owning pointers or references to GPU resources must never outlive the owning renderer/resource manager.

### Editor State

- `EditorLayer` owns:
  - editor UI state
  - the active editable `SceneDocument`
  - editor selection state
  - transient modal/interaction state
- Consumers may borrow the pending scene document during reload handoff, but `EditorLayer` remains the source of truth for authoring state until the handoff occurs.

### MCP State

- `McpController` owns:
  - MCP settings document in memory
  - protocol instance
  - HTTP server instance
  - command queue
  - published snapshot pointer
  - status/activity tracking
- `McpHttpServer` owns the worker thread and bind lifecycle for the HTTP server.
- MCP callers never own editor state directly; they only receive snapshots or enqueue commands.

## Centralized Lifetime Rules

- Initialization must flow from outer orchestration to lower-level services.
- Shutdown must happen in reverse dependency order.
- Borrowed references must never outlive the owner that created them.
- Cross-thread consumers should prefer copied snapshots or queued commands instead of shared mutable ownership.

## Current Shutdown Order

Current expected order on `main`:

1. Application/game code stops issuing new work.
2. Editor-owned integrations stop accepting new remote work.
3. `McpController::Shutdown()` stops the server and drops protocol/controller state.
4. Editor ImGui and editor rendering helpers shutdown.
5. Renderer/context and remaining runtime resources shutdown.
6. Window/application shell teardown completes.

This order should be preserved unless a documented replacement is introduced.

## Non-Owning Reference Rules

- Raw pointers and references are borrowed by default.
- Shared mutable access across modules is discouraged unless one side is clearly the owner and the other side is only a temporary user.
- Snapshot objects should be cloned or copied when crossing thread or transport boundaries.

## Deterministic Lifecycle Checklist

Use this checklist in reviews:

- Who creates the resource?
- Who shuts it down?
- Can shutdown be called more than once safely?
- What borrows this resource?
- What is the reverse-dependency shutdown order?

## Seed Validation in Code

The architecture seed check for this issue should preserve at least one deterministic lifecycle invariant in tests:

- `McpController` initialize/shutdown is safe to call repeatedly.
- Disabled/default MCP settings do not require an active server to shutdown cleanly.

Those tests do not prove the whole architecture, but they anchor the documented lifecycle contract in executable behavior.
