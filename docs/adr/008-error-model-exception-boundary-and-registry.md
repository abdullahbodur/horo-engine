# ADR-008: Error Model, Exception Boundary and Registry Ownership

- **Status**: Proposed
- **Date**: 2026-08-27
- **Supersedes**: None
- **Scope**: Foundation `Result<T,Error>`, `ErrorCode`/`Error`, diagnostics, registry and exception boundaries
- **Issue**: [#1814](https://github.com/abdullahbodur/horo-engine/issues/1814) ([ERR-001.1])
- **Normative document**: [Error And Diagnostics](../architecture/foundation/error-and-diagnostics.md)

## Context

`docs/architecture/foundation/error-and-diagnostics.md` defines the canonical Horo failure model: typed `Result<T,Error>` values, stable machine-readable `ErrorCode` under a module-owned `ErrorDomainId`, a bounded `Error` payload, deterministic `Diagnostic` lists for validation, and host-owned registry validation. Exceptions are forbidden from crossing public module, host, plugin, C ABI and thread boundaries.

Implementation audit (2026-08-27) found the baseline is structurally aligned but scattered across three locations with no registry and a narrower payload:

- `src/foundation/error/.gitkeep` — empty directory; the intended dedicated error module never materialized.
- `src/foundation/FoundationErrors.h/.cpp` — concrete `ErrorCodeDescriptor` declarations for 8 domains (Configuration, Job, Hashing, ModuleDescriptor, Observability, Math, Paths) live at `src/foundation/` root, not under `error/`. Descriptors are `const` globals with no deduplication or domain-collision validation.
- `include/Horo/Foundation/ErrorCode.h` — public `ErrorCode`, `ErrorDomainId`, `ErrorSeverity`, `Error {code,domain,severity,message,diagnostics}`, `ErrorCodeDescriptor`, `MakeError()` and `ErrorPublishedEvent`. Owned by `HoroFoundation` in `cmake/HoroPublicHeaderOwnership.cmake`. Correct header location.
- `src/foundation/diagnostics/ErrorCode.cpp` (18 lines) — implements `MakeError()` by assigning `descriptor.summary` when message empty and returning `Error` with no cause. Ownership anomaly: error construction lives in `diagnostics/`, not `error/`.
- `include/Horo/Foundation/Result.h` — `Result<T>` as `variant<T,Error>` and `Result<void>` specialization. Functional equivalent of the documented `Expected<ValueT,Error>` alias; naming differs only.
- `include/Horo/Foundation/Diagnostics.h` — `Diagnostic {DiagnosticCode, severity, message, location}`. Missing the normative document's `notes`/`SuggestedAction`/`DiagnosticNote` enrichments, but sufficient for current validation needs.
- `include/Horo/Foundation/ErrorCode.h:Error` lacks the normative `cause` chain (`unique_ptr<const Error>`) and `ErrorMetadata` bounded typed fields. `message` is still the only operation-specific carrier besides `diagnostics`.
- No registry exists. The host never validates duplicate `(domain,code)`, prefix collisions, `deprecatedBy` lifecycle, or translation coverage for CLI/MCP/GUI/Python.
- No exception-boundary adapter enforces the documented exception-free edges.

[ERR-001.1] requires: audit deviations as explicit ratify-or-revise outcomes, decide exception-boundary enforcement points, resolve the empty `src/foundation/error` vs stub `diagnostics/ErrorCode.cpp` ownership, and record how validation surfaces multiple diagnostics without changing `Result` semantics.

## Decision

**The foundation owns error types and inert descriptors; the application host owns registry validation and immutability. `src/foundation/error/` is the canonical implementation owner for `ErrorCode` types and `MakeError`; `src/foundation/diagnostics/` retains diagnostic observation and bundle concerns. Validation carries multiple `Diagnostic` values inside `Error.diagnostics` without introducing a second result channel.**

### Ownership and dependency direction

- `HoroFoundation` (target `HoroFoundation`) owns: `include/Horo/Foundation/ErrorCode.h`, `include/Horo/Foundation/Result.h`, `include/Horo/Foundation/Diagnostics.h`, and the implementation `src/foundation/error/ErrorCode.cpp` (+ descriptor sources). These are inert type/descriptor definitions — creating or validating a descriptor never registers a service, selects a backend, or mutates ambient state, per `docs/architecture/foundation/internal-module-descriptor.md`.
- `src/foundation/error/` is canonical. `MakeError()` moves from `src/foundation/diagnostics/ErrorCode.cpp` to `src/foundation/error/ErrorCode.cpp`. `src/foundation/diagnostics/` keeps `DiagnosticsEngine`, `OperationStore`, `DiagnosticBundle`, `BuildOutputStore` — observation and persistence of diagnostics, not error construction.
- `src/foundation/FoundationErrors.h/.cpp` is ratified at its current path for this milestone to avoid churn; it is conceptually owned by `src/foundation/error/` and may be relocated to `src/foundation/error/FoundationErrors.h/.cpp` in a follow-up without changing public headers. Public consumers include only `Horo/Foundation/ErrorCode.h`.
- The host composition root (`ModuleHost` / application layer) owns the **ErrorCodeRegistry**: a host-constructed, immutable-after-activation table that validates every `ErrorCodeDescriptor` contributed by selected modules. Foundation declares; host validates. This preserves dependency direction — feature code never discovers or registers error domains through a global locator.
- `cmake/HoroPublicHeaderOwnership.cmake` continues to assign `Horo/Foundation/ErrorCode.h`, `Result.h`, `Diagnostics.h` to `HoroFoundation`. `horo_configure_target_header_boundary` remains the enforcement gate; no repository-wide `src/` include is published.

### Stable error codes and registry (ratify with bounded revision)

| Area | Current | Outcome |
|---|---|---|
| `ErrorCode`/`ErrorDomainId`/`ErrorCodeDescriptor` shape | Matches normative struct (including `deprecatedBy`) | **Ratified** |
| Descriptor declarations as `const` globals per domain namespace | Present in `FoundationErrors.h` | **Ratified** |
| Registry validation (duplicate detection, domain prefix, deprecation lifecycle, translation coverage) | Absent | **Revised — host-owned registry added** (immutable after activation, duplicate `(domain,code)` is composition failure, extension codes must nest under module's registered domain) |
| `Error` fields `code/domain/severity/message/diagnostics` | Present | **Ratified** |
| `Error.cause` chain and `ErrorMetadata` bounded fields per normative doc | Absent | **Revised-deferred: ratify minimal `Error` for M0**; `cause` and bounded `ErrorMetadata` are accepted as forward-compatible extensions in the normative doc but not required for M0 closure. Future ADR migrates `Error` to include `std::unique_ptr<const Error> cause` + `ErrorMetadata` without breaking `MakeError` callers (fields are additive, validated by registry). Metadata must remain bounded and redacted per `observability-logging.md`. |
| Numeric interned IDs after validation | Not implemented | **Ratified as optional optimization** — serialized forms keep stable textual `domain`+`code`. |

### Result contract

- Public fallible APIs use `Horo::Result<T>` (`Result<void>` as `Status`). The template is the foundation-owned alias for the normative `Expected<ValueT,Error>`; naming is ratified. Implementation detail (`std::variant` vs `std::expected`) is not public. `HasValue()`/`HasError()`/`Value()`/`ErrorValue()` are the branching contract — never `message` text.
- `Result` is for invalid user/project input, missing files/assets/capabilities, unsupported operations, serialization/validation failures, recoverable platform/renderer failures, cancellation/timeout/exhaustion. Absence is `std::optional<T>`.
- Validation that needs multiple findings returns `Error` with `vector<Diagnostic> diagnostics` and/or a dedicated validation result carrying `vector<Diagnostic>` — one `Result` still represents one operation. Callers branch on `Error.code`; presentation reads `diagnostics` deterministically ordered.

### Diagnostics (ratify minimal shape)

- Current `Diagnostic {code, severity, message, location}` and `SourceLocation {source,line,column}` are **ratified** for M0. The richer `notes`/`SuggestedAction`/`DiagnosticNote` in the normative doc are deferred to Post-1.0 — they are additive and do not invalidate the `Error.diagnostics` vector as the multi-diagnostic channel. Ordering is deterministic by `(code.value, severity, source, line, column)` at emission for CLI/test/bundle stability.

### Exception policy and enforcement

The following boundaries are **exception-free**; any exception thrown inside must be caught at the nearest owned adapter and converted to `Error`:

- public engine module APIs (`include/Horo/**`)
- application use cases
- GUI, CLI, and MCP adapters
- job queue callbacks (`HoroFoundation/JobSystem`)
- renderer backend interfaces
- process entry points and C ABI surfaces (`Horo/Extensions/ExtensionAbi.h`, gameplay C ABI)

Rules:

- Exceptions may be used privately only when required by stdlib/third-party and must be caught at the adapter boundary (`try`/`catch(...)` → `MakeError(descriptor, context)`). The conversion chooses the nearest module-owned `ErrorCode` — never an ad-hoc string code.
- Destructors are `noexcept`; out-of-memory / corrupted-process paths may enter the fatal path when recovery cannot be guaranteed, per normative doc.
- Enforcement is by **code review + host adapter guards**: each boundary layer owns a `try`/`catch` at its entry point (e.g., `ModuleHost` activation, `JobSystem` dispatch, CLI `main`, MCP handler, render backend factory). A follow-up adds `clang-tidy`/`MSVC /EH` checks that flag `throw` across the listed boundaries, but the ADR does not rely on toolchain enforcement for M0 correctness.
- Logging is supporting evidence, not the result. The failing operation's owner logs once at the actionable boundary; intermediate frames add context via `WithContext` (future `cause`/`metadata`) without re-logging.

### Validation surfacing multiple diagnostics

- Ratified: validation preserves `Result` semantics — one operation, one `Result`. Multiple findings are surfaced as `Error.diagnostics` (`vector<Diagnostic>`) and, where the operation itself is validation, as a sibling `vector<Diagnostic>` return alongside `Result<void>`. No second authoritative store is introduced. GUI/CLI/MCP/Python adapters render the same `diagnostics` list; logs carry correlation IDs (`operation_id`, `job_id`) from `LogContext`.

## Consequences

- Every production module has one place to declare stable codes (module-owned `ErrorCodeDescriptor` under its domain), one branching contract (`ErrorCode`), and one host-validated registry that rejects duplicates and domain escapes at composition — satisfying [ERR-001.1] acceptance criteria 1 and 2.
- `src/foundation/error/` becomes the discoverable owner for error types; `diagnostics/` no longer owns error construction, resolving the empty-directory anomaly without a header-visibility violation.
- `Result<T,Error>` remains the sole expected-failure channel; validation does not fork the result model.
- Exception safety is explicit and boundary-owned rather than relying on global `noexcept` propagation, preserving the concurrency and plugin ABI contracts.
- Deferred `cause`/`metadata`/`notes` keep M0 scope bounded while leaving the normative shape forward-compatible — adapters that already expect those fields can be added without breaking `MakeError` callers.

## Rejected Alternatives

- **Keep `MakeError` in `diagnostics/` and treat `error/` as documentation-only.** Rejected: splits error ownership across two foundation sub-targets and leaves the empty-directory anomaly as permanent tech debt; contradicts `header-visibility-and-ownership.md` locality.
- **Make the registry a foundation-global singleton populated by static initializers.** Rejected: introduces ambient side effects, static-init ordering hazards, and violates the AGENTS.md rule that descriptor creation must be inert and registration lives in the host composition root.
- **Add `cause`/`ErrorMetadata`/`Diagnostic` notes now as a breaking change.** Rejected: no current caller needs the richer payload; the bounded-revision path (ratify minimal for M0, additive migration next) avoids a cross-cutting refactor while keeping the normative doc as the target.
- **Enforce exception boundaries solely with `noexcept` on all public APIs.** Rejected: `noexcept` would terminate on any missed throw and hides the conversion-to-Error requirement; the adapter `try`/`catch` → `Error` preserves diagnostics and keeps the host in control of presentation.
- **Return `vector<Error>` or `vector<Diagnostic>` as the primary result for validation.** Rejected: fragments the result contract into two success/failure channels; the single `Result` + `diagnostics` vector already satisfies multi-finding validation without a second authoritative store.
