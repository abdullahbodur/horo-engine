# ADR-011: Command Registration, Permissions, Threading and Packaged-Build Policy

- **Status**: Proposed
- **Date**: 2026-08-28
- **Supersedes**: None
- **Scope**: Runtime debug console command registration, `DebugCommandDescriptor`, `CommandPermission` access levels, execution threading rules, packaged-build retail gating, and reconciliation with network administration and world streaming diagnostics
- **Issue**: [#1842](https://github.com/abdullahbodur/horo-engine/issues/1842) ([DBG-001.1])
- **Normative document**: [Runtime Debug Console And Development Overlays](../architecture/runtime/debug-console-and-overlays.md)

## Context

`docs/architecture/runtime/debug-console-and-overlays.md` defines the runtime debug console and development overlay ecosystem for Horo Engine across editor, game development, profiling, dedicated server, and shipping profiles. The console serves as an engine service that exposes registered commands and typed console variables to multiple presentation adapters (in-game UI, editor console panel, CLI, MCP tools, and remote diagnostics endpoints).

As the engine baseline evolves in Milestone 0 (M0), several architectural requirements must be ratified before implementing concrete command handlers across subsystems:

1. **Registration Model**: Command registration must avoid hidden global state, static initializers, or raw arbitrary function pointers. Registration must follow the inert descriptor model (`docs/architecture/foundation/internal-module-descriptor.md`), where modules contribute typed descriptors validated and bound at the host composition root.
2. **Permissions & Security**: Access to commands must be governed by explicit capability tiers (`CommandPermission`) rather than coarse ad-hoc checks. Commands with side effects or cheat semantics must be restricted, audited, and strictly isolated from untrusted callers.
3. **Threading & Lifecycle**: Commands must execute at deterministic safe points aligned with the engine frame lifecycle and structured concurrency rules ([ADR-010](../adr/010-job-waiting-and-operation-store-ownership.md)). Synchronous blocking on the main thread is forbidden for long-running operations.
4. **Packaged-Build Policy**: Shipping and retail game builds must not leak internal developer commands, cheat vectors, or private symbol metadata. Developer commands must be stripped or compiled out at build time rather than relying solely on runtime boolean checks.
5. **Subsystem Reconciliation**: Several planned subsystem capabilities—dedicated server administration ([NET-007.9](https://github.com/abdullahbodur/horo-engine/issues/1169)), authorized network debug controls ([NET-008.12](https://github.com/abdullahbodur/horo-engine/issues/1183)), and world streaming diagnostics ([WST-010.8](https://github.com/abdullahbodur/horo-engine/issues/1652))—rely on console infrastructure. Their structural assumptions must be explicitly reconciled.

[DBG-001.1] establishes the normative architectural decisions governing command registration, permissions, threading, and build-time gating.

## Decision

**Horo Engine adopts an inert, descriptor-based command registration model (`DebugCommandDescriptor`) validated and bound at host composition roots. Command access is strictly enforced via four discrete permission tiers (`Public`, `Developer`, `AdminCheat`, `Restricted`). Command execution dispatches according to declared thread policies (`ImmediateConsoleThread`, `OwnerThreadNextFrame`, `RenderSafePoint`, `WorkerJob`), strictly prohibiting synchronous waits on the main/editor thread. Packaged retail shipping builds compile out or omit developer and cheat command descriptors entirely. Subsystem console capabilities (NET-007.9, NET-008.12, WST-010.8) conform to this unified contract without introducing ad-hoc backdoor execution channels.**

---

### 1. Command Registration and Descriptor Model

#### Inert Descriptor Contract

Debug commands are declared as inert metadata using `DebugCommandDescriptor` (aliased as `ConsoleCommandDescriptor` for runtime console surfaces). Constructing or referencing a descriptor has no ambient side effects: it does not register into global tables, invoke callbacks, allocate heap state, or query service locators.

```cpp
enum class CommandPermission : uint32_t {
    Public     = 1 << 0, ///< Safe for all users and players; allowed in shipping builds.
    Developer  = 1 << 1, ///< Internal diagnostics and inspection; dev/editor/profile builds.
    AdminCheat = 1 << 2, ///< State-mutating cheat/debug actions; dev/editor only, server authority.
    Restricted = 1 << 3  ///< Sensitive ops (remote admin, support dumps); requires explicit token/auth.
};

enum class CommandThreadPolicy : uint8_t {
    ImmediateConsoleThread, ///< Synchronous pure operations (help, history, parsing).
    OwnerThreadNextFrame,   ///< Main/Editor thread deterministic frame safe point.
    RenderSafePoint,        ///< Render thread execution at frame synchronization point.
    WorkerJob               ///< Asynchronous dispatch via Foundation JobSystem.
};

enum class CommandAvailability : uint8_t {
    AllProfiles,       ///< Available across all product profiles.
    DevelopmentOnly,   ///< Compiled/registered only in Editor and Development builds.
    DiagnosticsOnly,   ///< Available in Editor, Development, and Diagnostics builds.
    ShippingAllowlist  ///< Only registered in Shipping if explicitly allowlisted by project configuration.
};

struct DebugArgumentDescriptor {
    std::string_view name;
    std::string_view description;
    DebugArgumentType type;       ///< String, Int32, Int64, Float, Bool, Enum, EntityId.
    bool required{true};
    std::string_view defaultValue{};
    bool sensitive{false};        ///< Redacted from history, logs, and telemetry if true.
};

struct DebugCommandDescriptor {
    DebugCommandId id;
    std::string_view name;        ///< Canonical command identifier (e.g. "log_level", "net.disconnect").
    std::string_view summary;     ///< Short human-readable summary for help and autocomplete.
    std::string_view syntax;      ///< Usage pattern (e.g. "log_level <category> <level>").
    std::span<const DebugArgumentDescriptor> arguments;
    CommandPermission permissions{CommandPermission::Developer};
    CommandThreadPolicy threadPolicy{CommandThreadPolicy::OwnerThreadNextFrame};
    CommandAvailability availability{CommandAvailability::DevelopmentOnly};
    CommandFlags flags{CommandFlags::None};
    DebugCommandHandler handler;  ///< Typed function pointer: Result<DebugCommandOutput, Error>(*)(const DebugCommandContext&, const DebugParsedArguments&)
};
```

#### Rejection of Raw Arbitrary Callbacks

Commands never expose raw untyped function pointers (`void*`, `void (*)(int, char**)`), arbitrary parameter packs, or direct lambdas capturing transient host references. Handlers conform strictly to:

```cpp
using DebugCommandHandler = Result<DebugCommandOutput, Error> (*)(
    const DebugCommandContext& context,
    const DebugParsedArguments& arguments
);
```

- `DebugCommandContext` provides read-only execution metadata: calling host profile, authenticated caller identity/permission bitmask, cancellation token, submission timestamp, output sink, and immutable `ConfigurationSnapshotRef`.
- `DebugParsedArguments` provides strongly-typed, schema-validated positional and keyword arguments. Type validation, range checks, and required-argument verification are completed by the console framework before handler invocation.

#### Registration Seam and Validation

- Modules export inert arrays of `DebugCommandDescriptor` via their `ModuleDescriptor` contributions.
- The host composition root (`ModuleHost` / `Application` / `HoroEditor`) aggregates and validates descriptors during startup activation into an authoritative, immutable `DebugCommandRegistry`.
- Validation enforces:
  - **Uniqueness**: Duplicate command names or aliases cause startup composition failure.
  - **Namespace Discipline**: Subsystem commands must follow registered namespace prefixes (`sys.*`, `log.*`, `net.*`, `wst.*`, `rnd.*`, `phys.*`, `game.*`). Top-level un-namespaced commands are reserved for engine core (`help`, `find`, `version`, `clear`, `screenshot`).
  - **Schema Integrity**: Argument descriptors must have valid types, unique argument names within the command, and valid default values.

---

### 2. Permissions, Access Levels, and Security Model

Command access is evaluated against the caller's active security context prior to execution:

| Permission Level | Description | Target Audiences | Default Profile Availability |
|---|---|---|---|
| `CommandPermission::Public` | Non-mutating queries, information discovery, and player-safe utilities (`help`, `version`, `screenshot`, `clear`). | Players, external users, developers, automated testing. | Editor, Game Development, Game Profile, Game Shipping (if console enabled), Dedicated Server. |
| `CommandPermission::Developer` | Inspection, logging control, performance monitoring, scene tree inspection (`log_level`, `metrics`, `scene_tree`, `inspect`, `debug_draw.*`). | Internal developers, QA, automated test suites. | Editor, Game Development, Game Profile, Dedicated Server. **Excluded from Retail Shipping.** |
| `CommandPermission::AdminCheat` | State-mutating debug actions (`teleport`, `god`, `give`, `net.simulate_loss`, `wst.evict_cell`). | Gameplay developers, internal playtesting. | Editor, Game Development (when cheat mode enabled). **Forbidden in multiplayer without server authority. Excluded from Retail Shipping.** |
| `CommandPermission::Restricted` | High-privilege operations, remote server administration, sensitive diagnostic export (`net.server_shutdown`, `remote_admin.*`, `support_bundle`). | Server operators, authorized engineers. | Dedicated Server (with auth token), Diagnostics builds. Requires cryptographic token or host session authentication. |

#### Permission Enforcement and Denial Rules

1. **Pre-execution Gate**: Permission verification occurs before argument parsing, allocations, or handler invocation.
2. **Denial Semantics**: An unauthorized attempt returns a typed `ErrorCode` (`CommandError::PermissionDenied`) and produces zero side effects.
3. **Information Disclosure Prevention**: Commands requiring permissions higher than the current context are hidden from `help`, `find`, and autocomplete suggestions unless explicitly configured for discovery.
4. **Audit and Redaction**: Commands marked `CommandFlags::AuditLogged` emit structured security audit records through `HostObservability`. Arguments marked `sensitive` (passwords, tokens, player PII) are replaced with `[REDACTED]` in command history, log outputs, and telemetry traces.

---

### 3. Threading, Execution Rules, and Safe Points

All console command adapters (In-Game UI, Editor Panel, CLI, MCP, Remote WebSocket/TCP) accept command strings asynchronously from any thread. However, command execution strictly respects declared thread policies:

```text
[Console Input Sources] (UI / CLI / MCP / Remote)
            │
            ▼ (Thread-safe enqueue)
   [Console Command Queue]
            │
            ├───────────────────────────────────────────────────────┐
            │ (ImmediateConsoleThread)                              │ (OwnerThreadNextFrame)
            ▼                                                       ▼
   [Immediate Pure Evaluation]                             [Frame Scheduler Safe Point]
   - Parsing, help, history                                - PreUpdate / DebugPhase
   - Synchronous Result                                    - Main / Editor Thread
                                                                    │
                                            ┌───────────────────────┴───────────────────────┐
                                            │                                               │
                                            ▼ (Direct handler)                              ▼ (WorkerJob)
                                   [State Mutation / Query]                        [JobSystem Dispatch]
                                   - Scene, ECS, Viewport                          - Snapshot / Export / Dump
                                   - Deterministic Frame Sync                      - Async OperationStore record
                                                                                   - NON-BLOCKING main thread
```

#### Thread Execution Rules

1. **`ImmediateConsoleThread`**: Purely algorithmic operations (e.g. `help`, `find`, `history`, syntax validation) execute synchronously on the caller thread. Handlers must not access engine subsystems, scene objects, or render resources.
2. **`OwnerThreadNextFrame`**: Commands that inspect or mutate engine, scene, or gameplay state are enqueued into a lock-free command queue and drained strictly on the **Main Thread** during a dedicated, deterministic frame phase (`PreUpdate` or `DebugPhase`) before gameplay simulation ticks.
3. **`RenderSafePoint`**: Render-related debug toggles (e.g. `rnd.wireframe`, `rnd.freeze_culling`) dispatch during render frame synchronization boundaries where render frontend/backend state is stable, adhering to [Rendering Architecture](../architecture/runtime/rendering-architecture.md).
4. **`WorkerJob` (Asynchronous / Non-Blocking)**: Heavy diagnostic tasks (e.g. world streaming memory dumps, profiler trace serialization, support bundle generation) dispatch a background job via the Foundation `JobSystem`.
   - In adherence to [ADR-010 (Job Waiting)](010-job-waiting-and-operation-store-ownership.md), the **Main/Editor thread is strictly forbidden from synchronously blocking on `WorkerJob` completion (`Wait()` is illegal)**.
   - The command handler immediately returns an accepted `JobId` / `OperationId`. Progress, diagnostics, and completion are tracked asynchronously through `OperationStore`.

---

### 4. Packaged-Build Retail Gating Policy

To ensure security, minimize binary footprint, and eliminate cheat vectors in commercial distribution, command descriptors and execution machinery are gated per product profile:

| Build Profile | Binary Gating & Preprocessor Rules | Command Set Registered | Remote Access |
|---|---|---|---|
| **Editor** (`HORO_PROFILE_EDITOR`) | Full command tables and debug UI compiled in. | `Public`, `Developer`, `AdminCheat`, `Restricted` (local). | Local MCP / Editor loopback. |
| **Game Development** (`HORO_PROFILE_DEVELOPMENT`) | Full command tables compiled in. In-game console UI active. | `Public`, `Developer`, `AdminCheat` (if cheats enabled). | Localhost diagnostics only. |
| **Game Profile** (`HORO_PROFILE_PROFILE`) | Diagnostic commands compiled in; cheat handlers stripped. | `Public`, read-only `Developer` (metrics, profiler). | Localhost profiling only. |
| **Game Shipping / Retail** (`HORO_PROFILE_SHIPPING`) | **`Developer` and `AdminCheat` descriptors stripped at compile time.** Debug symbols/strings removed. Console UI compiled out or disabled. | `Public` allowlist only (`help`, `version`, `screenshot`). `support_bundle` remains `CommandPermission::Restricted` and is not a Shipping Public command. | **Disabled completely.** |
| **Dedicated Server** (`HORO_PROFILE_SERVER`) | Headless CLI / Remote console compiled in. Visual debug UI omitted. | `Public`, server `Developer`, and authenticated `Restricted` admin. | Authenticated TLS/Token remote admin only. |

#### Compile-Time Stripping Mechanism

- Descriptors with `CommandAvailability::DevelopmentOnly` are enclosed in preprocessor guards (`#if HORO_DEBUG_COMMANDS_ENABLED`) or registered through translation units conditionally linked only in non-shipping configurations.
- In Retail Shipping builds, `HORO_DEBUG_COMMANDS_ENABLED` evaluates to `0`. Stripped command handler functions are dead-code eliminated by the compiler/linker, ensuring that internal identifiers, string literals, and cheat logic cannot be extracted from shipping binaries.

---

### 5. Reconciliation with Subsystems

This decision explicitly ratifies and reconciles the console command requirements across dependent subsystems:

```text
+---------------------------------------------------------------------------------------+
| Subsystem Console Reconciliation                                                      |
+------------------------------------+------------------+-------------------------------+
| Subsystem & Ticket                 | Permission Tier  | Threading & Gating Contract   |
+------------------------------------+------------------+-------------------------------+
| NET-007.9                          | Restricted       | OwnerThreadNextFrame (Tick)   |
| Dedicated Server Administration    |                  | Token-auth / Redacted args    |
+------------------------------------+------------------+-------------------------------+
| NET-008.12                         | AdminCheat /     | OwnerThreadNextFrame          |
| Authorized Network Debug Controls  | Developer        | Generation-safe session IDs   |
+------------------------------------+------------------+-------------------------------+
| WST-010.8                          | Developer        | WorkerJob for heavy dumps     |
| World Streaming Diagnostics        | (Read-Only)      | Non-blocking OperationStore   |
+------------------------------------+------------------+-------------------------------+
```

#### A. Dedicated Server Administration ([NET-007.9](https://github.com/abdullahbodur/horo-engine/issues/1169))
- **Role**: Server management commands (e.g. `net.kick`, `net.ban`, `net.change_map`, `net.server_status`, `net.set_max_players`).
- **Classification**: `CommandPermission::Restricted`.
- **Reconciliation**:
  - Remote admin console must authenticate via cryptographic token before registering an active administrative session.
  - Commands execute strictly on the Dedicated Server Main Thread at server tick safe points (`OwnerThreadNextFrame`). Commands cannot bypass Network Admission or Gameplay Authority.
  - Sensitive arguments (passwords, admin tokens, player IP addresses) must set `sensitive = true` and `CommandFlags::RedactArguments` to guarantee automatic redaction in logs and history.

#### B. Authorized Network Debug Controls ([NET-008.12](https://github.com/abdullahbodur/horo-engine/issues/1183))
- **Role**: Simulation of network impairment and connection lifecycle (e.g. `net.simulate_latency`, `net.simulate_packet_loss`, `net.disconnect`, `net.request_resync`).
- **Classification**: `CommandPermission::AdminCheat` (for mutations/impairments) and `CommandPermission::Developer` (for inspection).
- **Reconciliation**:
  - All network debugger UI panels in `HoroEditor` must route actions through typed console command invocations rather than calling transport/replication internals directly.
  - Commands targeting network sessions must use generation-safe session handles (`SessionHandle`); stale handles return typed errors without affecting replacement sessions.
  - Completely stripped from Retail Shipping client builds.

#### C. World Streaming Diagnostics ([WST-010.8](https://github.com/abdullahbodur/horo-engine/issues/1652))
- **Role**: Streaming cell residency inspection, memory usage query, forced residency changes (e.g. `wst.snapshot`, `wst.cell_status`, `wst.force_evict`).
- **Classification**: Read-only queries are `CommandPermission::Developer`; forced eviction/loading mutations are `CommandPermission::AdminCheat`.
- **Reconciliation**:
  - Read-only diagnostics commands (`wst.snapshot`) capture an immutable data snapshot on the Main Thread during `OwnerThreadNextFrame`, then hand off report formatting/serialization to a `WorkerJob`.
  - Heavy dumps publish results to `OperationStore` without blocking the frame loop.
  - CLI, MCP, and Editor Streaming Debugger consume identical underlying snapshot queries.

---

### Ratify-or-Revise Outcomes

| Topic | Baseline / Prior State | Ratified Outcome |
|---|---|---|
| Command Registration | Ad-hoc or raw function pointers envisioned in draft documents. | **Revised**: Strictly typed `DebugCommandDescriptor` registered via inert module descriptors at host composition root. |
| Permission Tiers | Unstructured string permission tags. | **Revised**: Formal `CommandPermission` enum (`Public`, `Developer`, `AdminCheat`, `Restricted`) with pre-execution validation. |
| Main Thread Execution | Unspecified queuing vs immediate execution. | **Ratified & Enforced**: State mutations execute on Main Thread during deterministic `PreUpdate` safe points; long work uses `WorkerJob`. |
| Main Thread Job Waiting | Ambiguous blocking rules for diagnostics. | **Revised**: Strictly aligned with ADR-010. Main thread waits are prohibited; async commands yield `OperationId` / `JobId`. |
| Retail Gating | Runtime boolean checks. | **Revised**: Compile-time preprocessor stripping and conditional descriptor linkage in shipping builds (`HORO_DEBUG_COMMANDS_ENABLED`). |
| NET-007.9 Admin Boundary | Dedicated server admin planned separately. | **Ratified**: Conforms to `CommandPermission::Restricted`, token authentication, audit logging, and argument redaction. |
| NET-008.12 Network Controls | Direct UI-to-transport calls in early drafts. | **Revised**: UI panels must invoke typed `DebugCommandDescriptor` commands with generation-safe session handles. |
| WST-010.8 Streaming Diagnostics | Undefined dump threading. | **Ratified**: Snapshot taken on `OwnerThreadNextFrame`, heavy dump serialized on `WorkerJob`, reported via `OperationStore`. |

## Consequences

- **Correctness & Safety**: Eliminates data races, re-entrancy issues, and main-thread stalls by enforcing clear execution safe points and asynchronous job delegation.
- **Security & Integrity**: Shipping builds are protected against reverse engineering of cheat commands and unauthorized remote administration by compile-time dead-code elimination.
- **Unified Developer Experience**: GUI debug panels, CLI, MCP tools, and runtime in-game console share identical command semantics, arguments, and execution pipelines.
- **Auditing & Privacy**: Automatic argument redaction prevents credential and PII leakage in command history, telemetry, and log stores.
- **Maintenance Overhead**: Adding new debug commands requires explicit descriptor declaration with schemas and permission classification, discouraging quick ad-hoc hacks in favor of production-grade diagnostics.

## Rejected Alternatives

- **Macro-based static auto-registration (`REGISTER_DEBUG_COMMAND(...)`)**: Rejected. Introduces static initialization order fiascoes, hidden global registries, unpredictable binary bloat, and violates the repo's inert module descriptor architecture.
- **Untyped C-style callbacks (`void (*)(int argc, char** argv)`)**: Rejected. Pushes argument parsing, type conversion, and error handling onto every command handler, resulting in duplicate code, inconsistent error messages, and memory safety vulnerabilities.
- **Runtime-only permission gating in retail builds**: Rejected. Leaving developer/cheat command strings and handler logic in shipping binaries allows easy discovery and exploitation via memory patching or binary disassembly.
- **Synchronous execution of all commands on calling thread**: Rejected. Invoking engine/scene state modifications directly from UI or background MCP threads causes fatal concurrency races and violates frame scheduler invariants.
