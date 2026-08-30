# ADR-018: Command Registration, Permissions, Threading and Packaged-Build Policy

- **Status**: Proposed
- **Date**: 2026-08-28
- **Supersedes**: None
- **Scope**: Runtime debug console command registration, `DebugCommandDescriptor`, `CommandPermission` access levels, execution threading rules, packaged-build retail gating, and reconciliation with network administration and world streaming diagnostics
- **Issue**: [#1842](https://github.com/abdullahbodur/horo-engine/issues/1842) ([DBG-001.1])
- **JIRA**: HORO-1798
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
    Developer  = 1 << 1, ///< Internal diagnostics and inspection; non-shipping builds.
    AdminCheat = 1 << 2, ///< State-mutating cheat/debug actions; dev/editor only, server authority.
    Restricted = 1 << 3  ///< Sensitive ops (remote admin, support dumps); requires explicit token/auth.
};

constexpr CommandPermission operator|(CommandPermission lhs, CommandPermission rhs) noexcept {
    return static_cast<CommandPermission>(static_cast<uint32_t>(lhs) | static_cast<uint32_t>(rhs));
}

constexpr CommandPermission operator&(CommandPermission lhs, CommandPermission rhs) noexcept {
    return static_cast<CommandPermission>(static_cast<uint32_t>(lhs) & static_cast<uint32_t>(rhs));
}

enum class CommandThreadPolicy : uint8_t {
    ImmediateConsoleThread, ///< Synchronous pure operations (help, history, parsing).
    OwnerThreadNextFrame,   ///< Main/Editor thread deterministic frame safe point.
    RenderSafePoint,        ///< Render thread execution at frame synchronization point.
    WorkerJob               ///< Asynchronous dispatch via Foundation JobSystem.
};

enum class CommandAvailability : uint8_t {
    NonShipping,       ///< Available in every non-shipping product profile.
    DevelopmentOnly,   ///< Compiled/registered only in Editor and Development builds.
    DiagnosticsOnly,   ///< Available in Editor, Development, and Diagnostics builds.
    ShippingAllowlist  ///< Only registered in Shipping if explicitly allowlisted by project configuration.
};

enum class CommandFlags : uint32_t {
    None            = 0,
    AuditLogged     = 1 << 0, ///< Emit structured security audit records through HostObservability.
    RedactArguments = 1 << 1, ///< Redact every argument in history, logs, and telemetry (in addition to per-argument `sensitive`).
};

constexpr CommandFlags operator|(CommandFlags lhs, CommandFlags rhs) noexcept {
    return static_cast<CommandFlags>(static_cast<uint32_t>(lhs) | static_cast<uint32_t>(rhs));
}

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
    std::string_view name;        ///< Canonical command identifier (e.g. "log.level", "net.disconnect").
    std::string_view summary;     ///< Short human-readable summary for help and autocomplete.
    std::string_view syntax;      ///< Usage pattern (e.g. "log.level <category> <level>").
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

- `DebugCommandContext` provides read-only execution metadata: calling host profile, authenticated caller identity, `permissionMask` (`CommandPermission` bits granted to this session), `hasServerAuthority` (bool; independent of the permission mask), cancellation token, submission timestamp, output sink, immutable `ConfigurationSnapshotRef`, and a host-composed `IDebugCommandCapabilities` reference. The capability interface exposes only the typed subsystem operations registered for this host and preserves owner-thread checks; it is not a service locator and handlers never discover concrete backends.
- `DebugParsedArguments` provides strongly-typed, schema-validated positional and keyword arguments. Type validation, range checks, and required-argument verification are completed by the console framework before handler invocation.

#### Registration Seam and Validation

- Modules export inert arrays of `DebugCommandDescriptor` via their `ModuleDescriptor` contributions.
- The host composition root (`ModuleHost` / `Application` / `HoroEditor`) aggregates and validates descriptors during startup activation into an authoritative, immutable `DebugCommandRegistry`.
- Validation enforces:
  - **Uniqueness**: Duplicate command names or aliases cause startup composition failure.
  - **Namespace Discipline**: Canonical command names must use a registered namespace prefix (`sys.*`, `log.*`, `net.*`, `wst.*`, `rnd.*`, `phys.*`, `game.*`, `diag.*`). Top-level un-namespaced names are reserved exclusively for engine core (`help`, `find`, `version`, `clear`, `screenshot`). User-facing aliases (e.g. `teleport` → `game.teleport`) may be un-namespaced; the canonical name still carries the prefix. `CommandFlags` combinations on a descriptor are valid; combining `CommandPermission` bits on a descriptor is not (see Permissions).
  - **Schema Integrity**: Argument descriptors must have valid types, unique argument names within the command, and valid default values.
  - **Permission × Availability**: `CommandAvailability::ShippingAllowlist` is valid only with `CommandPermission::Public`. A descriptor that declares `AdminCheat` or `Restricted` together with `ShippingAllowlist` is a composition failure. `CommandPermission` never causes a descriptor to be compiled in; this rule only rejects illegal pairings of the two independent axes.

---

### 2. Permissions, Access Levels, and Security Model

`CommandPermission` is a bitflag type so a *caller session* can hold a grant mask. A *descriptor* declares exactly one of the four named permissions; zero, unknown bits, and OR-ed combinations on `DebugCommandDescriptor::permissions` are composition failures.

The shared enum deliberately keeps the descriptor and session contracts small; it does not make invalid descriptor values unrepresentable in C++. The bitwise operators support session grants and enforcement, while composition validation enforces the single-permission descriptor invariant. A future separation into a required-permission type and a session `PermissionMask` may encode that distinction statically; this ADR retains the validator-based contract.

The pre-execution check is a bit test, not an ordinal comparison:

```cpp
const bool permitted =
    (context.permissionMask & descriptor.permissions) == descriptor.permissions;
```

Tiers are not automatically hierarchical at check time. Session composition may OR lower bits when granting a ceiling:

- Granting `Developer` composes `Public | Developer`.
- Granting `AdminCheat` composes `Public | Developer | AdminCheat`.
- `Restricted` is an orthogonal capability (ops / sensitive dumps), not a rung above `AdminCheat`. It is granted independently and is never implied by developer or cheat grants. A server operator may hold `Public | Restricted` without `AdminCheat`.

`CommandFlags` *are* combinable on the descriptor (`AuditLogged | RedactArguments`). They are the only descriptor flags field that permits bitwise OR; session `permissionMask` grants also permit OR as described above.

Command access is evaluated against the caller's active security context prior to execution:

| Permission Level | Description | Target Audiences | Default Profile Availability |
|---|---|---|---|
| `CommandPermission::Public` | Non-mutating queries, information discovery, and player-safe utilities (`help`, `version`, `screenshot`, `clear`). | Players, external users, developers, automated testing. | Editor, Game Development, Game Profile, Diagnostics, Dedicated Server; Game Shipping only through project-approved local surfaces for allowlisted commands (Section 4). |
| `CommandPermission::Developer` | Inspection, logging control, performance monitoring, scene tree inspection (`log.level`, `sys.metrics`, `game.scene_tree`, `game.inspect`, `rnd.debug_draw.*`). | Internal developers, QA, automated test suites. | Editor, Game Development, Game Profile, Diagnostics, Dedicated Server. **Excluded from Retail Shipping.** |
| `CommandPermission::AdminCheat` | State-mutating debug actions (`game.teleport`, `game.god`, `game.give`, `net.simulate_loss`, `wst.evict_cell`). | Gameplay developers, internal playtesting. | Editor, Game Development (when cheat mode enabled). **Forbidden in multiplayer without server authority. Excluded from Retail Shipping.** |
| `CommandPermission::Restricted` | High-privilege operations, remote server administration, sensitive diagnostic export (`net.server_shutdown`, `net.remote_admin.*`, `diag.support_bundle`). | Server operators, authorized engineers. | Dedicated Server (with auth token), Diagnostics builds. Requires cryptographic token or host session authentication. |

#### Permission Enforcement and Denial Rules

1. **Pre-execution Gate**: Permission verification occurs before argument parsing, allocations, or handler invocation. The check is the bit test above; there is no numeric rank among the four values.
2. **Denial Semantics**: An unauthorized attempt returns a typed `ErrorCode` (`CommandError::PermissionDenied`) and produces zero side effects.
3. **Information Disclosure Prevention**: Commands whose required permission bit is absent from the current `permissionMask` are hidden from `help`, `find`, and autocomplete suggestions unless explicitly configured for discovery.
4. **Audit and Redaction**: Commands marked `CommandFlags::AuditLogged` emit structured security audit records through `HostObservability`. Arguments marked `sensitive` (passwords, tokens, player PII) are replaced with `[REDACTED]` in command history, log outputs, and telemetry traces. `CommandFlags::RedactArguments` redacts every argument of that command, not only `sensitive` ones.
5. **Multiplayer Authority**: `AdminCheat` commands additionally require `DebugCommandContext::hasServerAuthority`. This flag is independent of `permissionMask`. A client that holds the `AdminCheat` bit still receives `CommandError::PermissionDenied` (zero side effects) when `hasServerAuthority` is false. Dedicated-server execution always sets the flag; a PIE listen-server host and single-player authority set it; connected game clients never do. `Restricted` commands authenticate via token/session instead of this flag.

---

### 3. Threading, Execution Rules, and Safe Points

All console command adapters (In-Game UI, Editor Panel, CLI, MCP, Remote WebSocket/TCP) may submit command strings from any thread. Submission dispatch follows the declared thread policy: immediate pure commands execute synchronously on the submitting thread, while every other policy enqueues work for its owning safe point or worker.

```text
[Console Input Sources] (UI / CLI / MCP / Remote)
            │
            ▼ (Thread-safe enqueue)
   [Console Command Queue]
            │
            ├── ImmediateConsoleThread ──► [Immediate Pure Evaluation]
            │                              - Parsing, help, history
            │                              - Synchronous result
            │                              - No engine/scene/render access
            │
            ├── OwnerThreadNextFrame ────► [Frame Scheduler Safe Point]
            │                              - PreUpdate / DebugPhase
            │                              - Main / Editor thread
            │                              - Scene, ECS, viewport mutation
            │
            ├── RenderSafePoint ─────────► [Render Frame Sync]
            │                              - Frontend/backend stable
            │                              - rnd.* debug toggles
            │
            └── WorkerJob ───────────────► [JobSystem Dispatch]
                                           - Snapshot / export / dump
                                           - Async OperationStore record
                                           - NON-BLOCKING main thread
```

The four `CommandThreadPolicy` values are peer dispatch paths. `WorkerJob` is not a child of `OwnerThreadNextFrame`; a command that needs a main-thread snapshot *then* a background dump declares `OwnerThreadNextFrame` for the snapshot handler, which itself enqueues the `WorkerJob`.

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
| **Diagnostics** (`HORO_PROFILE_DIAGNOSTICS`) | Non-shipping diagnostics and support handlers compiled in; `DevelopmentOnly` TUs and cheat handlers omitted. Console UI and overlays available by product policy. | `Public`, diagnostic `Developer`, and authenticated `Restricted` support operations (`diag.support_bundle`); no `AdminCheat`. | Disabled by default; explicit opt-in to an authenticated TLS/token or authenticated host-session endpoint with rate limits and audit logging. |
| **Game Shipping / Retail** (`HORO_PROFILE_SHIPPING`) | Only descriptors marked `ShippingAllowlist` and approved by project configuration are compiled. Internal debug symbols/strings removed. Console UI compiled out or disabled by default; an optional local UI exposes only the allowlist. | `Public` allowlist only (`help`, `version`, `screenshot`). `diag.support_bundle` remains `CommandPermission::Restricted` with `CommandAvailability::DiagnosticsOnly` and is rejected at composition if marked `ShippingAllowlist`. | **Disabled completely.** |
| **Dedicated Server** (`HORO_PROFILE_SERVER`) | Headless CLI / Remote console compiled in. Visual debug UI omitted. | `Public`, server `Developer`, and authenticated `Restricted` admin. | Authenticated TLS/Token remote admin only. |

Diagnostics is a standalone non-shipping product profile for controlled support and investigation builds, selected with `HORO_PROFILE_DIAGNOSTICS`; it is not a runtime mode of Profile, Server, or Shipping. Exactly one product-profile macro is active for a build. These macros specify the required build contract, not existing CMake implementation. Switching profiles requires a separate build; a Shipping launch flag cannot enable Diagnostics handlers.

Shipping command inclusion is independent of console UI inclusion. An approved local startup/CLI option, player-facing action (such as a screenshot key), or explicitly enabled local console UI may invoke an allowlisted command through the same registry, permission gate, and thread-policy dispatch. No surface may bypass those checks or expose commands absent from the Shipping allowlist; remote command access remains disabled even when a local surface is enabled.

#### Compile-Time Stripping Mechanism

- `CommandAvailability` is the sole compile-time inclusion authority. `CommandPermission` is evaluated only at runtime for commands present in the selected profile; it never causes a descriptor to be compiled into a build.
- `HORO_DEBUG_COMMANDS_ENABLED` is the profile-level preprocessor umbrella for non-shipping command translation units. It is defined when the active `HORO_PROFILE_*` is Editor, Development, Profile, Diagnostics, or Server, and is undefined under `HORO_PROFILE_SHIPPING`.
- Descriptors whose `CommandAvailability` is `NonShipping`, `DevelopmentOnly`, or `DiagnosticsOnly` live in translation units gated by `#if HORO_DEBUG_COMMANDS_ENABLED` (further narrowed by profile as needed). `ShippingAllowlist` descriptors live in separately linked translation units that do **not** use this macro; they additionally require an explicit project allowlist entry.
- The availability profile sets are exact: `NonShipping` includes Editor, Development, Profile, Diagnostics, and Server; `DevelopmentOnly` includes only Editor and Development; `DiagnosticsOnly` includes only Editor, Development, and Diagnostics. In particular, Profile and Server do not gain `DiagnosticsOnly` handlers from the umbrella macro. Cheat handlers use `DevelopmentOnly`, so they are absent from Diagnostics as well as Profile, Server, and Shipping.
- In Retail Shipping builds, only approved `ShippingAllowlist` translation units are linked. All other handler functions and descriptor strings are absent rather than relying on dead-code elimination.

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
| WST-010.8                          | AdminCheat /     | WorkerJob for heavy dumps     |
| World Streaming Diagnostics        | Developer        | Non-blocking OperationStore   |
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
| Retail Gating | Runtime boolean checks. | **Revised**: Compile-time preprocessor stripping via `CommandAvailability`. Non-allowlist descriptors live in TUs gated by `HORO_DEBUG_COMMANDS_ENABLED` (defined for every `HORO_PROFILE_*` except `HORO_PROFILE_SHIPPING`); `ShippingAllowlist` TUs are linked separately and do not use that macro. |
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
