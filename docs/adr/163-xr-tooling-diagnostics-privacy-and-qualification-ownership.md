# ADR-163: XR Tooling, Diagnostics, Privacy and Qualification Ownership

- **Status**: Proposed
- **Date**: 2026-09-02
- **Supersedes**: None
- **Scope**: XR project settings, setup UI, inspectors, commands, diagnostic snapshots/events/metrics/captures, privacy/cardinality, compatibility evidence, physical-device qualification, lifecycle, migration and validation
- **Issue**: [XRA-007.1](https://github.com/abdullahbodur/horo-engine/issues/2167)
- **Jira**: [HORO-2121](https://horo-engine.atlassian.net/browse/HORO-2121)
- **Related**: [ADR-018](018-command-registration-permissions-threading-and-packaged-build-policy.md), [ADR-041](041-backend-neutral-renderer-diagnostics-model.md), [ADR-042](042-cpu-gpu-timestamps-and-pipeline-statistics.md), [ADR-049](049-render-graph-and-resource-inspector-ui.md), [ADR-157](157-xr-ownership-runtime-composition-and-capability-tier.md), [ADR-158](158-openxr-loader-backend-packaging-and-host-composition.md), [ADR-159](159-xr-action-tracking-and-input-projection-ownership.md), [ADR-160](160-xr-rendering-openxr-compositor-and-renderer-ownership.md), [ADR-162](162-mixed-reality-ownership-privacy-and-capability-tier.md)
- **Normative documents**: [XR Architecture](../architecture/runtime/vr-ar-architecture.md), [Metrics and Profiling](../architecture/observability/observability-performance.md), [Application Security](../architecture/security/application-security.md), [Configuration System](../architecture/foundation/configuration-system.md)

## Context

The XR architecture requires setup/diagnostics and qualification evidence but does not
separate project intent, effective runtime state, editor presentation, captures and
release support claims. A setup panel could otherwise mutate native state directly,
serialize discovered runtime values as project policy, or call a simulator pass “device
qualified.” High-cardinality runtime/device strings, paths, serials, poses, gaze and room
geometry could leak into metrics or support bundles.

This ADR fixes the owners, typed projections, privacy/cardinality limits and evidence
levels before XR tooling implementation.

## Decision

### 1. Settings, runtime truth, tooling and qualification are separate

| Responsibility | Sole owner | Deliberate non-owner |
|---|---|---|
| Authored XR enablement, required profile, backend/loader policy and explicit fallbacks | Project/product configuration schema | Setup UI and runtime discovery |
| User-local developer device/runtime override | User settings/launch profile | Portable project data |
| Effective installed/backend/loader/runtime/system/session/capability snapshots | Application + XRRuntime owners | Editor models and widgets |
| Setup/inspector view models and draft edits | HoroEditor tooling | XR session/native backend |
| Apply/restart/recompose commands and operation status | Application command/use-case owner | Widget callbacks and DataBus |
| Diagnostic descriptors/events/metrics and bounded ingest | XR producers + Observability | Setup UI and release matrix |
| Detailed capture session, files, manifest, retention/export | Observability/capture owner | XR frame callback and editor tab |
| Privacy purpose/consent/redaction/access revision | Application Security/privacy owner | Diagnostic producer and user preference alone |
| Compatibility test result | Test/qualification runner | Successful runtime discovery |
| Supported product tuple and release gate | Release qualification authority | Simulator, editor or device marketing name |

Project settings store intent, never the active runtime name, native path, serial,
session handle, discovered extension list or last test result. Editor UI reads immutable
snapshots and invokes typed commands. It does not call OpenXR, change renderer/session
state from a draw callback or own background operation lifetime.

### 2. One typed setup model shows requested, effective and evidence separately

The setup model joins immutable revisions for authored request, installed component,
backend composition, loader/runtime/system discovery, Renderer/Input/Platform tuple,
admitted profile/capabilities, session state, privacy permissions/consent and current
qualification evidence.

Every displayed row distinguishes `Requested`, `Effective`, `Availability`, `Reason`,
`EvidenceLevel`, `SourceRevision` and whether a restart/recomposition is required. A
green runtime probe cannot overwrite a required profile or mark the tuple supported.
Unknown, unavailable, denied, stale, partial and not-run remain visible states.

Draft settings validate without side effects. Apply uses the configuration/application
transaction, writes portable fields only and schedules recomposition at its safe point.
Failure preserves prior settings/runtime generation and returns a typed operation result.

### 3. Inspectors are query/command clients

XR setup, runtime, tracking/input, rendering, interaction/MR and qualification panels
consume bounded view models. They may request refresh, preflight, start/stop session,
recenter, capture or qualification only through capability-checked application commands.

Commands name target owner/generation, operation ID, caller role, timeout/cancellation
and privacy purpose. Completion publishes to an operation store; widgets poll/observe it
without retaining callbacks into destroyed tabs. Closing a panel does not cancel runtime
unless the operation's explicit owner token says so.

Editor panels never expose mutable native structures, arbitrary runtime paths or raw
camera/gaze/room data. A redacted diagnostic ID can correlate with owner logs/captures;
display strings are not identity.

### 4. XR metrics have a finite low-cardinality vocabulary

XR metric descriptors are registered before activation and define unit, aggregation,
privacy, sampling cadence, allowed dimensions and `maxSeries`. Allowed dimensions are
finite registered values such as backend ID, product profile, capability family, lifecycle
phase, view configuration class, renderer backend and normalized outcome class.

Forbidden metric dimensions include runtime/device/headset/user names, serials, native
paths/handles, extension strings, anchor/plane/action/view/session IDs, project/scene/
entity IDs, exact error text, IP/account identity and any pose/gaze/location/geometry.
Unknown values map to a finite `other/unknown` bucket; they never create a new series.

Frame-hot producers use preallocated/bounded channels, counters/histograms and sampling.
Overflow increments one bounded drop counter. It cannot allocate unbounded labels, block
the XR frame or emit one event per pose/joint/image.

### 5. Diagnostic snapshots/events are bounded and privacy classified

Snapshots may report typed loader/runtime/system/session/capability states, normalized
versions, view/format counts and bounds, timing aggregates, tracking-validity counts,
frame/image lifecycle outcomes and redacted failure codes. Every snapshot has owner/
generation, captured-at time, completeness/staleness and explicit capacity/truncation.

High-cardinality correlation belongs only in rate-limited diagnostic events or an armed
capture, using Horo opaque IDs. Raw native handles/paths, serials and unbounded strings
remain private. ADR-159/162 sensitive data is excluded by default, including continuous
poses, joints, gaze, raw camera, room geometry and anchor tokens.

Diagnostic UI cannot reconstruct “current” state by merging snapshots from mismatched
generations. Loss/replacement makes old evidence historical/stale, not active.

### 6. Captures are explicit finite operations

An XR capture request declares purpose, channels, tuple/generations, duration/frame/byte
bounds, trigger, output owner, redaction class, encryption/access policy, retention and
export permission. Capture follows `Idle -> Armed -> Capturing -> Finalizing -> Complete /
Partial / Failed / Cancelled`, with cleanup from every state.

Baseline channels cover lifecycle/event order, normalized capability/profile plans,
frame/image/composition transitions, aggregate timing and optional deterministic replay
inputs. Sensitive channels are unavailable unless separately implemented and admitted;
requesting one cannot create an empty success.

The manifest records schema/tool/build/tuple identity, channel bounds, privacy/redaction,
checksums, truncation/drop counters and completion state. Partial/crashed/disconnected
capture is labeled honestly. General support export excludes local secrets and sensitive
XR data unless the exact export purpose is authorized.

### 7. Compatibility evidence and physical qualification are distinct

Evidence levels are:

- `StaticContract`: schema/target/dependency checks with no runtime;
- `DeterministicFake`: lifecycle/order/fault tests against fake backend/runtime;
- `SimulatorReplay`: recorded/synthetic behavioral coverage;
- `RuntimeCompatibility`: real loader/runtime/system session on a declared host tuple;
- `PhysicalDeviceQualification`: measured end-to-end headset/controller/device evidence;
  and
- `ReleaseQualified`: release authority accepts the required evidence set for a product.

Lower levels never imply higher levels. A simulator is not hardware; loader discovery
is not compatibility; one successful session is not release qualification. Results are
`Passed`, `PassedWithLimitations`, `Failed`, `Blocked` or `NotRun`, never coerced to pass.

Evidence keys the exact product/build, OS/platform host, loader/backend/runtime version,
headset/connection mode, renderer/driver/device class, profile/interaction configuration,
admitted capability set and test-plan/schema version. Changing a qualification-relevant
component invalidates or stales affected evidence rather than inheriting support by name.

### 8. Privacy and release policy bound every surface

Development tooling does not bypass shipping privacy. Permission, consent, purpose,
region/parental restrictions and access revision govern capture/inspection independently
from project settings. Shipping builds omit developer commands/channels not declared by
the product and reject ambient debug/runtime overrides under ADR-158.

Support matrices contain bounded normalized tuple identities and limitations, not raw
user/device data. Publishing/exporting evidence is a separate authorized operation.
Deleting a capture or revoking consent closes viewers/exports and follows durable
retention policy; it does not leave an editor MRU or cached preview with the payload.

### 9. Lifecycle, migration and coverage are explicit

Tooling models subscribe through generation-safe query ports and release leases on tab,
project, runtime or app teardown. Shutdown closes capture/qualification admission,
cancels/joins operations, finalizes or marks partial artifacts, disconnects view models,
then lets XR/Renderer/Platform owners stop. No inspector callback or capture hook outlives
its owner.

There is no production XR tooling to preserve. Initial work must implement typed models/
commands and bounded Observability descriptors; it must not bind widgets to native state
or store discovery/evidence inside portable project settings.

Required coverage includes requested/effective/evidence separation, portable settings,
stale-generation rejection, command authorization/cancellation, dimension whitelist and
series overflow, snapshot/capture bounds/redaction, sensitive-channel denial, partial
capture manifests, every evidence level, qualification invalidation and teardown with no
operation/view/capture lease surviving its owner.

## Consequences

### Positive

- Setup UI can explain the exact failing layer without becoming runtime authority.
- Metrics and captures remain bounded and privacy-reviewable.
- Simulator, compatibility and physical qualification claims cannot be conflated.
- Project intent stays portable and separate from local runtime/device state.

### Negative

- Tooling needs typed view models, operations and evidence stores rather than direct
  backend inspection.
- Detailed XR captures require explicit bounds, manifests and privacy policy.
- Qualification must be repeated when relevant tuple components change.

## Rejected Alternatives

### Let setup widgets call OpenXR directly

Rejected because widgets would own native lifecycle, thread affinity and rollback and
could outlive the project/session they display.

### Store discovered runtime state in project settings

Rejected because it is machine-local evidence, not portable authored intent.

### Put runtime/device identifiers in metric labels

Rejected because they create unbounded cardinality and privacy leakage.

### Treat simulator or successful discovery as device support

Rejected because neither measures a physical end-to-end tuple or satisfies release gates.

### Enable all diagnostic channels in development automatically

Rejected because build type does not grant privacy consent, retention or unbounded frame
cost.
