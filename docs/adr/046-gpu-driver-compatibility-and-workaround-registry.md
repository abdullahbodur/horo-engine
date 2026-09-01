# ADR-046: GPU Driver Compatibility and Workaround Registry

- **Status**: Proposed
- **Date**: 2026-09-01
- **Supersedes**: None
- **Scope**: Versioned GPU driver rules, restrictive capability resolution and private workaround selection
- **Issue**: [RND-017.6](https://github.com/abdullahbodur/horo-engine/issues/438)
- **Jira**: [HORO-438](https://horo-engine.atlassian.net/browse/HORO-438)
- **Companion decisions**: [ADR-028](028-renderer-capability-limits-and-product-profiles.md), [ADR-029](029-opengl-core-profile-and-platform-policy.md), [ADR-030](030-metal-platform-and-feature-baseline.md), [ADR-031](031-vulkan-loader-platform-and-version-baseline.md), [ADR-032](032-d3d12-baseline-and-agility-sdk-policy.md), [ADR-041](041-backend-neutral-renderer-diagnostics-model.md)
- **Normative documents**: [Configuration System](../architecture/foundation/configuration-system.md), [Rendering Architecture](../architecture/runtime/rendering-architecture.md), [Render Backend Parity Contract](../architecture/runtime/render-backend-parity-contract.md)

## Context

ADR-028 requires effective capabilities to be the intersection of reported device
facts, implemented operations and restrictive driver policy. It deliberately
defers the registry that supplies that policy. Without one owner, backend code can
accumulate vendor-name branches, version strings can be compared lexically,
feature systems can apply different exceptions and a workaround can silently
become an undocumented capability or quality fallback.

Driver rules are safety and compatibility policy, not live telemetry. They need
reviewable evidence, deterministic precedence, bounded startup work and provenance
in every effective snapshot. They must also survive unknown or malformed driver
identity without treating uncertainty as support.

## Decision

### 1. Delivery owns immutable policy; the frontend owns resolution

Release composition selects one validated `GpuCompatibilityPolicy` before
renderer initialization. The policy is a versioned signed release artifact or an
embedded artifact covered by the engine package signature. Project assets, user
settings, plugins and remote services cannot add, remove or override rules.

The selected backend reports a canonical `GpuEnvironmentIdentity`; the renderer
frontend evaluates it with reported and implemented capability snapshots and
publishes one immutable `AppliedGpuCompatibilityPolicy` as part of the ADR-028
effective capability revision. Backends provide typed facts and execute selected
private routes. Feature systems consume effective support only and never query
the registry, vendor ID or driver string.

Descriptor creation and policy parsing are inert. They create no device, register
no service, modify no environment variable and perform no network access. A
policy revision change requires the ADR-028 quiesce/recreate transaction; rules
never revoke resources or plans during a live generation.

### 2. Environment identity is typed and provenance-aware

The canonical environment contains applicable values:

```cpp
struct GpuEnvironmentIdentity {
    RenderBackendId backend;
    BackendModuleVersion module;
    PlatformId platform;
    ArchitectureId architecture;
    OsVersion os;
    std::optional<WindowSystemId> windowSystem;
    std::optional<GraphicsRuntimeIdentity> runtime;
    GpuVendorId vendor;
    GpuDeviceId device;
    std::optional<GpuRevisionId> revision;
    DriverIdentity driver;
};
```

Vendor/device/revision are numeric IDs in the backend's declared namespace, not
marketing names. `DriverIdentity` preserves the source namespace and either a
backend-normalized comparable tuple or `Unknown` with bounded raw evidence.
OpenGL, Vulkan, Metal and D3D12 adapters normalize their platform-specific driver
forms independently; one API's tuple is never compared using another API's
ordering. Strings are evidence only and are not parsed by generic rule matching.

Missing identity remains absent. It cannot equal zero, wildcard itself into a
version-specific rule or borrow qualification from a similar product name.
General rules that do not constrain the missing field may still match. Malformed,
overflowing or contradictory identity fails capability publication with
`InvalidGpuEnvironmentIdentity`.

### 3. Rules are finite, stable and evidence-backed

Each rule has a stable canonical `GpuCompatibilityRuleId`, policy schema/revision,
owner, issue/reference, reason, introduced engine version, optional expiry/review
version, typed match predicate and one or more restrictive actions. IDs never
change meaning or get reused. A changed predicate/action receives a new rule ID;
retired rules remain in release provenance.

Predicates may constrain backend, module version, platform/architecture, OS,
window system, runtime/loader, vendor, device/revision and normalized driver range.
Ranges are inclusive/exclusive explicitly and use the identity namespace's typed
comparison. Predicates contain no regex, marketing-name substring, raw message,
locale, filesystem path or arbitrary executable expression.

Every rule cites reproducible qualification evidence: affected and unaffected
configurations, failing operation, expected semantics, verification workload and
the narrowest safe action. Broad vendor-wide rules require explicit justification.
An expired rule fails release-policy validation until removed, renewed with new
evidence or replaced; runtime does not silently ignore it.

Version 1 admits at most 4,096 rules, 32 predicates and 32 actions per rule, with
a 4 MiB decoded policy hard limit. Matching uses prevalidated indexed tables and
checked arithmetic during initialization, never per frame/resource/draw.

### 4. Actions only reduce support or preserve existing semantics

Allowed actions are:

- deny a reported feature, format/usage/sample/view combination or queue operation;
- reduce a numeric upper bound;
- increase an alignment or other conjunctive requirement;
- deny a provider/backend implementation route; or
- select a registered private workaround route that preserves the same public
  operation semantics and fits already reported and implemented support.

A rule cannot enable unreported/unimplemented support, increase a limit, weaken an
alignment, invent a format, select another adapter/backend, use software rendering,
change product profile, mutate content or suppress an authoritative typed error.
A private route declares stable route ID, owner, exact semantic equivalence,
requirements, cost class and qualification evidence before the policy references
it. The registry does not contain code, shader source, native handles or dynamic
plugin entry points.

After all actions, ADR-028 recomputes feature dependency closure and validates
formats/limits. A restriction that removes a required baseline or project feature
makes admission fail. Only the host's separately declared backend/profile/recipe
fallback policy may choose an alternative; the workaround registry never does.

### 5. Matching and composition are deterministic

All matching rules apply. Source-file order and “last rule wins” have no meaning.
Denials union, upper bounds take the minimum, alignments compose by checked least
common multiple, and route restrictions intersect. No later rule can restore
denied support. Incompatible private-route selections, unsatisfiable constraints
or overflow fail with `GpuCompatibilityRuleConflict` rather than choosing one.

Matched rule IDs are sorted canonically and hashed with policy revision,
environment identity, reported revision and implementation revision into the
applied-policy identity. Identical inputs produce byte-equivalent decisions.
Unknown fields produce `NotMatchedUnknown` evidence for predicates that require
them; they never produce a positive version-specific match.

Policy validation rejects duplicate IDs, unknown fields/actions/routes, invalid or
empty ranges, namespace mismatch, impossible bounds, unregistered ownership,
missing evidence, cycles in declared route dependencies and non-canonical order.
No partial policy becomes active. A missing/corrupt required policy fails renderer
composition; a release may explicitly embed a validated empty policy revision.

### 6. Results and diagnostics preserve provenance

The applied snapshot records policy schema/revision/digest, exact safe environment
identity, matched and unknown-dependent rule IDs, every action, before/after value,
selected private route and resulting effective capability revision. Public queries
expose Horo identities and bounded safe facts, never unrestricted registry text or
native handles.

Each applied rule emits at most one ADR-041 `Compatibility` event per renderer
generation. Equivalent repeated admission failures refer to the applied snapshot
instead of emitting per-resource events. Diagnostics use rule/action IDs as typed
fields and do not branch on message text. Metric dimensions cannot include driver,
device or rule IDs.

Policy artifacts and local diagnostics contain no user data. Normal operation
performs no download or vendor telemetry. A future policy updater must use the
signed application-update transaction, explicit release compatibility and rollback
rules; this ADR does not authorize background remote policy mutation.

### 7. Lifecycle and failure paths are generation-safe

Policy parsing/validation may run on bounded cancellable startup work, but final
matching and snapshot publication occur at the renderer initialization safe point
before resource admission. Cancellation publishes nothing. Workers own their
inputs/results and cannot retain references into package mappings after teardown.

Device loss, adapter replacement, backend replacement, OS/runtime change or module
change invalidates the applied policy and effective capabilities. Recovery re-queries
identity, evaluates the same selected policy revision and resolves product policy
before reconstruction. Old worker plans and resources cannot enter the new
revision. Failure retains no half-applied snapshot and never silently reuses old
rules against new identity.

Shutdown cancels unpublished evaluation, closes snapshot leases, then releases
policy storage after frontend consumers. It performs no GPU wait and is idempotent
after partial parse, match or publication. The immutable policy may be shared by
multiple renderer candidates, but each applied snapshot has its own environment,
renderer/device generation and lifetime.

### 8. Qualification distinguishes policy from hardware truth

Null uses bounded synthetic environment/reported/implementation fixtures to prove
parser, matching, conflict, provenance and invalidation behavior. It cannot qualify
a native driver or workaround route. Each native rule requires affected and
unaffected OpenGL/Metal/Vulkan/D3D12 lanes as applicable, exact OS/runtime/driver,
positive failure reproduction, fixed-route verification and regression tests that
the rule does not broaden support.

Release validation proves artifact signature/digest, deterministic decoding,
canonical matching, expiry ownership and packaged offline startup. Qualification
matrices remain evidence inputs; they are not converted into universal vendor
thresholds without a reviewed rule.

## Migration And Verification

Existing vendor/device/version branches migrate to registered rules or are removed.
Backend workarounds migrate to registered private routes. Existing capability
booleans remain reported/implemented facts until ADR-028 migration completes; no
second mutable effective-capability table is introduced.

Tests must cover exact/range/unknown identity matches; numeric driver ordering;
duplicate, malformed, expired and conflicting rules; conservative feature/format/
limit/alignment composition; restrictive-only invariants; deterministic digest and
rule order; bounded parsing/matching/cancellation; stale generation and recovery;
one compatibility event per rule/generation; signed offline package behavior;
synthetic Null fixtures; and affected/unaffected native qualification.

## Consequences

Driver exceptions become reviewable, reproducible inputs to one effective
capability authority. Rules cannot grant support or hide fallback, and every
restriction retains stable provenance. The cost is normalized per-backend identity,
signed policy maintenance, private-route registration and sustained native
qualification across affected and unaffected configurations.

## Rejected Alternatives

| Option | Decision and reason |
|---|---|
| Scatter vendor/version branches through backends and features | Rejected: creates inconsistent, unversioned policy and frame-path branching. |
| Compare generic driver strings lexically | Rejected: namespaces and version ordering differ by API/platform. |
| Apply only the most specific or last matching rule | Rejected: ordering can accidentally restore unsafe support. |
| Let workarounds enable missing capabilities | Rejected: policy may only restrict reported and implemented support. |
| Let projects/users disable safety rules | Rejected: compatibility is release policy, not content preference. |
| Download mutable rules during startup | Rejected: breaks reproducibility, offline operation and signed release ownership. |
| Treat Null fixtures as driver qualification | Rejected: they prove resolution logic, not native behavior. |
