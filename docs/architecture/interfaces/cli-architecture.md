# CLI Architecture

## Purpose

This document defines command discovery, parsing, application use-case
integration, output, exit codes, progress, cancellation, configuration, and
testing for the `horo-engine` and `horopak` command-line hosts.

## Core Decisions

- CLI commands are host adapters over shared application use cases.
- Parsing, execution, and presentation are separate stages.
- Every command declares human and machine-readable output contracts.
- Machine-readable stdout never contains logs, progress decoration, or prompts.
- Exit codes represent stable categories.
- Long-running commands expose cancellation and progress without requiring GUI.
- Interactive prompting is explicit and disabled in non-interactive mode.
- CLI execution can run headless without window, ImGui, or GPU dependencies
  unless the command declares them.

## Host Model

```text
argv / environment
        |
        v
CLI Parser
        |
        v
Typed Command Request
        |
        v
Application Use Case / Job Service
        |
        v
Typed Result
        |
        v
Human or Structured Presenter
```

The CLI does not invoke GUI code or synthesize editor widget actions.

## Command Registry

```cpp
struct CliCommandDescriptor {
    CommandPath path;
    std::string summary;
    OptionSchema options;
    CapabilitySet capabilities;
    OutputSchema output;
    InteractivePolicy interactive;
    HostAvailability hosts;
    ContractVersion contractVersion;
    SideEffectPolicy sideEffects;
    CancellationPolicy cancellation;
    TimeoutPolicy timeout;
    StdinPolicy stdinPolicy;
};
```

Command paths form a hierarchy:

```text
horo-engine project create
horo-engine project validate
horo-engine project restore
horo-engine scene validate
horo-engine asset import
horo-engine asset cook
horo-engine package restore
horo-engine package verify
horo-engine package cache list
horo-engine package cache clean
horo-engine build
horo-engine release
horo-engine test
horo-engine console exec
horo-engine mcp serve
horopak inspect
horopak verify
```

Duplicate command paths and option names are startup errors. Help is generated
from the typed registry.

## Command Contributions

CLI commands are contributed through validated command descriptors. Built-in
modules, first-party tools, and approved extension packages may contribute
commands only through the host-owned command registry.

A command contribution declares:

- command path
- option schema
- output schema
- required capabilities
- supported hosts
- side-effect policy
- interactive policy
- cancellation and timeout policy
- stdin policy
- contract version

The host validates all command descriptors before exposing them in help,
completion, or execution. Duplicate command paths, incompatible option schemas,
unauthorized capability requirements, or unsupported hosts fail activation.

Built-in commands and contributed commands use the same descriptor shape. The
host rejects contributions that conflict with existing commands or that attempt
to introduce commands without declaring their hosts and side effects.

## Parsing

Parsing produces a typed request or a list of diagnostics. It does not start
application work.

Rules:

- unknown options fail
- missing required values fail
- enum and numeric ranges validate before execution
- `--` terminates option parsing
- response/config files require an explicit supported format
- paths are normalized by the platform adapter
- credentials are never accepted in positional arguments

Common options such as project, output format, logging, and non-interactive mode
use shared descriptors.

## Configuration

CLI options participate in the precedence defined by
[Configuration System](../foundation/configuration-system.md). Explicit command options
override environment and persisted configuration only for keys the command is
allowed to control.

The effective safe configuration and its provenance may be shown with a
diagnostic command. Secret values are never printed.

## Execution Context

```cpp
struct CliExecutionContext {
    ApplicationServices& application;
    JobSystem& jobs;
    ConfigurationSnapshot configuration;
    CancellationToken cancellation;
    CliOutput& output;
};
```

Concrete commands receive the narrow application capabilities they require
rather than an omnibus mutable engine object.

Runtime console commands exposed through CLI, such as `horo-engine console exec`,
use the same descriptor registry, product-profile policy, and permission checks
as the in-game console. The CLI adapter may submit commands to a running process
only through an explicitly enabled local or remote console endpoint.

## Output Modes

Canonical modes:

- `human`: concise text and terminal-aware progress
- `json`: one valid JSON result document
- `jsonl`: streaming records for commands that declare a streaming schema

In structured modes:

- stdout contains only schema-valid command output
- logs and diagnostics intended for humans use stderr
- colors and terminal control sequences are disabled
- field names and enum values are versioned contracts
- partial failure is represented structurally

Commands do not silently change schema based on TTY presence. TTY detection may
change presentation only in human mode.

## Structured Output Envelope

Structured output modes use a stable top-level envelope unless a command
explicitly declares a different streaming schema.

```json
{
  "schemaVersion": 1,
  "command": "project.validate",
  "invocationId": "cli-...",
  "status": "succeeded",
  "result": {},
  "diagnostics": [],
  "error": null,
  "metadata": {
    "durationMs": 123,
    "projectId": "..."
  }
}
```

Rules:

- `schemaVersion`, `command`, `invocationId`, and `status` are always present.
- `result` is command-specific and follows the command's declared output schema.
- `error` uses the canonical Horo serialized error shape.
- `diagnostics` contain safe, structured diagnostics.
- Human logs, progress decorations, prompts, and ANSI control sequences never
  appear in structured stdout.
- New optional fields may be added only under a versioned compatibility policy.
- Commands that return JSONL streams declare a per-record schema and emit the
  same envelope as the final summary record unless they choose a declared
  streaming schema.

## Progress

Long-running operations expose progress from the authoritative job store.

Human TTY mode may render a live progress line. Non-TTY human mode emits
rate-limited phase updates. Structured streaming mode emits declared progress
records only when requested.

Progress is bounded and never delays the operation because a consumer is slow.

## Cancellation And Signals

The first interrupt requests cooperative cancellation. A second interrupt within
a configured period may request forced host termination after emergency
diagnostics.

Cancellation:

- propagates to the command's task group
- terminates owned subprocesses through platform policy
- preserves transactional file guarantees
- returns the cancellation exit category

The CLI does not leave background jobs running after process exit.

## Input Sources

Commands that read from stdin must declare an explicit `StdinPolicy` in their
descriptor.

Supported policies:

- `None`: stdin is ignored and never blocks command execution.
- `JsonDocument`: stdin contains one bounded JSON document.
- `JsonLines`: stdin contains bounded streaming JSONL records.
- `BinaryStream`: stdin contains binary input and requires a declared size or
  bounded streaming policy.

Commands must not accidentally block on stdin. In non-interactive mode, a
command may read stdin only when its descriptor declares it and the user selected
the matching input option. A command that declares `StdinPolicy::None` keeps
stdin closed or drained to avoid blocking subprocesses or piped automation.

## Interactive Input

Commands declare whether they may prompt. `--non-interactive` is supported by
all commands and is implied when no suitable terminal is available unless an
explicit input channel exists.

Prompts:

- are written to the terminal, not machine stdout
- provide deterministic alternatives through options
- never echo secrets
- fail with an actionable error when required input is unavailable

Release credentials use credential providers or protected input channels, not
ordinary command arguments.

## Exit Codes

Stable categories:

| Code | Category                                      |
| ---: | --------------------------------------------- |
|  `0` | Success                                       |
|  `2` | Usage or command-line validation error        |
|  `3` | Project or input validation failure           |
|  `4` | Required capability or dependency unavailable |
|  `5` | Operation failed                              |
|  `6` | Security or permission failure                |
|  `7` | Cancelled or interrupted                      |
|  `8` | Timeout                                       |
| `10` | Internal invariant or unexpected host failure |

Code `1` is reserved for legacy or host-adapter failures that occur before the
Horo error mapping layer is available, such as an uncaught host exception or a
failure to initialize the CLI runtime. Normal engine failures use the stable
categories listed above so scripts can distinguish generic process failure from
Horo's typed error domains.

Detailed failure identity remains in the structured Horo error code. Shell
scripts should not infer domain details from prose.

## CLI And Data Bus

Commands call use cases directly and receive typed results. They may subscribe
to process-level job or lifecycle notifications to refresh queries, but do not
publish command requests through `EngineDataBus`.

One-shot commands stop subscriptions before application services shut down.

## MCP Serve Mode

`horo-engine mcp serve` composes the documented headless MCP host. CLI parsing
configures the service; MCP requests still execute through shared application
operations and follow [MCP Architecture](./mcp-architecture.md).

Protocol output and ordinary CLI presentation use separate channels so logs
cannot corrupt framing.

## Observability

CLI logs use stderr and the common structured schema. Each command establishes
operation context containing command path, invocation ID, project ID when safe,
and job ID.

Arguments are redacted before logging. Diagnostic bundles may include the safe
effective command configuration but not credentials or arbitrary environment
variables.

## Testing

Required tests cover:

- registry uniqueness and generated help
- parser success and diagnostic failures
- option/configuration precedence
- stdout purity in JSON and JSONL modes
- stable exit-code mapping
- TTY and non-TTY progress behavior
- cancellation and subprocess termination
- non-interactive prompt failure
- redaction of arguments and credentials
- headless commands without GUI or renderer
- equivalence of GUI, CLI, and MCP use-case results

## Adapter Equivalence

GUI, CLI, and MCP adapters must call the same application use cases for the same
business operation. Differences are limited to:

- input parsing and validation envelope
- presentation format
- transport/protocol error envelope
- interactive prompting policy
- progress delivery mechanism

They must not implement separate business rules, scene mutation paths, asset
import logic, build behavior, or release policy.

## Related Documents

- [System Design](../foundation/system-design.md)
- [Error And Diagnostics](../foundation/error-and-diagnostics.md)
- [Configuration System](../foundation/configuration-system.md)
- [Concurrency And Job System](../foundation/concurrency-and-jobs.md)
- [MCP Architecture](./mcp-architecture.md)
- [Runtime Debug Console And Development Overlays](../runtime/debug-console-and-overlays.md)
- [Application Security](../security/application-security.md)
