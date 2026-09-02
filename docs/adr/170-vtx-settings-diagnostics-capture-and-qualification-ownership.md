# ADR-170: VTX Settings, Diagnostics, Capture and Qualification Ownership

- **Status**: Proposed
- **Date**: 2026-09-02
- **Supersedes**: None
- **Scope**: VTX settings/preflight, snapshots, telemetry, debug commands, capture privacy/limits and release qualification
- **Issue**: [VTX-007.1](https://github.com/abdullahbodur/horo-engine/issues/2233)
- **Jira**: [HORO-2187](https://horo-engine.atlassian.net/browse/HORO-2187)
- **Parent**: [VTX-007](https://github.com/abdullahbodur/horo-engine/issues/2232)
- **Related**: [ADR-009](009-configuration-schema-precedence-and-secret-boundary.md), [ADR-018](018-command-registration-permissions-threading-and-packaged-build-policy.md), [ADR-028](028-renderer-capability-limits-and-product-profiles.md), [ADR-041](041-backend-neutral-renderer-diagnostics-model.md), [ADR-043](043-gpu-memory-and-resource-inspection.md), [ADR-047](047-renderdoc-pix-and-metal-capture-integration.md), [ADR-051](051-renderer-benchmark-and-regression-gates.md), [ADR-164](164-virtual-texturing-ownership-product-scope-and-capability-tier.md), [ADR-166](166-vtx-feature-local-residency-and-eviction-within-global-reservations.md), [ADR-167](167-vtx-feedback-readback-prediction-and-camera-data-ownership.md), [ADR-168](168-vtx-gpu-page-table-physical-cache-shader-and-material-ownership.md)
- **Normative documents**: [Virtual Texturing Architecture](../architecture/runtime/virtual-texturing-architecture.md), [Metrics and Profiling](../architecture/observability/observability-performance.md)

## Context

VTX tooling lacks an authority contract. A panel could mutate mappings, a setting could
bypass admission, per-page metrics could create unbounded cardinality, captures could
persist camera/content data without limits, and Null tests could be mistaken for native
Atlas/Sparse proof. Configuration, observation, control, capture and qualification must
remain separate from runtime authority.

## Decision

### 1. Responsibilities have explicit owners

| Responsibility | Owner | Deliberate non-owner |
|---|---|---|
| Typed setting schema, precedence and provenance | Configuration under ADR-009 | UI/VTX do not read ambient files/environment |
| Effective tier/budget/fallback/artifact/diagnostics plan | Application host preflight | Backend/panel/profile rank does not select support |
| Logical request/pin/residency snapshot | VTX | Renderer/tools cannot reinterpret logical state |
| Physical mapping/resource/feedback snapshot | Renderer | VTX/tools receive no native handles |
| Metrics, logs and traces | Observability | Per-page metric series are forbidden |
| UI/CLI/MCP inspection and control | Presentation over application queries/commands | Presentation owns no runtime state |
| Capture storage/privacy/retention | Capture/storage owner | VTX does not write arbitrary paths |
| Native qualification/release gate | Qualification/Release under ADR-051 | Null tests are not release evidence |

Descriptors are inert: registration performs no allocation, subscription, file access,
dynamic metric creation or mutation.

### 2. Settings resolve to one immutable effective plan

Host preflight intersects typed settings/provenance with content requirements, product
target/profile, Renderer capability, ADR-166 budgets, artifact/material variants and
privacy/build policy. It publishes a generation-tagged plan or typed rejection and
records every fallback. Tier, geometry, artifacts/shaders, budgets or capture-capability
changes require new preflight and admitted replacement. Only a closed declared subset
updates through safe-point owner commands.

Unknown required fields fail. Paths, secrets and native device strings are excluded.
Headless and 1.0 products retain ADR-164 `Unavailable` behavior.

### 3. Inspection is immutable, bounded and generation checked

VTX publishes aggregate lifecycle/mip/reason counts, reservations, pins, queues, demand/
eviction outcomes and typed failures. Renderer publishes correlated Horo resource
generations, realization mode, admitted/actual/pending-retirement costs and mapping/
feedback summaries.

Page detail is a paginated query with snapshot generation, stable cursor and count/byte/
time limits. It returns Horo values, never pointers, native descriptors, GPU addresses
or mutable references. Stale cursors fail typed. UI rendering performs no blocking I/O,
GPU readback or unbounded frame-thread sort/filter. Retention and consumer leases are
finite.

### 4. Metrics remain low-cardinality

Pre-registered metrics may use finite tier, stage, result-class and host-role dimensions.
Texture/page/material/cell/camera/view/operation identities, paths, device names and
arbitrary error strings are prohibited labels. Detail belongs in rate-limited events,
profiler traces, snapshots or explicit captures.

Over budget diagnostics aggregate/sample and increment one dropped counter instead of
allocating more series. Instrumentation cannot change residency, priority, graph order
or fallback.

### 5. Control is an authorized application operation

Flush, preload, preference change, fault injection and capture carry permission, target
generation, declared cost, cancellation and typed result. They revalidate at owner safe
points and route through ADR-166 reservations and ADR-168 Renderer operations. Flush
cannot remove pins or skip retirement; preload cannot bypass admission. UI/CLI/MCP use
the same operations.

Customer builds expose no developer controls unless explicitly included/authorized.
Fault injection is deterministic test/developer-only and cannot target arbitrary paths,
addresses or native handles.

### 6. Captures are finite privacy-classed artifacts

A capture request declares purpose, privacy, frame/time/page/byte limits, categories,
redaction, owner-managed destination and retention; capacity is reserved first. Default
content is schemas, plan fingerprint, safe generations, aggregates and bounded samples.
Source/page bytes, paths, raw camera trajectories, identities, credentials, provider
URLs and native handles are excluded unless a higher privacy policy authorizes them.

Limit exhaustion produces marked truncation and dropped counts, never silent growth.
Storage uses structured paths, atomic publication, hashes and cancellation cleanup.
Native RenderDoc/PIX/Metal capture remains ADR-047 authority.

### 7. Qualification is target, tier and workload specific

Evidence records source/build/toolchain, product plan, artifact/material/shader
fingerprints, OS/device/backend/driver, VTX mode, workload, sampling policy, budgets and
statistics. It is immutable/content-addressed and invalidates when the cohort changes.

Atlas and Sparse qualify independently per shipped target/backend. Suites cover cook/
provider determinism, residency/pressure, sampling, feedback loss, overload,
cancellation/replacement/device loss, retirement, visuals and CPU/GPU/I/O/memory/frame
metrics. Thresholds are versioned before runs.

Null/Memory providers prove contracts and injected failures only. Missing hardware,
baseline or evidence is `Unqualified`, never pass; a developer capture or one successful
run is not release proof.

### 8. Errors, lifecycle and migration fail closed

Typed outcomes distinguish invalid settings/preflight, stale/capacity snapshot,
unauthorized/stale command, dropped telemetry, capture privacy/budget/storage/
truncation/cancellation and qualification environment/baseline/regression/expiry.
Replacement creates new settings/snapshot/capture generations. Old readers may finish
under leases but cannot command new state. Shutdown closes operations and retains
Renderer/storage leases until acknowledgement.

Prototype direct-container panels, mapped buffers and ad hoc JSON migrate to snapshot/
query/command/capture contracts. Per-page metrics migrate to aggregates. Environment/
profile-name toggles migrate to typed preflight. Screenshots/log folders do not become
qualification evidence.

## Consequences

- Tools share one bounded truth without owning it; telemetry remains safe at scale.
- Detail requires pagination, mutations are asynchronous, and qualification is maintained
  per target/backend/tier cohort.
- Captures gain explicit privacy, size, integrity and retention rules.

## Rejected Alternatives

### Let debug UI mutate VTX or Renderer directly

Rejected because presentation would become a lifetime/admission authority and adapters
would diverge.

### Emit one metric series per texture or page

Rejected because content-dependent cardinality turns diagnostics into resource failure.

### Capture every page, path and camera sample by default

Rejected because size, confidentiality and privacy are unbounded.

### Treat Null tests or developer captures as native qualification

Rejected because they cannot prove mapping, synchronization, shader correctness, driver
behavior or production performance.
