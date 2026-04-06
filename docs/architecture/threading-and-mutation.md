# Threading and Safe Mutation Points

This document is the baseline execution-safety contract for issue `#37`.

## Goals

- make main-thread-only work explicit
- document which work may happen off-thread
- define handoff points between background work and owning runtime/editor state
- reduce accidental unsynchronized mutation

## Global Rules

- Mutable editor document state is main-thread owned unless a narrower documented owner is introduced later.
- GPU/resource context access is main-thread/render-thread owned and must not be mutated from transport/background threads.
- Background work should communicate back through snapshots, queues, or explicit handoff points.
- Shared mutable state across threads must be protected by existing synchronization primitives or replaced by copied snapshots.

## Module Matrix

### `math`

- Main-thread only: none
- Off-thread safe: pure math operations
- Handoff points: not needed
- Mutation rules: no shared mutable global state

### `core`

- Main-thread only:
  - application/window lifecycle
- Off-thread safe:
  - logging buffer operations that already protect shared state
- Handoff points:
  - application hooks define the outer sequencing boundary
- Mutation rules:
  - startup/shutdown remains centralized in application orchestration

### `scene`

- Main-thread only:
  - scene mutation tied to live runtime/editor orchestration unless explicitly copied
- Off-thread safe:
  - read-only work on copied scene data
- Handoff points:
  - scene reload/application orchestration boundaries
- Mutation rules:
  - live ECS/document mutation should not be performed from transport or worker threads directly

### `renderer`

- Main-thread only:
  - OpenGL/render-context work
  - GPU resource creation/destruction unless backend rules later change
- Off-thread safe:
  - CPU-only preparation that does not touch live GPU state
- Handoff points:
  - render-context owned upload/teardown boundaries
- Mutation rules:
  - background threads must not touch renderer/GPU handles directly

### `physics`

- Main-thread only:
  - live world stepping unless a dedicated simulation threading model is introduced later
- Off-thread safe:
  - deterministic calculations on isolated/copied state
- Handoff points:
  - scene/runtime orchestration before and after step boundaries
- Mutation rules:
  - no concurrent mutation of a live physics world without an explicit ownership model

### `input`

- Main-thread only:
  - platform/input pump integration
- Off-thread safe:
  - reading copied/latched input state only when documented by the owning layer
- Handoff points:
  - per-frame input update boundaries
- Mutation rules:
  - platform input state remains owned by the main application loop

### `mcp`

- Main-thread only:
  - execution of queued editor commands
- Off-thread safe:
  - HTTP serving
  - request parsing
  - snapshot reads protected by controller synchronization
- Handoff points:
  - `McpController` command queue
  - published immutable snapshot copies
- Mutation rules:
  - transport threads must never mutate editor state directly
  - command execution must flow through `DrainCommands(...)` on the owning editor thread

### `editor`

- Main-thread only:
  - ImGui/UI state
  - editor document mutation
  - scene reload handoff
  - renderer-facing editor interactions
- Off-thread safe:
  - only copied data or explicitly synchronized helper state
- Handoff points:
  - pending document reload boundary
  - MCP command drain/publication cycle
- Mutation rules:
  - editor document changes must remain serialized through the editor-owned mutation path

## High-Risk Zones on `main`

- `EditorLayer` concentrates UI state, document mutation, renderer-facing operations, and MCP orchestration.
- Renderer/OpenGL access remains fundamentally thread-sensitive.
- Asset loading/import workflows need explicit handoff boundaries when background work grows.
- MCP transport is threaded, but editor mutation must remain queued onto the editor-owned path.

## Review Checklist for New Architecture PRs

- Is this operation main-thread only?
- If it runs off-thread, what state does it own?
- How does the result get handed back?
- Is mutable state being shared, copied, or queued?
- Is there an existing sanctioned queue/handoff point that should be reused instead of adding a new mutation path?
