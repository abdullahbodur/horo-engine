# ADR-045: Backend Validation and Debug Layer Integration

- **Status**: Proposed
- **Date**: 2026-09-01
- **Supersedes**: None
- **Scope**: Native renderer validation policy, activation, callback integration and lifecycle
- **Issue**: [RND-017.5](https://github.com/abdullahbodur/horo-engine/issues/437)
- **Jira**: [HORO-437](https://horo-engine.atlassian.net/browse/HORO-437)
- **Companion decisions**: [ADR-029](029-opengl-core-profile-and-platform-policy.md), [ADR-030](030-metal-platform-and-feature-baseline.md), [ADR-031](031-vulkan-loader-platform-and-version-baseline.md), [ADR-032](032-d3d12-baseline-and-agility-sdk-policy.md), [ADR-041](041-backend-neutral-renderer-diagnostics-model.md), [ADR-044](044-render-markers-and-debug-labels.md)
- **Normative documents**: [Error And Diagnostics](../architecture/foundation/error-and-diagnostics.md), [Configuration System](../architecture/foundation/configuration-system.md), [Rendering Architecture](../architecture/runtime/rendering-architecture.md), [Render Backend Parity Contract](../architecture/runtime/render-backend-parity-contract.md)

## Context

Native graphics APIs expose materially different validation facilities. Vulkan
selects layers and debug utilities while creating an instance. D3D12 debug and
GPU-based validation must be configured before the relevant factory/device work.
OpenGL debug-context intent precedes context creation, while its message callback
is installed after a current context exists. Metal validation availability and
activation may be controlled by the launched process environment and development
tooling rather than a portable runtime switch.

Treating a Debug build as sufficient policy creates silent differences between
hosts, packages and backends. Enabling validation after device creation is too
late for some APIs. Requiring development layers in a player package would make
renderer availability depend on tools that are not runtime prerequisites.
Allowing native callbacks to log directly would also bypass ADR-041 identity,
privacy, aggregation and shutdown rules.

This ADR owns selection, realization and lifecycle of native validation/debug
facilities. It does not redefine Horo's always-applicable typed input, graph,
resource or lifecycle validation. It does not own render markers, external capture
tools, GPU crash bundles, driver workaround policy or inspector UI.

## Decision

### 1. Validation intent is an explicit host-owned startup request

The application composition root resolves one immutable request before creating a
presentation window, native instance, context, factory or device:

```cpp
struct RendererNativeValidationRequest {
    NativeValidationRequirement requirement;
    NativeValidationFeatureMask features;
    NativeValidationMessageFilter messageFilter;
    NativeValidationBreakPolicy breakPolicy;
    ProductProfileId productProfile;
    BuildProfileId buildProfile;
    ConfigurationRevision configuration;
};
```

`NativeValidationRequirement` is `Disabled`, `Optional` or `Required`:

- `Disabled` forbids native validation activation for that renderer generation.
- `Optional` requests the declared feature set but permits only the backend's
  documented non-validation retry or disabled realization.
- `Required` makes every requested feature an admission requirement; missing or
  failed activation returns a typed startup failure.

The feature mask uses backend-neutral intent such as core API validation,
synchronization validation, resource-lifetime validation, shader validation and
expensive GPU-assisted validation. A feature is not inferred from severity,
backend ID or build type. The requirement applies to the complete requested
feature set: `Required` admits no omission, while `Optional` may realize only a
declared supported subset or the declared disabled fallback.

Configuration precedence follows the Foundation configuration contract: explicit
CLI, project/user policy and host/build defaults are resolved into one value before
backend selection. Shipping defaults to `Disabled`. Editor/developer and
qualification profiles may request validation, but a Debug binary alone does not.
Project content cannot require a developer layer for normal game correctness.

The request is not a live setting. Changing it requires backend restart, and a
process-level Metal activation change may require host process restart. Renderer
feature code, editor panels and backends cannot mutate the request after
composition.

### 2. Resolution produces an immutable, evidence-bearing plan

Before native creation the selected backend resolves the request against its
installed component metadata, platform rules and pre-device discovery into:

```cpp
struct RendererNativeValidationPlan {
    NativeValidationPlanId id;
    RendererGeneration renderer;
    RenderBackendId backend;
    NativeValidationRequirement requirement;
    NativeValidationFeatureMask requested;
    NativeValidationFeatureMask realizable;
    NativeValidationFallback fallback;
    NativeValidationMessageFilter messageFilter;
    NativeValidationBreakPolicy breakPolicy;
    ProductProfileId productProfile;
    BuildProfileId buildProfile;
};
```

Plan construction is inert: it does not create a native object, register a
callback, change process environment or install a global handler. Validation
checks canonical feature combinations, profile permission, package support and
the backend's declared activation stage. A plan cannot claim post-device discovery
as proof for a feature that must be enabled before that device exists.

An optional plan contains exactly one declared fallback edge. The normal edge is
the same backend and baseline contract with native validation disabled. OpenGL's
ADR-029 one-time non-debug-context retry is this edge. A fallback cannot lower API
version/profile, change hardware adapter, select another renderer, use software
rasterization, remove a separately required feature or alter project quality.
Required plans contain no disabled fallback.

Failures distinguish `ValidationProfileDisabled`, `ValidationFeatureUnsupported`,
`ValidationRuntimeUnavailable`, `ValidationConfigurationInvalid`,
`ValidationActivationFailed`, `ValidationCallbackInstallFailed` and
`ValidationProcessRestartRequired`. They retain backend, activation stage,
requested feature and bounded native cause without exposing native handles.

### 3. Realized state, not request, is authoritative

After native initialization the backend publishes one generation-scoped realized
state; failed initialization returns the same evidence with the typed error rather
than publishing an active state:

```cpp
struct RendererNativeValidationState {
    NativeValidationPlanId plan;
    RendererGeneration renderer;
    DeviceGeneration device;
    NativeValidationFeatureMask active;
    NativeValidationCallbackState callback;
    NativeValidationRealization realization;
};
```

`realization` is `Disabled`, `Active`, `Partial` or `Failed`. `Partial` is valid
only for an `Optional` request whose plan declared the exact supported subset.
The state records safe API/runtime/layer versions and activation evidence needed
for qualification. Request, reported availability and active state remain
different values.

Marker, capture, crash-bundle and inspector code consumes the realized state or
effective capability derived from it. It cannot assume that a requested layer is
active. Conversely, lack of a native layer never disables Horo's frontend
validation, typed errors or ADR-041 ingestion.

### 4. Native activation stays inside private backend adapters

Each adapter performs activation at the earliest API-required stage:

| Backend | Required ordering and boundary |
|---|---|
| OpenGL | Apply debug-context intent before window/context creation. After the context is current on its owner thread, install the qualified debug callback and filters. Optional debug-context failure may take ADR-029's single complete-rollback retry without the debug flag. |
| Vulkan | Validate requested instance layers/extensions before instance creation, chain debug creation data when needed for creation/destruction findings, create the messenger for the accepted instance and destroy it before the instance. Device-time validation features are enabled only when admitted before device creation. |
| Metal | Resolve the host launch/environment contract before renderer startup. The backend observes and verifies realized validation/tool state; it does not mutate process environment after launch or claim runtime activation when the platform exposes none. Command-buffer error/detail options remain separately capability-checked. |
| D3D12 | Enable requested debug/GPU-based validation through the private debug interfaces before factory/device work at the API-required point, then configure the information queue after device creation. Missing requested facilities do not trigger WARP or another backend. |
| Null | Perform no native activation and report native validation unsupported. It still validates plans, fallbacks, lifecycle and synthetic callback translation deterministically. |

Native API symbols, layer names, environment keys, callback types and objects stay
private. The public plan describes Horo intent and result, not a portable union of
native flags. A backend cannot enable a facility that the host plan disabled.

The backend owner tracks each acquired native registration as an explicit
lifecycle step. Partial failure unwinds in reverse order. An optional activation
attempt must completely remove callbacks, messengers, contexts and temporary
objects before its declared disabled retry begins; no state from the failed
attempt becomes active evidence.

### 5. All messages enter through the ADR-041 adapter

Every active native source has a validated mapping table from native
source/type/severity/message identity to registered ADR-041 codes, kind,
subsystem, severity and bounded fields. Known equivalent conditions use the same
Horo code across backends. Unknown messages use the bounded
`render.backend.native_message_unknown` family and preserve API family plus stable
native message ID when available.

Callbacks retain optional bounded redacted native text as evidence. They never
expose pointer values, GPU addresses, unrestricted object labels, shader/resource
contents, arbitrary paths or environment dumps. Message text is not control flow,
test identity or a metric dimension. Native severities cannot downgrade Horo
invariant/fatal classifications.

Callback submission uses the generation-bound non-blocking ingest port. It does
no UI work, file/network I/O, sink flush, fallback, renderer mutation, arbitrary
allocation or renderer-lock acquisition. Filters are compiled into fixed lookup
data before installation. Repetition, unknown-ID cardinality, string bounds,
queue saturation and emergency delivery follow ADR-041; the validation adapter
does not create a second queue or log file.

Driver callback threads carry the immutable renderer/device/plan generation
captured at registration. Work-specific context is included only when the backend
can prove the association; missing frame/pass/resource correlation remains
explicit absence. A callback never reads transient thread-local render context.

### 6. Filters and break behavior are bounded local policy

The host request may set a finite allow/deny table of registered Horo codes and
stable native numeric IDs, plus a minimum severity. Wildcard regular expressions,
driver-message text matching and runtime-growing suppression sets are forbidden.
The default table contains only reviewed noisy-message exceptions with backend,
runtime/driver range, owner and removal rationale. A filter suppresses reporting;
it cannot make a failed operation succeed or hide Horo's authoritative typed
error.

Break policy is `Never`, `FatalOnly` or `ErrorAndFatal` and is permitted only by
an attached local developer/qualification profile. It schedules a host-owned
safe debugger break signal after bounded admission; a native callback does not
trap while holding API/driver state. Packaged Shipping configurations resolve to
`Never`. Absence of a debugger cannot block, crash or change renderer behavior.

Runtime filter edits are not supported for version 1. A change creates a new
request and follows the restart boundary, preventing callbacks from racing with
mutable policy storage.

### 7. Work and memory are finite

Version 1 admits at most 256 explicit native-ID filter entries and 256 registered
code filter entries per plan; each hard maximum is 1,024. Mapping tables and
normalized lookup storage are allocated and validated before activation. The
plan's memory is charged to renderer diagnostics CPU memory and remains immutable
for the generation.

Native callback events consume ADR-041's existing 2,048-event default queue,
aggregate table and emergency capacity; this ADR creates no independent message
budget. A callback formats only fixed bounded fields into preallocated/thread-safe
submission storage. If normal capacity is saturated, ADR-041 aggregation/drop
summaries apply. `Error` and `Fatal` use its bounded emergency path. Native APIs
whose callback contract cannot satisfy non-blocking bounded submission are polled
at a documented render safe point with a fixed per-drain record/time budget.

Validation overhead is measured during backend qualification by exact backend,
OS/runtime/driver, feature set, workload and build/product profile. It is not
reported as normal renderer performance. Expensive GPU-assisted validation is
never enabled by a generic Debug default and cannot be toggled during a live
device generation.

### 8. Failure, device loss and shutdown are explicit

If required activation or callback installation fails, backend initialization
fails and unwinds. If optional activation fails, the backend may take only the
plan's declared disabled retry and emits one ADR-041 availability/degradation
event after the successful retry. If that retry fails, its typed startup failure
is authoritative and the validation-attempt failure is retained as a cause.

A callback failure after activation closes that callback generation at the next
safe boundary. Core rendering may continue only when validation was optional; a
required qualification session becomes failed with its typed cause. No per-message
reinstall loop is permitted. A callback finding itself does not trigger backend
fallback, quality reduction, device recreation or process termination.

Device loss closes acceptance for the old device generation, drains or
generation-tags already admitted messages, unregisters device-scoped sources on
their required owner threads, then destroys native state. Recreation resolves and
realizes a new plan; it cannot reuse the old callback, messenger, information
queue or context registration.

Shutdown order is: stop new frontend work, close validation emitters, disable new
native callbacks/poll admission, unregister/drain according to the native API,
destroy validation objects, then destroy the device/context/instance. Accepted
events retain owned bounded values and may drain asynchronously. Late callbacks
are rejected by generation without dereferencing destroyed state. Shutdown is
idempotent after every partial-initialization stage and never waits for GPU idle
solely for diagnostics.

### 9. Qualification proves parity without pretending equivalence

All interactive backends implement the same request, plan, realized-state, typed
failure, ADR-041 projection and lifecycle semantics. Native feature sets need not
be identical. Each adapter reports the exact supported intent and omissions;
backend code does not fabricate parity by dropping unsupported options.

Null deterministic tests cover configuration precedence, plan validation,
required/optional fallback, filters, mapping, saturation, stale generations,
partial initialization and repeated shutdown. Null cannot prove native layer
availability, message identity/severity mapping, callback threading, driver
coverage or overhead.

Hardware qualification for OpenGL, Metal, Vulkan and D3D12 records exact native
facility/version, startup arguments/environment, active feature set, representative
known and unknown messages, callback thread behavior, rollback stage and measured
overhead. Shipping-package tests prove that absent development tooling does not
break a disabled plan. Required developer/qualification tests prove that missing
facilities fail rather than silently disabling validation.

## Migration And Verification

Existing build-condition debug-context flags, direct native logging and mutable
message filters migrate to the host request, private activation adapters and
ADR-041 mapping tables. Existing ADR-029 OpenGL retry remains the declared
optional fallback rather than a general silent-disable precedent. Marker support
continues to use ADR-044's separate plan and cannot activate validation.

Tests must cover:

- CLI/project/host precedence, Shipping prohibition, Debug-build non-implication
  and restart-required changes;
- duplicate/invalid feature combinations, profile-disabled requests, exact
  required rejection and only the declared optional disabled fallback;
- every backend activation stage, unavailable layer/tool/runtime, partial failure,
  complete reverse rollback and no backend/adapter/API downgrade;
- realized-state evidence, optional partial features and no request-to-active
  inference by markers, capture or inspector consumers;
- known/unknown native mapping, severity rules, bounded redaction, explicit absent
  work correlation and no native handles/addresses/private contents;
- callback and polled-source affinity, non-blocking bounded admission, filter hard
  limits, saturation summaries and emergency delivery;
- optional callback failure, required qualification failure, device loss,
  recreation, late callbacks and repeated partial/full shutdown;
- deterministic Null fixtures and native OpenGL/Metal/Vulkan/D3D12 qualification;
  and
- disabled Shipping startup without developer tools plus measured validation
  overhead under exact declared configurations.

## Consequences

Validation behavior becomes reproducible from an immutable startup request and
realized evidence instead of build-type convention. Native detail reaches one
bounded backend-neutral diagnostic path, and required qualification cannot pass
after an invisible disable. The cost is backend-specific staged activation,
mapping tables, rollback tests and platform qualification for facilities whose
native capabilities are intentionally unequal.

## Rejected Alternatives

| Option | Decision and reason |
|---|---|
| Enable all validation in Debug builds | Rejected: build type does not prove installed support, acceptable cost or product intent. |
| Toggle validation after device creation | Rejected: several APIs require activation before instance/context/factory/device creation. |
| Let each backend log native callback strings | Rejected: bypasses stable identity, bounds, privacy, correlation and retention. |
| Require development layers in every player package | Rejected: native diagnostics are tooling, not baseline rendering correctness. |
| Silently disable a missing required layer | Rejected: makes qualification evidence false and hides environment drift. |
| Fall back to another backend or software adapter | Rejected: validation policy cannot change renderer/device selection. |
| Break directly inside native callbacks | Rejected: callbacks may run under driver/API state on uncontrolled threads. |
| Match suppression by raw message text | Rejected: text is unstable, high-cardinality and unsuitable as identity. |
| Treat Null tests as native qualification | Rejected: Null proves shared contracts, not driver facilities, threading or overhead. |
