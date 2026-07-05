# Runtime Debug Console And Development Overlays

## Purpose

This document defines the runtime debug console, command system, console
variables, and optional development overlays available to games built with Horo
Engine. The goal is to provide runtime control and inspection in editor,
development, profile, and selected shipping builds without turning debug features
into hidden global state or a security liability.

The console is a runtime feature, not an editor-only panel. A game can ship with
the console module present but locked down by profile, policy, platform, and
permissions.

## Horo Target

The default feature set should make a running game inspectable and controllable
without requiring the editor process. Horo should provide:

- an in-game terminal that can execute registered commands and set typed console
  variables
- startup command execution for reproducible local runs
- command history, search, autocomplete, help, and descriptor-driven discovery
- live log stream, metrics charts, profiler controls, and debug-draw toggles
- project/gameplay command registration through the public SDK
- profile-gated behavior so development builds are powerful and shipping builds
  are locked down by default
- optional remote access for diagnostics builds only, with authentication and
  audit logging

The important distinction is that the command registry and data sources are
engine services; the visible terminal, overlay panels, editor tabs, CLI, and MCP
tools are only adapters.

## Runtime Layout

```text
+--------------------------------------------------------------------------------+
| Horo Game Window                                                               |
|                                                                                |
|  Fullscreen Game View                                                          |
|                                                                                |
|       player camera / gameplay / debug.draw.physics / debug.draw.nav           |
|                                                                                |
|  ............................................................................  |
|  . Dimmed game background while runtime debug UI owns focus                 .  |
|  .                                                                          .  |
|  .                                                    +------------------+  .  |
|  .                                                    | Logs             |  .  |
|  .                                                    | warn/error       |  .  |
|  .                                                    | filter: ai.*     |  .  |
|  .                                                    +------------------+  .  |
|  .                                                    +------------------+  .  |
|  .                                                    | Frame            |  .  |
|  .                                                    | cpu/gpu p95      |  .  |
|  .                                                    +------------------+  .  |
|  .                                                    +------------------+  .  |
|  .                                                    | Memory/Jobs      |  .  |
|  .                                                    | rss/assets       |  .  |
|  .                                                    +------------------+  .  |
|  ............................................................................  |
|                                                                                |
|  +----------------------------------------------------------------------------+ |
|  | > help renderer                                                             | |
|  | > log_level renderer debug                                                  | |
|  | > profile_capture start --seconds 20 --channels cpu,gpu,jobs                | |
|  | result: capture started: cap_42                                             | |
|  +----------------------------------------------------------------------------+ |
+--------------------------------------------------------------------------------+
```

The game view remains fullscreen and continues to render behind the debug UI.
Opening the terminal or overlay panels applies a dimming layer over the game and
moves input focus to the debug surface. Gameplay input is suppressed while the
debug surface owns focus unless a command explicitly requests a pass-through
inspection mode. The terminal and panels can be hidden independently. Hidden
panels keep no private shadow state; they query bounded stores when opened or
refreshed.

## Product Profiles

| Profile           | Console UI                          | Commands                                      | Variables                               | Overlays            | Remote access                            |
| ----------------- | ----------------------------------- | --------------------------------------------- | --------------------------------------- | ------------------- | ---------------------------------------- |
| Editor            | Available                           | Full editor-safe and runtime-safe sets        | Full development set                    | Available           | Local MCP/editor only                    |
| Game Development  | Available by default or launch flag | Full development set                          | Full development set                    | Available by flag   | Localhost by default                     |
| Game Profile      | Available by launch flag            | Profiling and read-only inspection by default | Tunables marked profile-safe            | Available by flag   | Localhost only unless explicitly enabled |
| Game Shipping     | Hidden by default                   | Allowlisted safe commands only                | Read-only or player-safe variables only | Disabled by default | Disabled                                 |
| Diagnostics build | Available by policy                 | Expanded diagnostics set                      | Expanded diagnostics set                | Available           | Explicit authenticated endpoint only     |

Shipping builds must not contain arbitrary file commands, unrestricted scene
mutation, unauthenticated remote execution, source-path disclosure, or commands
that bypass gameplay/security policy. A shipping console may still expose safe
commands such as `help`, `version`, `log_level`, `screenshot`, accessibility
toggles, or player-support diagnostics when the product opts in.

## Core Concepts

### Console Command

A command is an explicitly registered operation:

```cpp
struct ConsoleCommandDescriptor {
    ConsoleCommandId id;
    std::string_view name;
    std::string_view summary;
    ConsoleArgumentSchema arguments;
    ConsoleCommandAvailability availability;
    ConsoleCommandPermissions permissions;
    ConsoleCommandThread thread;
    ConsoleCommandFlags flags;
};
```

Commands do not expose raw function pointers to arbitrary callers. Execution
produces a typed result, diagnostics, and structured console output. Commands
that mutate runtime state run through the owning application or runtime service
and respect lifecycle safe points.

### Console Variable

A console variable is a typed, descriptor-owned setting bridge:

```cpp
struct ConsoleVariableDescriptor {
    ConsoleVariableId id;
    std::string_view name;
    ConsoleValueType type;
    ConsoleValue defaultValue;
    ConsoleValue minValue;
    ConsoleValue maxValue;
    ConsoleVariableAvailability availability;
    ConsoleVariablePersistence persistence;
    ConsoleVariableApplyPolicy applyPolicy;
};
```

Console variables are not an untyped global map. Each variable declares type,
range, profile availability, persistence, and apply behavior. Some variables map
to configuration keys; others are runtime-only diagnostic toggles. Apply policy
is explicit:

| Apply policy      | Meaning                                                     |
| ----------------- | ----------------------------------------------------------- |
| `Immediate`       | Safe to update atomically while running.                    |
| `NextFrame`       | Queued and applied at the next owner-thread frame boundary. |
| `NextSceneLoad`   | Takes effect when a scene is loaded or restarted.           |
| `RestartRequired` | Stored as pending and reported as not active yet.           |
| `ReadOnly`        | Inspectable only.                                           |

### Development Overlay

A development overlay is a runtime UI adapter over observability, console,
debug-draw, and runtime inspection services. It is not the owner of logs,
metrics, profiler captures, or gameplay state.

Default overlay modules should include:

- console terminal and command history
- live log stream with filtering and collapse
- frame time, CPU, memory, jobs, asset streaming, and renderer charts
- profiler capture controls for allowed profiles
- entity/scene inspector in development builds
- debug draw toggles and categories
- input/action monitor
- network/session monitor when networking is enabled

## Module Shape

```text
Process Host
  +-- Configuration Service
  +-- Observability Runtime
  +-- Engine Data Bus
  +-- Runtime Console Service
  |     +-- Command Registry
  |     +-- Variable Registry
  |     +-- Console History Store
  |     +-- Console Script Runner
  |
  +-- Runtime Debug Services
  |     +-- Debug Draw Registry
  |     +-- Runtime Inspector
  |     +-- Overlay Layout Store
  |
  +-- Presentation Adapters
        +-- In-game Console UI
        +-- Development Overlay UI
        +-- Editor Console Tab
        +-- CLI command adapter
        +-- MCP adapter where allowed
```

The runtime console service is constructed by the process host when the selected
product profile and project configuration request it. The visible in-game UI is
optional; commands can still be executed by CLI/MCP/editor adapters when policy
allows.

## Data Flow

```mermaid
sequenceDiagram
    participant User as Player / Developer
    participant UI as Console UI
    participant Console as Runtime Console Service
    participant Policy as Command Policy
    participant App as Application Runtime Service
    participant Store as Console History Store
    participant Obs as Observability Runtime

    User->>UI: Enter command text
    UI->>Console: Submit command line
    Console->>Console: Parse and resolve descriptor
    Console->>Policy: Check profile, permissions, and thread policy
    Policy-->>Console: Allowed command request
    Console->>App: Execute through owning capability
    App-->>Console: Typed result and diagnostics
    Console->>Store: Append bounded command/result record
    Console->>Obs: Emit structured log and metrics
    Console-->>UI: Render result snapshot
```

No command may mutate scene, renderer, physics, asset, or networking state by
capturing private pointers from registration time. Mutation goes through narrow
capabilities that own validation and safe-point scheduling.

## Launch And Configuration

Recommended launch flags:

```text
--console                 enable the runtime console UI if profile allows it
--dev-overlays            enable default development overlay panels
--dev-overlay=<name>      enable one overlay panel or group
--exec <command>          execute one startup command after configuration load
+<command> [args...]      startup command alias where supported
--console-script <path>   execute an allowlisted script file
--remote-console <addr>   enable authenticated remote console in diagnostics builds
```

Recommended project configuration:

```json
{
  "runtimeConsole": {
    "enabledByDefault": false,
    "toggleChord": "Grave",
    "historyLimit": 500,
    "allowStartupExec": true,
    "shippingAllowlist": ["help", "version", "screenshot", "support_bundle"]
  },
  "developmentOverlays": {
    "enabledByDefault": false,
    "defaultPanels": ["logs", "frame", "memory", "jobs"]
  }
}
```

Configuration is resolved through immutable snapshots. Runtime variable changes
that map back to persisted settings must declare persistence and write policy;
most diagnostic variables are session-only.

## Command Language

Initial command language should stay deliberately small:

- whitespace-separated command and arguments
- quoted strings with escaping
- `key value` and `--key value` forms for typed options
- command history and reverse search
- autocomplete from descriptors
- `help`, `help <command>`, and `find <text>`
- optional aliases and key binds in development profiles
- optional startup script execution from allowlisted project/user locations

Pipes, conditionals, loops, and filesystem commands are not part of the initial
runtime contract. They can be added later to a development-only script module if
there is a real need. The first version should optimize for predictable command
execution and low risk, not shell completeness.

## Default Commands And Variables

Minimum command set:

| Command                         | Profile                                          | Behavior                                                   |
| ------------------------------- | ------------------------------------------------ | ---------------------------------------------------------- |
| `help`                          | all enabled profiles                             | List available commands and variables.                     |
| `find <text>`                   | all enabled profiles                             | Search descriptors.                                        |
| `version`                       | all enabled profiles                             | Print engine, game, build, platform, and profile identity. |
| `clear`                         | all enabled profiles                             | Clear visible console buffer, not persistent logs.         |
| `log_level <category> <level>`  | development/profile, optionally shipping support | Change session log level through observability policy.     |
| `metrics`                       | development/profile                              | Show available metric channels.                            |
| `profile_capture <start\|stop>` | development/profile                              | Control bounded profiler captures.                         |
| `screenshot`                    | development/profile, optionally shipping support | Capture the current frame through platform policy.         |
| `scene_tree`                    | development only                                 | Print runtime scene hierarchy or ECS debug view.           |
| `inspect <entity>`              | development only                                 | Inspect safe public entity/component state.                |
| `teleport`, `give`, `god`, etc. | project-defined, cheat-gated                     | Gameplay-specific commands controlled by project policy.   |
| `support_bundle`                | diagnostics/shipping opt-in                      | Create user-approved diagnostic bundle.                    |

Minimum variable groups:

```text
console.visible
console.history.limit
log.<category>.level
overlay.logs.visible
overlay.frame.visible
overlay.memory.visible
overlay.jobs.visible
debug.draw.<category>
debug.draw.physics
renderer.debug.<feature>
asset.streaming.debug
```

Project/gameplay commands register under a project namespace such as
`game.player.teleport` or user-facing aliases such as `teleport`. Namespace
collisions are startup diagnostics.

## Development Overlays

Development overlays are optional modules that can be composed per game:

| Overlay    | Data source                            | Notes                                                               |
| ---------- | -------------------------------------- | ------------------------------------------------------------------- |
| Logs       | `StructuredLogStore`                   | Filter by level/category, collapse repeats, no unbounded buffering. |
| Frame      | `MetricsStore` and profiler scopes     | FPS, frame p50/p95/p99, CPU/GPU split where available.              |
| CPU/Memory | platform sampler and allocator metrics | Label process vs engine allocator values explicitly.                |
| Jobs       | `OperationStore` and job metrics       | Queued/running/waiting counts, saturation, slow operations.         |
| Renderer   | renderer metrics and debug views       | Draw calls, passes, GPU memory, frame graph summary.                |
| Assets     | asset service metrics                  | Streaming queue, loaded assets, failed loads, hot reload state.     |
| Scene      | runtime scene inspector                | Development-only, safe public component views.                      |
| Input      | input snapshots/action map             | Current actions, devices, focus/capture state.                      |
| Network    | networking metrics                     | Connection state, packet loss, RTT, replication stats when enabled. |

Overlay rendering must not allocate per visible row every frame. Long lists use
virtualized/recycled rows, bounded stores, and explicit refresh rates. Disabled
overlays must have near-zero cost beyond the underlying always-on metrics that
the product profile already permits.

## Security And Permissions

Console access is a capability decision:

- Profile controls which commands and variables exist.
- Project policy controls which project commands are exposed.
- Platform policy controls toggle chords, remote access, and shipping support.
- Multiplayer/server authority controls cheat commands.
- Remote console requires authentication, local opt-in, rate limits, audit logs,
  and a separate threat model.

Commands declare permissions such as:

```text
ReadDiagnostics
ChangeSessionDiagnostics
MutateRuntimeScene
MutateGameplayState
AccessFilesystem
ControlProfiler
CreateSupportBundle
RemoteOnlyDenied
ShippingDenied
CheatGated
ServerAuthorityRequired
```

The command registry refuses to register commands whose permissions are
incompatible with the active product profile unless the command is compiled out
or hidden by policy. Hidden commands must not be discoverable through `help` or
autocomplete.

## Threading And Lifecycle

Command handlers declare where they execute:

| Thread policy            | Use                                                       |
| ------------------------ | --------------------------------------------------------- |
| `ImmediateConsoleThread` | Pure parsing/help/history operations only.                |
| `OwnerThreadNextFrame`   | Scene/runtime/editor mutations.                           |
| `WorkerJob`              | Long-running diagnostics or support bundle generation.    |
| `RenderSafePoint`        | Renderer debug toggles requiring render-thread ownership. |

The console UI may accept input at any frame, but mutable commands are queued to
the owning safe point. Shutdown rejects new commands, cancels pending long
operations, and preserves command history only after sinks are still valid.

## Relationship To Observability

The console and overlays consume observability. They do not replace it.

- Logs are emitted through the logging contract and displayed from the bounded
  structured log store.
- CPU, memory, frame, job, renderer, and asset charts query metrics stores.
- Detailed captures are controlled through `ProfilerCaptureService`.
- Console command execution emits structured logs and metrics.
- The data bus carries coalesced revision notifications only.

This keeps runtime overlays cheap enough to leave compiled into development and
profile builds without turning every frame sample into UI traffic.

## Modularization

The recommended default feature package is split into modules:

| Module                | Required?                 | Responsibility                                                  |
| --------------------- | ------------------------- | --------------------------------------------------------------- |
| `RuntimeConsoleCore`  | yes when feature enabled  | registry, parser, variables, policy, history, command execution |
| `RuntimeConsoleUi`    | optional                  | in-game terminal presentation                                   |
| `DevelopmentOverlays` | optional                  | default overlay panels and layout                               |
| `DebugDraw`           | optional                  | world-space debug lines, shapes, text, categories               |
| `RuntimeInspector`    | optional development-only | safe public scene, entity, component, input, and session views   |
| `OverlayLayout`       | optional                  | panel placement, visibility, focus order, and persisted layout   |
| `RemoteConsole`       | optional diagnostics-only | authenticated remote command endpoint                           |

Games can remove UI modules while keeping core command execution for CLI/MCP or
diagnostic builds. Project-specific commands and overlays register through public
SDK descriptors, not by modifying engine registries directly.

## Testing

Required tests cover:

- descriptor registration, duplicate-name diagnostics, and namespace rules
- command parsing, quoting, autocomplete, help, and startup execution
- variable type/range validation and apply policies
- product-profile availability and shipping allowlist enforcement
- permission denial does not execute handler side effects
- owner-thread scheduling and lifecycle shutdown rejection
- bounded history, log stream, and overlay list memory behavior
- observability integration without per-sample data-bus traffic
- development overlay enable/disable cost and virtualized row behavior
- packaged development build can open console and execute safe commands
- shipping profile hides denied commands and disables remote access
- remote console authentication/rate-limit/audit behavior if implemented

## Related Documents

- [Console Panel](./console-panel.html): HTML reference design for the editor
  console tab, command input, log filtering, and record details.
- [Runtime Lifecycle](./runtime-lifecycle.md)
- [Input Architecture](./input-architecture.md)
- [Rendering Architecture](./rendering-architecture.md)
- [Scene Runtime](./scene-runtime.md)
- [Networking Architecture](./networking-architecture.md)
- [Configuration System](../foundation/configuration-system.md)
- [Engine Data Bus](../foundation/engine-data-bus.md)
- [Observability Architecture](../observability/observability.md)
- [Metrics And Profiling Contract](../observability/observability-performance.md)
- [Logging, Context, And Diagnostics Contract](../observability/observability-logging.md)
- [Gameplay Module](../extensions/gameplay-module.md)
- [CLI Architecture](../interfaces/cli-architecture.md)
- [Application Security](../security/application-security.md)
