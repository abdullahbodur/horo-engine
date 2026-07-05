# Error And Diagnostics Architecture

## Purpose

This document defines how Horo Engine represents, propagates, presents, and
observes failures across C++, GUI, CLI, MCP, Python tooling, background jobs,
and runtime systems.

Errors are typed operation results. Diagnostics explain those results to humans
and tools. Logs provide supporting evidence. These are related signals, but
they are not interchangeable.

## Core Decisions

- Expected failures use typed `Result<T, Error>` values.
- Exceptions do not cross public engine module, host, plugin, C ABI, or thread
  boundaries.
- Every externally visible failure has a stable machine-readable error code.
- Errors preserve a cause chain without requiring callers to parse text.
- Validation may return multiple diagnostics in one pass.
- GUI, CLI, MCP, and Python translate the same application error instead of
  inventing host-specific business errors.
- Logs never become the operation result and are not parsed for control flow.

## Industry Alignment

The Horo error model follows the practices used by production engines, IDEs,
compilers, cloud APIs, and developer tooling:

- expected failures are values, not control-flow exceptions across module,
  thread, host, plugin, or ABI boundaries;
- error identity is machine-readable and stable, similar in intent to
  `std::error_code` categories, HRESULT domains, compiler diagnostic IDs, and
  RFC 9457 Problem Details `type` identifiers;
- human text is presentation fallback, not the branching contract;
- validation can return multiple diagnostics, matching compiler, LSP, and IDE
  workflows instead of failing on the first user-content issue;
- host adapters map the same engine error into GUI state, CLI exit category,
  JSON-RPC error, Python exception, or support bundle payload;
- logs are supporting evidence and correlation data, not the authoritative
  operation result.

Horo deliberately does not copy one external system wholesale. Numeric-only
HRESULT-style codes are compact but poor for extension packages and project
gameplay modules. Exception-only APIs are convenient locally but unsafe across
plugin ABIs, job callbacks, transport boundaries, and process entry points.
String-only ad hoc errors are easy to emit but impossible to validate, localize,
filter, or test reliably. The contract combines typed result values, stable
namespaced codes, structured diagnostics, bounded metadata, and host-specific
translation tables.

## Signal Model

| Signal | Purpose | Owner |
|---|---|---|
| `Error` | Return one failed operation to its caller | Failing operation |
| `Diagnostic` | Describe one or more actionable findings | Validator or operation |
| Log record | Preserve execution evidence | Observability runtime |
| Assertion | Detect an internal invariant violation | Owning module |
| Crash record | Preserve unrecoverable process failure context | Host crash handler |

An operation may return an error containing diagnostics and also emit a
correlated log record. The caller still decides behavior from the typed result.

## Result Contract

Public fallible APIs use a common result type:

```cpp
template<typename ValueT>
using Result = Expected<ValueT, Error>;

using Status = Result<void>;
```

The concrete `Expected` implementation is a foundation detail. Public APIs must
not expose a third-party result type.

Use `Result` for:

- invalid user or project input
- missing files, assets, tools, or capabilities
- unsupported operations
- serialization and validation failures
- recoverable platform and renderer failures
- cancellation, timeout, and resource exhaustion

Do not use `Result` for ordinary query absence when `std::optional<T>` fully
describes the outcome.

## Error Model

```cpp
struct Error {
    ErrorCode code;
    ErrorDomainId domain;
    ErrorSeverity severity;
    std::string message;
    std::vector<Diagnostic> diagnostics;
    std::unique_ptr<const Error> cause;
    ErrorMetadata metadata;
};
```

`ErrorDomainId` is a stable namespaced identifier owned by a module, not a
closed engine-wide enum. Built-in domains include:

```text
horo.foundation
horo.platform
horo.configuration
horo.project
horo.asset
horo.scene
horo.physics
horo.render
horo.pipeline
horo.editor
horo.job
horo.extension
horo.gameplay
horo.release
horo.transport
horo.security
```

Project gameplay modules and extension packages use their stable module ID as
the domain prefix, for example `project.combat` or `extension.mesh_optimizer`.
The host validates domains during module activation so two modules cannot claim
the same error namespace in one process. Internally, frequently used built-in
domains may be mapped to compact numeric IDs after validation, but serialized
diagnostics, logs, MCP payloads, CLI JSON, and Python exceptions carry the stable
textual domain.

`message` is developer-facing fallback text, not a stable API. Branching uses
`ErrorCode`. Presentation layers may localize or enrich messages from the code
and structured metadata.

`ErrorMetadata` is a bounded typed field collection. It may contain safe values
such as an asset ID, normalized project-relative path, target platform, or
operation ID. It must not contain credentials, raw environment dumps, arbitrary
user file contents, or unbounded subprocess output.

## Stable Error Codes

Error codes use a namespaced textual representation at serialization
boundaries:

```text
project.not_found
asset.import.unsupported_format
scene.validation.duplicate_entity_id
render.backend.unavailable
job.cancelled
platform.process.timeout
```

Rules:

- a code retains its meaning after release
- changing the meaning requires a new code
- internal enum values may change, serialized names may not
- one code identifies one failure class, not one source line
- host adapters maintain explicit mappings from codes to protocol or exit status
- module-owned codes live under the module's registered domain and are declared
  as descriptors before activation; dynamically inventing codes from arbitrary
  strings at the failure site is forbidden
- extension and gameplay error codes are versioned with the module contract so
  GUI, CLI, MCP, and Python adapters can render and filter them without knowing
  the module's private C++ types

### Error Code Registry

Every externally visible code is declared by an owning module descriptor before
activation:

```cpp
struct ErrorCodeDescriptor {
    ErrorDomainId domain;
    ErrorCode code;
    ErrorSeverity defaultSeverity;
    std::string_view summary;
    std::string_view remediationHint;
    bool retryable;
    bool userActionable;
    std::optional<ErrorCode> deprecatedBy;
};
```

The registry is host-owned and immutable after module activation. It validates:

- duplicate `(domain, code)` pairs;
- invalid prefixes or module namespace collisions;
- removed codes without a documented replacement or compatibility note;
- extension-provided codes that escape the module's registered domain;
- host translation coverage for public CLI, MCP, Python, and GUI surfaces.

Runtime errors carry a `severity` field, but that severity is bounded by the code
descriptor. A caller may raise or lower severity within the range allowed by the
descriptor; it may not downgrade `Fatal` or internal invariant severities, and it
may not promote a code beyond the descriptor's declared maximum. This keeps
filtering, alerting, and support-bundle interpretation stable across host
adapters. Strict modes may promote documented warning codes to errors, but only
when the descriptor explicitly allows promotion.

Code descriptors are metadata, not a dispatch mechanism. Runtime hot paths may
store compact interned identifiers after validation, but serialized errors keep
the stable textual domain and code.

### Serialized Error Shape

Structured host payloads use one canonical safe shape derived from the C++
`Error`. Adapters may wrap it in their protocol envelope, but they do not invent
new business-error fields:

```json
{
  "domain": "horo.asset",
  "code": "asset.import.unsupported_format",
  "severity": "error",
  "message": "Unsupported asset format",
  "diagnostics": [],
  "metadata": {
    "asset_id": "asset-42",
    "path": "assets/source/tree.fbx"
  },
  "cause": null
}
```

For JSON-RPC, the envelope uses the protocol error code while this Horo payload
lives inside `error.data`. For CLI machine output, it is emitted as the command's
declared JSON schema. For Python, typed exceptions expose the same fields. For
GUI, the same payload drives inline diagnostics, notifications, and details
drawers.

The serialized payload follows Problem Details principles: stable type identity,
safe detail fields, and extension metadata. Horo keeps engine-specific names
instead of adopting HTTP field names directly because most failures are local
engine, editor, runtime, or toolchain failures rather than HTTP resources.

## Diagnostics

Diagnostics are suitable for validation that should report several findings:

```cpp
struct Diagnostic {
    DiagnosticCode code;
    DiagnosticSeverity severity;
    std::string message;
    SourceLocation location;
    std::vector<DiagnosticNote> notes;
    SuggestedAction action;
};
```

`SourceLocation` may identify:

- a project-relative file and line/column
- a scene object and component property
- an asset ID and metadata field
- a configuration source and key
- a build stage and tool invocation

Diagnostics are ordered deterministically so CLI output, tests, and support
bundles remain stable.

## Severity

| Severity | Meaning |
|---|---|
| `Info` | Useful non-failing finding |
| `Warning` | Operation may continue with an explicit degraded condition |
| `Error` | Requested operation failed |
| `Fatal` | Process or isolated runtime cannot continue safely |

Warnings are not silently promoted or discarded by host adapters. Strict modes
may deliberately promote documented warning codes to errors.

## Propagation

Add context at ownership boundaries, not at every stack frame:

```cpp
auto result = importer.Import(request);
if (!result) {
    return Unexpected(
        result.error().WithContext("asset.import.failed")
            .With("asset_id", request.assetId)
            .With("source_path", request.projectRelativePath));
}
```

Context wrapping preserves the original code or adds a new outer code with the
original error as `cause`. It does not flatten the chain into one string.

Background jobs store their terminal `Result` in the authoritative job record.
Completion events carry job identity and terminal state; subscribers query the
job store for full diagnostics.

## Exception Policy

Exceptions may be used privately when required by a standard or third-party
library, but they are caught at the nearest owned adapter and converted to
`Error`.

The following boundaries are exception-free:

- public engine module APIs
- application use cases
- GUI, CLI, and MCP adapters
- job queue callbacks
- renderer backend interfaces
- process entry points and C ABI surfaces

Destructors do not throw. Out-of-memory and corrupted-process conditions may
enter the fatal path when recovery cannot be guaranteed.

## Assertions And Invariants

Assertions indicate programmer errors, not invalid user content.

- Debug and development builds fail fast with source context.
- Release builds preserve safety checks required to prevent corruption.
- User-provided project, asset, scene, network, or protocol data is validated
  and returns errors instead of triggering assertions.
- An assertion failure emits an emergency record before termination when the
  logging runtime is available.

## Host Translation

### GUI

The GUI maps errors into inline field diagnostics, non-blocking notifications,
workflow error states, or explicit fatal dialogs. Presentation code does not
inspect log text.

### CLI

Human output is written to `stderr`. Machine output uses the command's declared
structured schema. Exit codes are stable categories documented by
[CLI Architecture](../interfaces/cli-architecture.md).

### MCP

Protocol errors are distinct from application errors. Valid requests that fail
in the application return a structured application error payload with the
stable Horo code and safe diagnostics.

### Python

Python adapters raise typed Horo exceptions only at the Python API edge. Each
exception exposes the same code, domain, metadata, and diagnostic list as the
C++ result.

## Logging Relationship

The owner of a failed operation logs once at the boundary where the failure
becomes actionable. Intermediate functions propagate context without repeatedly
logging the same failure.

Error records include `operation_id`, `job_id`, `request_id`, or other active
diagnostic context when available. Sensitive fields follow the redaction rules
in [Logging, Context, And Diagnostics](../observability/observability-logging.md).

## Testing

Required tests cover:

- stable code serialization and deserialization
- error-code registry duplicate rejection and domain ownership validation
- host serialized payload shape for CLI, MCP, GUI details, and Python exceptions
- cause-chain preservation
- deterministic diagnostic ordering
- exception-to-error adapters
- GUI, CLI, MCP, and Python mappings
- cancellation and timeout distinction
- redaction of error metadata
- no success event after a failed operation
- release-build invariant checks that protect memory or persistent data

## Related Documents

- [System Design](./system-design.md)
- [Observability Architecture](../observability/observability.md)
- [Engine Data Bus](./engine-data-bus.md)
- [MCP Architecture](../interfaces/mcp-architecture.md)
- [Testing Architecture](../delivery/testing-architecture.md)
