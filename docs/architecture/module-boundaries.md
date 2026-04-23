# Module Boundaries and Public APIs

## Current Reality on `main`

- The repo is organized into module-shaped directories: `core`, `math`, `scene`, `renderer`, `physics`, `input`, `studio`, and `mcp`.
- The build currently compiles those directories into a single `MonolithEngine` target.
- The repo root is exported as a public include root, so cross-module includes are easy and are not yet strongly enforced by the build.
- Because of that, this document is the canonical review policy for boundaries until deeper build-time enforcement is added later.

## Global Rules

- Headers are internal by default.
- A header becomes public only when it is intended for inclusion from outside its owning module and is treated that way in this document.
- New public types must identify their owning module in code comments, docs, or PR notes.
- Lower-level modules must not take dependencies on higher-level authoring or transport layers.

## Allowed Dependency Direction

Preferred direction today:

- `math` -> no engine-module dependencies
- `core` -> `math`
- `scene` -> `math`, `core`
- `physics` -> `math`, `core`, `scene`
- `renderer` -> `math`, `core`, `scene`
- `input` -> `core`
- `mcp` -> `core`, `scene` snapshots/contracts only, narrow editor integration points
- `editor` -> `core`, `math`, `scene`, `renderer`, `physics`, `input`, `mcp`

Forbidden direction by policy:

- `math` must not depend on any higher-level module
- `core` must not depend on `scene`, `renderer`, `physics`, `input`, `editor`, or `mcp`
- `scene` must not depend on `editor` or `mcp`
- `renderer` must not depend on `editor` or `mcp`
- `physics` must not depend on `editor` or `mcp`
- `input` must not depend on `editor` or `mcp`
- `mcp` must not absorb editor UI concerns beyond explicit command/snapshot interfaces

## Module Map

### `math`

- Purpose: foundational math types and utility functions
- Public surface:
  - vector, matrix, quaternion, transform, and math utility headers intended for broad runtime use
- Internal surface:
  - implementation details and any helper code not intended to be included outside `math`
- Allowed dependencies:
  - none
- Forbidden upward dependencies:
  - all other engine modules
- Known current exceptions:
  - none should be introduced; this module is the architectural floor

### `core`

- Purpose: application shell, window/bootstrap, logging, path resolution, generic utilities
- Public surface:
  - headers such as `core/Application.h`, `core/Window.h`, `core/Logger.h`, `core/ProjectPath.h`, `core/EngineLaunchArgs.h`
- Internal surface:
  - helper implementation details that only support bootstrap/logging internals
- Allowed dependencies:
  - `math`
- Forbidden upward dependencies:
  - `scene`, `renderer`, `physics`, `input`, `editor`, `mcp`
- Known current exceptions:
  - none should be normalized into policy

### `scene`

- Purpose: scene data model, ECS registry, components, scene lifecycle integration
- Public surface:
  - registry/entity/component headers and scene-facing data structures used by runtime systems
- Internal surface:
  - module-local helpers and integration glue that should not become a general API
- Allowed dependencies:
  - `math`, `core`
- Forbidden upward dependencies:
  - `editor`, `mcp`
- Known current exceptions:
  - starter/integration helper headers may mention higher-level orchestration context in comments, but the module should remain runtime-owned

### `renderer`

- Purpose: rendering API, camera, meshes, shaders, textures, debug draw, render context
- Public surface:
  - headers needed by game/runtime code such as `renderer/Camera.h`, frame/pass descriptor types, the `Renderer` facade, and mesh/shader resource APIs
- Internal surface:
  - backend details such as `OpenGLRenderBackend`, preview helpers, and implementation-only rendering utilities
- Allowed dependencies:
  - `math`, `core`, `scene`
- Forbidden upward dependencies:
  - `editor`, `mcp`
- Known current exceptions:
  - some editor workflows still own OpenGL-adjacent preview code directly instead of going through narrower orchestration seams

### `physics`

- Purpose: collision, rigid bodies, constraints, world stepping
- Public surface:
  - physics world, collider, and solver-facing headers intended for gameplay/runtime use
- Internal surface:
  - narrow-phase, helper math wrappers, and implementation helpers not meant for direct use
- Allowed dependencies:
  - `math`, `core`, `scene`
- Forbidden upward dependencies:
  - `editor`, `mcp`
- Known current exceptions:
  - none should become public policy

### `input`

- Purpose: input polling and input state access
- Public surface:
  - input state and key/mouse code headers used by application/runtime code
- Internal surface:
  - platform event plumbing and GLFW-specific implementation details
- Allowed dependencies:
  - `core`
- Forbidden upward dependencies:
  - `editor`, `mcp`, `scene`, `renderer`, `physics`
- Known current exceptions:
  - none documented as policy

### `mcp`

- Purpose: protocol, server transport, settings, snapshots, command queue/controller
- Public surface:
  - `mcp/McpProtocol.h`, `mcp/McpController.h`, `mcp/McpServer.h`, `mcp/McpSettings.h`, `mcp/McpSnapshot.h`
- Internal surface:
  - protocol catalog details, transport internals, and request handling helpers not intended for non-MCP callers
- Allowed dependencies:
  - `core`
  - scene/editor state only through explicit snapshot and command interfaces
- Forbidden upward dependencies:
  - editor UI behavior and renderer internals beyond explicit integration seams
- Known current exceptions:
  - `mcp` currently integrates tightly with editor orchestration because `EditorLayer` owns the command execution and snapshot publication loop

### `editor`

- Purpose: authoring UI, inspection, scene editing workflows, asset editing, MCP orchestration
- Public surface:
  - `editor/EditorLayer.h`, scene document/schema/serializer headers that are intentionally consumed by app/editor integration code
- Internal surface:
  - UI layout helpers, editor-only rendering helpers, drag/drop code, thumbnail plumbing, modal state, and other authoring internals
- Allowed dependencies:
  - all lower layers as needed for authoring
- Forbidden upward dependencies:
  - none; this is currently the top layer inside the engine repo
- Known current exceptions:
  - `EditorLayer` is a coupling hotspot and currently mixes UI, document mutation, renderer-facing behavior, and MCP orchestration in one surface

## Current Exceptions to Watch

- Single-target build means boundary violations are possible without immediate build failures.
- Repo-root include visibility means public/internal separation is still convention-driven.
- `EditorLayer` currently concentrates multiple responsibilities that should eventually narrow behind smaller services.
- `mcp` is still bound closely to editor command execution and snapshot publication.

## Follow-Up Work Beyond This Issue

- Split the single `MonolithEngine` target into more explicit subtargets only when the cost is justified.
- Tighten include visibility per module.
- Move editor/MCP orchestration behind narrower interfaces where practical.
