# ADR-019: CLI Host, Command Ownership, Adapter Equivalence and horopak Boundary Decision

- **Status**: Proposed
- **Date**: 2026-08-28
- **Supersedes**: None (Amends and extends [ADR-004](004-cli-core-gui-boundary.md))
- **Scope**: CLI host ownership (`HoroEngine::CliHost`), command descriptor registry, option parsing, execution dispatch, structured presentation, executable responsibilities (`horo-engine`, `HoroEditor`, `horopak`), separation of concerns between CLI, runtime debug console (DBG-001), and MCP (MCP-001), console delegation seam, domain command adapter equivalence (`ICliCommandAdapter`), and `horopak` isolation boundary
- **Issue**: [#1858](https://github.com/abdullahbodur/horo-engine/issues/1858) ([CLI-001.1])
- **JIRA**: HORO-1814
- **Normative document**: [CLI Architecture](../architecture/interfaces/cli-architecture.md)

## Context

The terminal host in `apps/horo-engine/main.cpp` currently relies on an ad hoc, handwritten option parsing loop (`ParseOptions`) that handles only a handful of hardcoded flags (`--emit-observability-smoke`, `--diagnostic-bundle`). It lacks a reusable command descriptor model, unified option validation, structured machine output schemas, cooperative cancellation propagation, signal lifecycle, standard exit-code mappings, and capability-scoped execution dispatch.

Furthermore, previous architectural documentation left ambiguities across several critical boundaries:

1. **Host Executable Roles**: Unclear boundaries between `horo-engine` (headless engine host, CLI, automation), `HoroEditor` (graphical IDE, embedded MCP server), and `horopak` (specialized archive and cooking utility).
2. **Separation of Presentation Interfaces (CLI vs Debug Console vs MCP)**: Risk of creating a monolithic, string-based command registry shared between terminal CLI, runtime in-game debug console (`DBG-001`), and AI agent Model Context Protocol (`MCP-001`), or conflating terminal arguments with in-game cvars and console commands.
3. **Domain Logic Inversion**: Risk of command-line handlers absorbing business logic (e.g., project serialization, asset dependency graph traversals, build step recipes, pak file binary packing) instead of delegating to domain use cases via typed adapters.
4. **`horopak` Scope Creep**: Risk of `horopak` linking the entire engine runtime, scene graph, or rendering frontend rather than remaining a minimal, fast, zero-GPU standalone utility for CI/CD and containerized asset pipelines.

[CLI-001.1] ratifies the ownership model, target breakdown, executable responsibilities, interface boundaries, and adapter-equivalence contracts to establish a sound foundation before implementing the CLI subsystem across M2–M4.

## Decision

**`HoroEngine::CliHost` is the sole owner of command-line parsing, descriptor registration, help formatting, execution dispatch, structured output presentation, and exit-code mapping. CLI commands are thin adapters (`ICliCommandAdapter`) over domain use cases. CLI, Runtime Debug Console (`DBG-001`), and MCP (`MCP-001`) maintain distinct registries and protocols while sharing domain use cases and providing explicit delegation seams. `horopak` is strictly isolated to asset cooking and `.horo` pak archive manipulation with zero GUI or rendering dependencies.**

---

### 1. Authoritative Ownership & Target Topology

CLI responsibilities are partitioned into dedicated, single-purpose components under `HoroEngine::CliHost` (CMake target `HoroCliHost`, namespace `Horo::Cli`):

| Component / Contract | Target | Ownership & Responsibility |
|---|---|---|
| `CliCommandDescriptor` | `HoroEngine::CliHost` | Inert metadata declaring command path, option schema, output schema, required capabilities, supported hosts, interactivity, side effects, stdin policy, cancellation, timeout, and contract version. |
| `CliCommandRegistry` | `HoroEngine::CliHost` | Validates, deduplicates, and indexes built-in and contributed command descriptors. Rejects conflicts, duplicate options, or unauthorized capability requirements during initialization. |
| `CliOptionParser` | `HoroEngine::CliHost` | Pure parser from `argv`, configuration snapshots, and environment into typed `CliCommandRequest` or structured diagnostics. Does NOT initiate application work or side effects. |
| `CliDispatcher` | `HoroEngine::CliHost` | Validates permissions and required capabilities, binds `CliExecutionContext`, and invokes the registered `ICliCommandAdapter`. |
| `CliOutputPresenter` | `HoroEngine::CliHost` | Formats typed execution results into `human` (TTY-aware styling / progress), `json` (single envelope), or `jsonl` (streaming records) while enforcing stdout purity. |
| `ICliCommandAdapter` | `HoroEngine::CliHost` | Typed adapter interface implemented by domain modules to translate a validated `CliCommandRequest` into a domain use-case invocation and return a typed `Result<CliCommandResult>`. |
| `CliExitCode` | `HoroEngine::CliHost` | Maps typed Horo error domains and cancellation states to stable numeric exit categories. |

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                            HoroEngine::CliHost                              │
│                                                                             │
│   argv ──> [ CliOptionParser ] ──> CliCommandRequest                       │
│                    │                                                        │
│                    v                                                        │
│          [ CliCommandRegistry ] <── Validated Descriptors                   │
│                    │                                                        │
│                    v                                                        │
│             [ CliDispatcher ] ──> Binds CliExecutionContext                │
│                    │                                                        │
│                    v                                                        │
│          [ ICliCommandAdapter ] ──> Calls Application / Domain Use Case     │
│                    │                                                        │
│                    v                                                        │
│            CliCommandResult                                                 │
│                    │                                                        │
│                    v                                                        │
│         [ CliOutputPresenter ] ──> stdout (Pure) / stderr (Logs/Diag)       │
└─────────────────────────────────────────────────────────────────────────────┘
```

---

### 2. Executable Responsibilities and Boundaries

Horo Engine defines three distinct executable composition roots:

```
┌───────────────────────────────────┐  ┌───────────────────────────────────┐  ┌───────────────────────────────────┐
│            horo-engine            │  │            HoroEditor             │  │              horopak              │
│                                   │  │                                   │  │                                   │
│  - Headless host & CLI entrypoint │  │  - Graphical Editor IDE           │  │  - Standalone packaging utility   │
│  - Application & Project services │  │  - ImGui / GUI Screen Host        │  │  - Pak archive create/inspect/read│
│  - Asset cooker & Test runners    │  │  - Viewport Render Extraction     │  │  - Zero GUI, zero RenderApi       │
│  - Headless MCP (`mcp serve`)     │  │  - Embedded MCP Server            │  │  - Minimal cooking toolchain      │
│  - Zero GUI / RenderApi by default│  │  - Concrete RHI (OpenGL / Metal)  │  │  - CI / Container optimized       │
└───────────────────────────────────┘  └───────────────────────────────────┘  └───────────────────────────────────┘
```

#### A. `horo-engine` (Primary Host & Terminal CLI)

- **Role**: Headless engine host, terminal automation CLI, batch processor, and headless MCP server.
- **Composition**: Links `HoroEngine::Application`, `HoroEngine::CliHost`, `HoroEngine::Platform`, `HoroEngine::Foundation`, `HostObservability`, and domain service libraries (`HoroEngine::Assets`, `HoroEngine::Testing`, `HoroEngine::ProjectMigrations`).
- **Invariants**:
  - Compiles and runs in purely headless environments (CI runners, Docker containers, remote servers) without requiring X11, Wayland, Cocoa, or GPU hardware contexts.
  - Does NOT link `HoroEngine::Gui`, ImGui, or concrete viewport renderers (`HoroEngine::EditorViewportOpenGL`, `HoroEngine::EditorViewportMetal`).
  - Commands requiring GPU acceleration (e.g., GPU asset baking) declare explicit capability requirements and select null/headless backends when run without display hardware.

#### B. `HoroEditor` (Graphical IDE & Embedded MCP Host)

- **Role**: Visual authoring environment, document workspace, docked panel host, and interactive debug session.
- **Composition**: Links `HoroEngine::Gui`, `HoroEngine::EditorServices`, `HoroEngine::EditorRenderExtraction`, `HoroEngine::RenderFrontend`, `HoroEngine::Runtime`, concrete render viewports (`OpenGL` / `Metal`), and `HoroEngine::Mcp`.
- **Invariants**:
  - Owns window creation, OS presentation loops, ImGui frame rendering, and viewport picking.
  - Can host `McpServer` over stdio or local HTTP/SSE while the editor runs, dispatching agent tool calls directly to the main editor thread.

#### C. `horopak` (Specialized Asset Packaging & Cook Utility)

- **Role**: Standalone, lightweight CLI utility for `.horo` asset pak creation, table of contents (TOC) inspection, integrity verification, compression, encryption, and extraction.
- **Composition**: Links ONLY `HoroEngine::Archive` (pak format, SHA-256/CRC32 verification, AES-128-CTR crypto), `HoroEngine::Foundation`, `HoroEngine::Platform`, and minimal asset compilation adapters.
- **Invariants**:
  - Strictly forbidden from linking `HoroEngine::Gui`, `HoroEngine::RenderFrontend`, `HoroEngine::RenderApi`, `HoroEngine::RuntimeScene`, `HoroEngine::GameplayRuntime`, `HoroEngine::Physics`, `HoroEngine::Audio`, or `HoroEngine::Networking`.
  - Cannot instantiate full scene pipelines or launch game runtime loops.
  - A bounded cold-start and memory budget suitable for high-throughput containerized asset packaging. Concrete thresholds require a release-build benchmark on each supported host profile.

---

### 3. Separation of Concerns: CLI vs Debug Console vs MCP

CLI commands, Runtime Debug Console commands (`DBG-001`), and MCP tools (`MCP-001`) address different interaction paradigms and must not be collapsed into a single stringly-typed command table:

```
                  ┌──────────────────────────────────────────────┐
                  │            Shared Domain Services            │
                  │  (AssetCooker, ProjectService, TestRunner,   │
                  │   ReleasePipeline, DebugConsoleService)      │
                  └──────▲────────────────▲──────────────▲───────┘
                         │                │              │
                         │                │              │
          ┌──────────────┴──────┐  ┌──────┴──────┐  ┌────┴─────────────┐
          │ ICliCommandAdapter  │  │ IDebugCmd   │  │ McpToolAdapter   │
          └──────────────▲──────┘  └──────▲──────┘  └────▲─────────────┘
                         │                │              │
     ┌───────────────────┴──────┐  ┌──────┴──────┐  ┌────┴─────────────┐
     │   CliCommandRegistry     │  │ DebugConsole│  │ McpController    │
     │   (HoroEngine::CliHost)  │  │ (Runtime)   │  │ (HoroEngine::Mcp)│
     └───────────────────▲──────┘  └──────▲──────┘  └────▲─────────────┘
                         │                │              │
                    Terminal argv    In-Game Console   AI JSON-RPC
```

1. **Protocol & Context Differences**:
   - **CLI (`CliCommandRegistry`)**: Shell-native argument parsing (`--flag`, positional arguments, environment, `--` termination), stdin streaming policies, ANSI/TTY-aware progress, human vs JSON/JSONL output formatting, and OS exit codes.
   - **Debug Console (`DebugConsole`)**: In-game runtime console language (`cmd arg1 arg2`, cvar read/write, autocomplete, in-game terminal view, game pause/time step integration, product-profile gating for shipping vs dev).
   - **MCP (`McpController`)**: JSON-RPC 2.0 protocol over stdio/SSE with JSON Schema parameter definitions, structured tool responses, and LLM-oriented context payloads.

2. **Explicit Delegation Seam (`console exec`)**:
   - The CLI does not duplicate runtime console command handlers. Instead, the CLI provides a dedicated delegation command:
     ```bash
     horo-engine console exec "<cmd> [args]"
     ```
     (or `--exec-console="<cmd>"` during startup).
   - This command delegates execution to the authoritative `DebugConsoleService` / `IDebugConsoleHost`, validating product profiles and permissions without creating an entangled shared registry.

3. **Headless MCP Serve Seam (`mcp serve`)**:
   - `horo-engine mcp serve` initializes the headless `McpServer` over stdio or SSE.
   - CLI ensures strict stream isolation: JSON-RPC communication on `stdout` is completely isolated from engine diagnostic logging (which is routed exclusively to `stderr`).

---

### 4. Adapter Equivalence & Domain Independence

CLI handlers must not contain domain business logic. Domain modules own use-case logic, transactions, and state invariants, while exposing typed interfaces:

```cpp
namespace Horo::Cli {

    /**
     * @brief Context provided to a CLI command adapter during execution.
     */
    struct CliExecutionContext {
        ApplicationServices& application;
        JobSystem& jobs;
        const ConfigurationSnapshot& configuration;
        const CancellationToken& cancellation;
        ICliOutputWriter& output;
        InvocationId invocationId;
    };

    /**
     * @brief Interface implemented by domain modules to contribute CLI behavior.
     */
    class ICliCommandAdapter {
    public:
        virtual ~ICliCommandAdapter() = default;

        [[nodiscard]] virtual const CliCommandDescriptor& GetDescriptor() const noexcept = 0;

        [[nodiscard]] virtual Result<CliCommandResult> Execute(
            const CliCommandRequest& request,
            CliExecutionContext& context) = 0;
    };

}  // namespace Horo::Cli
```

**Adapter Equivalence Rule**:
For any shared operation (e.g., `project.create`, `project.validate`, `asset.cook`, `build.release`, `package.restore`):

- GUI (Editor modal/wizard)
- CLI (`horo-engine <command>`)
- MCP (`tools/call`)

must invoke the exact same underlying domain use-case service (`IProjectService`, `IAssetCooker`, `IReleasePipeline`). Differences are strictly limited to presentation formatting, progress reporting mechanisms, and transport envelopes.

---

### 5. Output Purity & Exit Code Contract

#### A. Strict Channel Separation

- **`stdout`**: Reserved exclusively for command payload.
  - In `human` mode: clean, human-readable prose and interactive tables.
  - In `json` / `jsonl` mode: strictly schema-valid JSON envelopes. Zero logs, warnings, progress bars, or ANSI escape codes.
- **`stderr`**: Reserved for logging, diagnostic messages, interactive progress bars, prompts, and failure backtraces.

#### B. Stable Exit Codes

| Code | Category | Description |
|:---:|:---|:---|
| `0` | **Success** | Command completed successfully. |
| `1` | **Host Failure** | Uncaught host-level exception or pre-initialization runtime failure. |
| `2` | **Usage Error** | CLI syntax error, unknown flag, missing required argument, or invalid option value. |
| `3` | **Input Validation Failure** | Target project, file path, scene, or input payload failed domain validation. |
| `4` | **Capability Unavailable** | Required engine capability, host environment, or dependency is missing. |
| `5` | **Operation Failed** | The requested domain operation encountered a functional error (e.g., compilation error, build failure). |
| `6` | **Security / Permission Error** | Access denied, untrusted script execution, or unauthorized cvar/command execution. |
| `7` | **Cancelled / Interrupted** | Operation was cancelled cooperatively via `SIGINT`/`SIGTERM` or cancellation token. |
| `8` | **Timeout** | Operation exceeded its declared execution timeout. |
| `10` | **Internal Invariant Failure** | Engine bug, unhandled internal invariant violation, or memory corruption. |

---

### 6. Migration Plan from Legacy Ad-Hoc Parsing

The migration path removes legacy ad-hoc parsing in `apps/horo-engine/main.cpp` without maintaining competing sources of truth:

1. **Step 1 ([CLI-001.2] & [CLI-001.3])**: Implement `HoroEngine::CliHost` library with `CliCommandDescriptor`, `CliCommandRegistry`, `CliOptionParser`, and unit test suite.
2. **Step 2 ([CLI-001.5])**: Introduce `CliDispatcher`, `CliExecutionContext`, and migrate smoke tests (`--emit-observability-smoke`, `--diagnostic-bundle`) into first-class `ICliCommandAdapter` registrations (`observability.smoke`, `diagnostics.bundle`).
3. **Step 3 ([CLI-001.6] & [CLI-001.7])**: Implement structured JSON/JSONL output presenters, cooperative cancellation handlers, and headless MCP serve composition (`horo-engine mcp serve`).
4. **Step 4 (Phase-out)**: Replace `apps/horo-engine/main.cpp` entry point with `Horo::Cli::CliHost::Run(argc, argv)`. Remove all legacy `ParseOptions` code.

---

### 7. Ratify-or-Revise Outcomes

| Area | Prior / Ambiguous State | Ratified Decision |
|---|---|---|
| CLI Parsing & Host Ownership | Ad hoc parsing loops in `apps/horo-engine/main.cpp` | **Revised.** `HoroEngine::CliHost` owns parsing, registry, validation, dispatch, and presentation. |
| Registry Separation | Risk of sharing single command table between CLI, Debug Console, and MCP | **Ratified.** Three distinct registries with typed domain use cases and explicit delegation seams (`console exec`, `mcp serve`). |
| Executable Boundaries | Undocumented overlap between `horo-engine`, `HoroEditor`, and `horopak` | **Ratified.** `horo-engine` (headless host), `HoroEditor` (graphical IDE/MCP), `horopak` (isolated archive/cook utility). |
| `horopak` Linkage | Undefined dependencies on engine graphics/runtime | **Ratified.** `horopak` links ONLY `HoroEngine::Archive` and minimal cooking helpers; zero GUI/Render dependencies. |
| Domain Business Logic | Risk of leaking file formatting and business rules into CLI handlers | **Ratified.** Domain modules expose `ICliCommandAdapter`; CLI handlers only transform inputs/outputs. |
| Exit Codes & Output | Inconsistent exit codes (0 vs 2 vs 3) and unseparated stdout logs | **Ratified.** 10 stable exit categories and strict stdout purity in structured modes. |

---

## Consequences

### Positive

- **Architectural Clarity**: Clear separation of responsibilities between CLI, GUI, Debug Console, and MCP interfaces.
- **Headless & CI Readiness**: `horo-engine` and `horopak` can be built and run in lightweight headless environments without graphics drivers or display servers.
- **Maintainability & Robustness**: Standardized descriptor validation prevents flag collisions, invalid option values, and command drift.
- **Scriptability & Tooling Purity**: Machine-readable JSON/JSONL output streams are guaranteed pure for downstream CI/CD pipelines and scripting tools.
- **Single Source of Domain Truth**: Shared application use cases guarantee that builds, cooks, package restores, and project validations behave identically across GUI, CLI, and MCP.

### Negative / Trade-offs

- **Descriptor Boilerplate**: Each new CLI command requires authoring a typed `CliCommandDescriptor` and implementing `ICliCommandAdapter` rather than writing a quick handler function.
- **Multiple Executable Targets**: Requires maintaining distinct CMake compositions for `horo-engine`, `HoroEditor`, and `horopak`.

---

## Rejected Alternatives

1. **Monolithic Unified Command Registry**: Merging CLI commands, in-game console commands (`DBG-001`), and MCP tools (`MCP-001`) into a single global string registry.
   - *Rejected because*: The three surfaces have fundamentally different argument lifecycles, execution contexts, security models, and presentation requirements.
2. **`horopak` as an Alias or Headless Mode of `horo-engine`**: Implementing `horopak` as a symlink or sub-command (`horo-engine pack`) without a dedicated binary.
   - *Rejected because*: Standalone asset packaging must have instantaneous startup, minimal memory footprint, and zero link-time dependencies on the broader engine codebase for deployment in restricted CI worker environments.
3. **Placing Command Business Logic inside CLI Handlers**: Implementing project creation, asset cooking, or pak packing directly inside CLI command source files.
   - *Rejected because*: Violates adapter equivalence and forces duplicate logic between CLI, GUI editor actions, and MCP agent tools.
