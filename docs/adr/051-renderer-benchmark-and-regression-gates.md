# ADR-051: Renderer Benchmark and Regression Gates

- **Status**: Proposed
- **Date**: 2026-09-01
- **Supersedes**: None
- **Scope**: Renderer benchmark workloads, measurement cohorts, variance policy, baselines and CI regression gates
- **Issue**: [RND-017.11](https://github.com/abdullahbodur/horo-engine/issues/443)
- **Jira**: [HORO-443](https://horo-engine.atlassian.net/browse/HORO-443)
- **Companion decisions**: [ADR-028](028-renderer-capability-limits-and-product-profiles.md), [ADR-042](042-cpu-gpu-timestamps-and-pipeline-statistics.md), [ADR-043](043-gpu-memory-and-resource-inspection.md), [ADR-046](046-gpu-driver-compatibility-and-workaround-registry.md), [ADR-050](050-cross-backend-reference-image-tests.md)
- **Normative documents**: [Rendering Architecture](../architecture/runtime/rendering-architecture.md), [Render Backend Parity Contract](../architecture/runtime/render-backend-parity-contract.md), [Metrics And Profiling](../architecture/observability/observability-performance.md), [Testing Architecture](../architecture/delivery/testing-architecture.md), [Quality And CI](../architecture/delivery/quality-and-ci.md)

## Context

Renderer performance changes with backend, device, driver, power state,
instrumentation, workload and scene revision. Comparing one local average FPS to
an unrelated CI run creates false regressions and false confidence. A rolling
average without cohort admission or minimum sample rules can also drift when slow
runs disappear, hardware changes or a real regression becomes the new normal.

ADR-042 defines trustworthy timestamp/statistic samples but intentionally does not
define benchmark workloads or gates. ADR-043 defines memory observations without
making them benchmark thresholds. Horo needs reproducible workloads, exact
environment identity, bounded measurement, explicit unstable/unsupported states
and objective gates that preserve enough evidence for triage.

## Decision

### 1. Benchmarks are explicit application operations

The benchmark host composes `RendererBenchmarkService` from narrow fixture,
renderer lifecycle, ADR-042 measurement, ADR-043 aggregate memory, process
resource, environment and artifact capabilities. It uses the production renderer
path and does not expose native device/query objects to generic benchmark code.

```text
benchmark runner -> RendererBenchmarkService -> typed benchmark result
                              |
                              +-> immutable workload fixture
                              +-> explicit renderer/backend/device
                              +-> ADR-042 measurement plan
                              +-> cohort baseline + gate evaluator
```

Workload descriptors are inert. Discovery/validation cannot activate a backend,
create a surface, enable instrumentation, register services or publish a baseline.
Application composition selects one explicit backend/device and operation. There
is no fallback to another backend, adapter, profile, resolution or workload.

### 2. A workload fixes every performance-relevant input

```cpp
struct RendererBenchmarkDescriptor {
    RendererBenchmarkId id;
    RendererBenchmarkRevision revision;
    RendererBenchmarkKind kind;
    RendererWorkloadFixture fixture;
    EffectiveCapabilityPredicate requirements;
    RendererBenchmarkMeasurementPlan measurements;
    RendererBenchmarkGatePolicy gates;
    RendererBenchmarkLimits limits;
};
```

Kinds are `FrontendCpu`, `NativeFrame`, `PassOrFeature`, `ResourcePressure` and
`Presentation`. A descriptor declares content/fixture hashes and shader/build
recipe revisions; camera, viewport, output format, sample count and render scale;
quality/product profile; fixed step, time trace and random seeds; frame-in-flight/
present policy; streaming and cache state; required capability path; warm-up and
measured frame/iteration counts; instrumentation plan; and expected CPU/GPU/
memory/resource budgets. Candidate source, binary and compiled shader hashes are
result provenance, not workload identity that would prevent comparison with an
earlier build.

Adaptive quality, dynamic resolution, automatic exposure, uncontrolled streaming,
editor interaction, wall-clock animation and ambient project/settings state are
disabled. A workload intentionally measuring one adaptive or streaming policy
declares its full input trace and reports transition/steady windows separately.
Shader/pipeline warm-cache and cold-cache workloads are different IDs; a run
cannot mix them.

Every descriptor has one focused hypothesis, such as frontend submission scaling,
opaque frame GPU latency, transient aliasing pressure or presentation CPU wait.
It cannot gate unrelated metrics merely because they are available. ADR-050
reference scenes may be reused as immutable content, but visual correctness and
performance outcomes remain separate.

Capability resolution is `RequiredAndSupported`, `OptionalPathSelected`,
`ExpectedUnsupported` or `UnexpectedUnsupported`. Expected unsupported passes only
when admission returns the descriptor's exact typed reason before measurement.
Missing capability never selects a cheaper path and reports benchmark success.

### 3. Environment identity defines a comparable cohort

```cpp
struct RendererBenchmarkCohortIdentity {
    RendererBackendId backend;
    RendererAdapterIdentity adapter;
    DriverVersion driver;
    OsVersion os;
    CpuTopologyClass cpu;
    MemoryClass memory;
    PowerThermalPolicy power;
    EffectiveCapabilitiesRevision capabilities;
    CompatibilityPolicyFingerprint workarounds;
    RendererInstrumentationPlanId instrumentation;
};

struct RendererBenchmarkRunProvenance {
    SourceCommitIdentity source;
    BuildArtifactIdentity build;
    ShaderArtifactSetIdentity shaders;
    GpuAdapterInstanceId adapterInstance;
    RendererRunnerIdentity runner;
};

struct RendererBenchmarkEnvironment {
    RendererBenchmarkCohortIdentity cohort;
    RendererBenchmarkRunProvenance provenance;
};
```

A comparison cohort requires exact workload/revision, platform/architecture,
backend, adapter hardware class, comparable driver namespace/version, OS/runner
image, CPU topology class, memory class, effective profile/capability fingerprint,
compatibility policy, build type/configuration, compiler/toolchain/dependency-lock
identity and instrumentation semantic revision. Source commit, executable/renderer/
shader artifact hashes and adapter instance remain full provenance and are
expected to differ across candidate and baseline builds; they never define cohort
equality. Display/present mode and power/thermal policy also match exactly.

Baseline policy may deliberately widen only fields proven irrelevant by the
workload, using a reviewed typed cohort descriptor. It never groups by raw strings
or silently compares different device classes. An unmatched environment returns
`BaselineUnavailable`; it cannot borrow the nearest faster/slower machine.

Before each native iteration, the lane verifies AC/performance power policy,
declared display/present state, no active debugger/capture/validation layer,
available memory/disk and temperature/frequency stabilization when sensors are
qualified. Unavailable optional sensors are recorded. A declared required
condition outside bounds makes the iteration `EnvironmentInvalid`, not a slow
sample added to the baseline.

### 4. Measurement has fixed warm-up, windows and instrumentation

Native frame workloads default to 300 warm-up frames followed by 1,200 measured
real frames and seven independent process iterations. The measured window must
also span at least 20 seconds; it extends to satisfy both conditions up to a hard
maximum of 10,000 frames or five minutes per iteration. Short focused CPU
microbenchmarks declare operation count and minimum one-second windows instead.

Warm-up establishes shader/pipeline caches, resource residency and temporal
history but contributes no gated samples. The service starts measurement only
after the exact warm-up count and declared readiness predicates. Frame loss,
device loss, graph/profile revision, resize, backgrounding or unexpected cache
mutation invalidates the iteration; samples are not spliced around the event.

ADR-042 plan identity is fixed before warm-up. Default frame gates use always-on
frame totals only. Detailed pass scopes and pipeline-statistic queries are disabled
unless the workload specifically measures their cost, in which case they form a
different cohort. Results with invalid/disjoint calibration, stale generation or
partial required queue coverage are invalid, not zero.

The service records every admitted measured frame in a bounded sample buffer and
computes deterministic binary64 aggregates in fixed order. It does not drop slow
frames, winsorize, sigma-clip or remove outliers. A descriptor may exclude only a
typed invalid frame established independently of metric magnitude, and reports all
excluded IDs/reasons. If valid sample count falls below 99% of the declared window,
the iteration is `InsufficientSamples`.

### 5. Canonical metrics preserve non-overlapping meanings

Required frame metrics use ADR-042 semantics:

- `cpu_frame_p50`, `cpu_frame_p95`, `cpu_frame_p99` from `engine.frame.cpu_time`;
- `gpu_frame_p50`, `gpu_frame_p95`, `gpu_frame_p99` only from a qualified whole-
  frame GPU span;
- `present_cpu_p50` and `present_cpu_p95` separately from frame/GPU time;
- throughput as successful real frames per measured steady duration, used only by
  throughput workloads; and
- frame/sample failure, measurement age and dropped-result counts.

ADR-043 memory gates use peak committed backing, peak resident bytes, peak
transient capacity and declared pool/alias efficiency without summing overlapping
totals. Process peak resident memory is a separate host metric. Resource counts,
draw/dispatch/submission counts and pipeline statistics gate only descriptors that
declare their canonical source/semantic revision.

CPU and GPU durations are never added into one “total”. Average FPS is diagnostic,
not a gate. A metric unavailable under the required path yields
`RequiredMetricUnavailable`; optional metrics remain explicit and cannot satisfy a
gate. Units, percentile algorithm and histogram/sample schema are versioned.

### 6. Baselines are robust, finite and cannot absorb candidates silently

For each metric/cohort, the rolling baseline contains the most recent 5 to 20
eligible protected-branch runs completed during the preceding seven days. Each
run contributes its median across seven valid process iterations for each
reported percentile/peak metric. The cohort baseline is the median of those run
values; dispersion is median absolute deviation (MAD) and the manifest retains
all source run IDs/values.

A run is eligible only if its commit was on the protected baseline branch before
the candidate, all required cases passed, environment/cohort identity matched,
the lane was not quarantined and no baseline freeze/exclusion applied. Candidate,
failed, manually retried, pull-request and post-candidate runs never enter the
comparison baseline. A passing candidate is admitted only after merge by the next
protected baseline publication; no failure can teach the baseline automatically.

Fewer than five eligible runs, a source older than seven days, schema change or
cohort change produces `BaselineUnavailable` and blocks required qualification.
A reviewed seed baseline may bootstrap a new cohort with at least seven valid
iterations and explicit expiry, but cannot be copied from another cohort.

Baseline data and manifests are immutable/content-addressed, checksum validated
and atomically published. Manual exclusion requires owner, reason and source run;
it remains visible in the next baseline manifest. Recomputing history cannot
rewrite an already used candidate comparison.

### 7. Regression gates combine relative, absolute and stability bounds

Unless a descriptor tightens them, default candidate gates compare the median of
its seven iteration-level values against the cohort baseline:

| Metric | Regression when both conditions hold |
|---|---|
| CPU/GPU frame p50 or p95 | increase `> 5.0%` and `> 0.20 ms` |
| CPU/GPU frame p99 | increase `> 8.0%` and `> 0.50 ms` |
| Present CPU p50/p95 | increase `> 8.0%` and `> 0.20 ms` |
| Throughput | decrease `> 5.0%` and `> 1.0 frame/s` |
| Peak GPU committed/resident or process resident memory | increase `> 5.0%` and `> 16 MiB` |
| Declared count/capacity metric | increase beyond descriptor's exact/relative bound |

Equality at the threshold passes. Relative change uses the positive baseline as
denominator; a zero/non-finite baseline is invalid. Latency/memory use
`candidate - baseline`; throughput uses `baseline - candidate`. Checked arithmetic
precedes unit conversion.

The candidate fails stability only when both a relative and metric-appropriate
absolute iteration-MAD bound are exceeded:

- p50/p95 latency: `3.0%` and `0.05 ms`;
- p99 latency: `5.0%` and `0.10 ms`;
- throughput: `3.0%` and `0.50 frame/s`; and
- memory peaks: `5.0%` and `4 MiB`.

Declared count/capacity metrics own an explicit stability rule or are exact-only.
A tighter descriptor rule may apply. Baseline dispersion above the same paired
limits freezes the gate as `BaselineUnstable`; it does not widen thresholds. The
lane emits source iterations for triage. Equality at either stability threshold
does not fail.

A descriptor can tighten any gate. Loosening requires a local rationale, owner,
before/after evidence on the same cohort and review in the same change. Runtime
environment variables cannot alter thresholds. A known intentional regression is
approved by updating the descriptor/budget with evidence, not by deleting old
baseline samples or adding the candidate to its own baseline.

### 8. Results and artifacts make failures actionable

```cpp
struct RendererBenchmarkResult {
    RendererBenchmarkRunId run;
    RendererBenchmarkId workload;
    RendererBenchmarkEnvironment environment;
    RendererBenchmarkOutcome outcome;
    RendererBenchmarkSampleManifest samples;
    RendererBenchmarkBaselineManifest baseline;
    RendererBenchmarkGateResults gates;
    RendererBenchmarkArtifactManifest artifacts;
};
```

Outcomes are `Passed`, `ExpectedUnsupported`, `UnexpectedUnsupported`,
`EnvironmentInvalid`, `RequiredMetricUnavailable`, `InsufficientSamples`,
`BaselineUnavailable`, `BaselineInvalid`, `BaselineUnstable`, `Regression`,
`TimedOut`, `DeviceLost`, `BudgetExceeded`, `Cancelled` and
`InfrastructureFailed`.

Every non-pass identifies workload/revision, backend/device/environment cohort,
capability and compatibility revisions, measurement plan/clock coverage,
operation/phase and stable actionable cause. Regression artifacts include
candidate iteration values/distributions, baseline source values/MAD, exact gate
math, excluded sample reasons, resource/measurement availability, bounded ADR-041
diagnostics and ADR-044 marker correlation.

Artifacts are versioned JSON/JUnit plus bounded histogram/sample data and optional
profiler capture only when separately armed. They use generated relative paths,
checksums and atomic manifest-last publication under the CI/test result root. No
native handle/address, machine-absolute path or unrestricted user/project content
is included. Upload is CI ownership and never occurs from the renderer.

### 9. Work, retries and CI lanes are bounded

Defaults permit one benchmark process/device, one candidate workload in flight,
256 MiB sample/intermediate memory and 512 MiB artifacts. Hard maxima are two
non-overlapping devices, 1 GiB memory and 2 GiB artifacts per workload. Aggregate
lane CPU/GPU time, memory, disk and parallel-device budgets are checked before
admission. Encoding/baseline evaluation runs on cancellable workers and cannot
block renderer owner threads.

An identical iteration may retry once only after a typed infrastructure failure
before valid measured samples publish. Regression, instability, timeout, device
loss, unsupported capability and crash are outcomes, not retry reasons. Repeated
identical identity with materially disagreeing gate outcomes is
`EnvironmentInvalid`/flaky qualification and cannot choose the passing run.

Pull-request fast lanes run descriptor, baseline, gate-math, artifact and Null
fixtures plus a short non-gating native smoke when available. A documented stable
GPU lane may run affected native candidate gates. Protected branches require the
bounded representative backend/platform/hardware-class matrix. Scheduled lanes
expand driver/device classes, cold-cache workloads and saturation tests.

Lane status is `Qualified`, `Regression`, `ExpectedUnsupported`, `NotScheduled`,
`EnvironmentInvalid` or `InfrastructureFailed`. Missing required hardware or an
expired/unavailable baseline blocks qualification; it is never pass. Reports do
not compare results across cohorts to fill a matrix cell.

### 10. Cancellation, device loss and shutdown preserve ownership

Cancellation stops future iterations/frames, suppresses unpublished comparisons,
releases owned samples and waits only through normal bounded renderer completion.
Submitted ADR-042 queries retain ownership until their GPU completion token;
cancellation cannot reuse slots early.

Device/backend replacement invalidates the iteration and closes old-generation
admission. It cannot continue the window on a new generation. Shutdown rejects
new work, cancels queued/active workloads, retires submitted measurement resources,
joins comparison/publication workers and then destroys service/backend state. It
is idempotent after partial initialization and never waits for global GPU idle,
network upload or UI.

Null supplies deterministic timing/memory/count streams and injected unsupported,
partial, unstable, regression, timeout, cancellation, device-loss, corrupt-
baseline and publication failures. It proves orchestration/gate math/lifecycle but
cannot establish native performance, measurement overhead or a hardware baseline.

## Migration And Verification

The vague global “frame time or memory >5%” check migrates to registered workload-
local metrics and dual relative/absolute gates. Existing microbenchmarks add typed
environment/sample manifests before becoming CI gates. Historical values lacking
cohort/schema provenance remain trend data and cannot seed gates automatically.

Tests must cover descriptor/cohort/schema/hash validation; exact environment
matching; warm-up/window boundaries and insufficient samples; fixed percentile/
MAD arithmetic; every equality/just-over gate boundary; zero/non-finite baselines;
latency/throughput/memory directions; baseline eligibility, expiry/freeze/seed and
candidate exclusion; unsupported/partial timestamps; invalid calibration, timeout,
device loss, cancellation and shutdown; aggregate admission/backpressure;
interrupted artifact publication; deterministic Null fixtures; and native
backend/hardware workloads on documented lanes.

## Consequences

Renderer regressions become reproducible, cohort-correct and actionable instead
of comparisons between unrelated averages. Baselines cannot silently absorb a
failure, noisy lanes cannot widen gates and missing hardware remains visible. The
cost is stable hardware ownership, repeated iterations, baseline storage, longer
protected/scheduled lanes and disciplined workload revision management.

## Rejected Alternatives

| Option | Decision and reason |
|---|---|
| Compare one local run with the latest CI value | Rejected: environment and random variance dominate the signal. |
| One baseline across hardware/backends | Rejected: unrelated performance classes make thresholds meaningless. |
| Gate only average FPS | Rejected: hides tail latency, CPU/GPU ownership and presentation waits. |
| Use only a relative percentage | Rejected: tiny absolute noise fails fast workloads; dual thresholds bound both meanings. |
| Remove slow frames as outliers | Rejected: slow frames are often the regression being measured. |
| Add failed candidates to the rolling baseline | Rejected: turns persistent regressions into accepted normal. |
| Retry until one run passes | Rejected: selects noise and hides instability. |
| Enable detailed profiler/statistic queries in every gate | Rejected: instrumentation perturbation changes the workload and cohort. |
| Treat missing GPU hardware/baseline as pass | Rejected: absent evidence is not qualification. |
| Use Null timing as native performance evidence | Rejected: synthetic values prove gate logic, not hardware behavior. |
