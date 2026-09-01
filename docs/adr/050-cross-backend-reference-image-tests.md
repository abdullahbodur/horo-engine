# ADR-050: Cross-Backend Reference Image Tests

- **Status**: Proposed
- **Date**: 2026-09-01
- **Supersedes**: None
- **Scope**: Renderer reference scenes, canonical image capture, comparison policy, artifacts and qualification lanes
- **Issue**: [RND-017.10](https://github.com/abdullahbodur/horo-engine/issues/442)
- **Jira**: [HORO-442](https://horo-engine.atlassian.net/browse/HORO-442)
- **Companion decisions**: [ADR-028](028-renderer-capability-limits-and-product-profiles.md), [ADR-041](041-backend-neutral-renderer-diagnostics-model.md), [ADR-044](044-render-markers-and-debug-labels.md), [ADR-047](047-renderdoc-pix-and-metal-capture-integration.md)
- **Normative documents**: [Rendering Architecture](../architecture/runtime/rendering-architecture.md), [Render Backend Parity Contract](../architecture/runtime/render-backend-parity-contract.md), [Testing Architecture](../architecture/delivery/testing-architecture.md), [Quality And CI](../architecture/delivery/quality-and-ci.md)

## Context

Unit and Null-backend tests prove renderer contracts but cannot prove that native
backends produce the intended pixels. A single shared golden image is also not a
sound oracle: platform rasterization, shader compiler routes, presentation
transforms and effective capabilities may legitimately differ while preserving
the same Horo semantics. Conversely, unrestricted per-machine baselines and broad
tolerances would hide actual regressions.

Reference-image validation needs reproducible scene inputs, an exact capture
boundary, bounded objective comparisons, reviewable baseline provenance and
artifacts that identify the backend/capability/operation which failed. Native GPU
availability is not guaranteed on every pull-request runner, so unsupported and
unqualified lanes must remain explicit rather than silently passing.

## Decision

### 1. One application-owned qualification service coordinates the suite

The test host composes `RendererReferenceImageService` from narrow scene-fixture,
renderer lifecycle, canonical readback, image codec, diagnostics and artifact
publication capabilities. It does not expose native devices, command objects or
image handles to generic test code.

```text
reference test runner -> RendererReferenceImageService -> typed case result
                                  |
                                  +-> deterministic scene fixture
                                  +-> render frontend + selected backend
                                  +-> canonical readback adapter
                                  +-> bounded comparator/artifact writer
```

Descriptors are inert metadata. Discovering or validating a scene/case cannot
install a backend, create a surface, register services, mutate global settings or
write a baseline. Host/application composition selects one explicit backend and
adapter before renderer activation. There is no fallback to another backend,
device, software rasterizer, scene or baseline.

### 2. Cases declare exact inputs and expected capability outcomes

```cpp
struct RendererReferenceImageCaseDescriptor {
    RendererReferenceCaseId id;
    ReferenceSceneRevision scene;
    ReferenceCapturePoint capturePoint;
    ReferenceImageContract image;
    ReferenceComparisonPolicy comparison;
    EffectiveCapabilityPredicate requirements;
    ReferenceCaseLimits limits;
};
```

Every case uses a stable registered ID and immutable cooked fixture. The fixture
declares scene/assets/materials/shader variants, camera/projection, viewport and
sample count; fixed simulation step/frame count; random seeds; exposure, color
space and output transform; product/quality profile; required and optional
capabilities; warm-up frames; capture frames; and expected resource/frame budgets.
Assets and shader outputs are content-addressed in the result manifest.

Adaptive resolution, temporal jitter sequences, animation time, particle seeds,
streaming residency, automatic exposure and other history inputs are fixed by the
case. A case that intentionally validates one adaptive feature declares its exact
input trace and convergence window. It cannot inherit editor focus, wall clock,
current project, display ICC profile or ambient driver settings.

Capability resolution produces one of:

- `RequiredAndSupported`: run the required path;
- `OptionalPathSelected`: run the explicitly declared path and record selection;
- `ExpectedUnsupported`: pass only when admission rejects with the declared typed
  capability reason before rendering; or
- `UnexpectedUnsupported`: fail with the missing/restricted capability and
  provenance.

An unsupported case never compares a blank image or reuses another profile's
reference.

### 3. Capture occurs at a named canonical render-graph boundary

Reference images use a Horo `ReferenceCapturePoint`, not a native render-target
handle or ADR-047 graphics capture. Initial points are:

- `SceneLinearBeforeOutputTransform`: RGBA16F-equivalent finite linear scene
  values after the declared scene/post-process path but before display encoding;
- `FinalSdrAfterOutputTransform`: the final logical SDR image after the declared
  Horo output transform, before platform compositor scaling/color management; and
- a registered feature checkpoint only when its resource semantics, format and
  lifetime are part of the test descriptor.

The render graph admits one bounded transfer/readback for the exact real frame,
graph execution, renderer/device/surface generation and capture point. The
backend-private adapter maps native format, swizzle, row pitch, orientation,
sample resolve and origin convention into the declared Horo contract. Generic
code never guesses these from backend names.

Normalization validates dimensions and checked byte sizes, resolves multisampling
through the production-declared resolve semantics, converts channel order, removes
row padding, flips only when the adapter declares a native origin difference and
canonicalizes negative zero. HDR non-finite values fail before comparison with
pixel/channel coordinates. SDR comparison uses exact 8-bit canonical sRGB bytes;
HDR comparison uses finite IEEE binary32 linear RGBA values decoded from the
captured contract. No lossy image codec is an oracle input.

Readback cannot wait for global GPU idle. It uses the normal submission/fence
lifecycle, a finite timeout and generation-safe resource ownership. Capture does
not alter pass policy, shader variants, quality, validation or compatibility
rules beyond the descriptor's explicit readback operation.

### 4. Backend-specific baselines and cross-backend invariants are distinct

A versioned baseline key is:

```text
case ID + scene revision + capture point + image-contract revision
+ backend ID + platform/architecture + effective profile/capability fingerprint
+ baseline schema revision
```

Backend/platform baselines account for qualified implementation differences.
Vendor/device/driver is recorded in provenance but does not create a new baseline
automatically. A narrower hardware-class baseline requires a reviewed descriptor
with evidence that one common backend/platform reference cannot satisfy the
declared semantic tolerance. Unknown hardware still runs against the nearest
applicable reviewed key only when matching is exact under that descriptor;
otherwise the result is `BaselineUnavailable`, never pass.

Cross-backend parity is checked separately through descriptor-owned invariants:
same admitted feature path, frame/capture identity, dimensions, alpha semantics,
required visible regions and feature-specific measurements. A cross-backend
aggregate report may compare canonical images diagnostically, but it does not
replace each backend/platform golden gate or require bit identity where the
renderer contract allows implementation variance.

Baselines store canonical lossless oracle data, a human-viewable preview and a
manifest containing all key fields, source commit, producing lane, environment,
effective capabilities, shader/compiler identity and checksums. Baseline lookup
verifies schema and hashes before use. Corrupt, duplicate, ambiguous or newer
unknown schemas fail as typed baseline errors.

### 5. Comparison thresholds are explicit, bounded and reviewed

The default required SDR policy for non-masked RGB channels is:

- dimensions and alpha bytes match exactly;
- per-channel absolute difference is at most `2/255`;
- RGB root-mean-square error is at most `0.5/255`; and
- pixels with any RGB difference greater than `1/255` are at most `0.10%`.

The default required linear-HDR policy is:

- dimensions match and alpha absolute difference is at most `1e-6`;
- each finite RGB channel satisfies absolute difference `<= 1e-3` or relative
  difference `<= 1e-3`, with relative denominator
  `max(abs(reference), 1e-3)`;
- RGB root-mean-square error is at most `2.5e-4`; and
- pixels exceeding both RGB absolute and relative limits are at most `0.10%`.

All calculations use a specified deterministic binary64 accumulator and fixed
row-major/channel order. Checked 64-bit counts precede allocation and arithmetic.
NaN/Infinity, dimension mismatch, missing channels or integer overflow are hard
failures, not outliers. Perceptual metrics such as SSIM may be included as
diagnostic evidence only until their exact implementation, parameters and gate
are versioned in the comparison policy.

A case may tighten thresholds. Loosening any default requires a case-local reason,
owner, metric values from at least two qualified runs and review in the same
change; no global “update tolerance” switch exists. Thresholds cannot vary from a
runtime environment variable or be learned from the candidate image.

Static masks are permitted only for a registered, deterministic region whose
semantic value is independently asserted (for example a timestamp overlay that
the case intentionally includes). A mask is content-addressed, reviewed and may
exclude at most `1.0%` of pixels by default; higher coverage requires an explicit
case limit and owner approval. Runtime-generated difference masks, dilation around
failed pixels and blanket edge suppression are forbidden. Reports show masked
area and still publish masked-pixel differences diagnostically.

### 6. Results and artifacts preserve actionable identity

```cpp
struct RendererReferenceImageResult {
    ReferenceRunId run;
    RendererReferenceCaseId testCase;
    RendererEnvironmentIdentity environment;
    RendererGeneration renderer;
    DeviceGeneration device;
    RealRenderFrameId frame;
    GraphExecutionId graph;
    ReferenceBaselineKey baseline;
    ReferenceImageOutcome outcome;
    ReferenceComparisonMetrics metrics;
    ReferenceArtifactManifest artifacts;
};
```

Outcomes are `Passed`, `ExpectedUnsupported`, `UnexpectedUnsupported`,
`BaselineUnavailable`, `BaselineInvalid`, `CaptureFailed`, `TimedOut`,
`DeviceLost`, `ComparisonFailed`, `BudgetExceeded`, `Cancelled` and
`InfrastructureFailed`, plus `NonDeterministic` when identical run identity
produces disagreeing results. A process exit and JUnit case derive from this
result; log text is not parsed to decide success.

Every non-pass identifies case, backend, adapter/device/environment, effective
capability revision, capture operation/state and stable actionable cause. When an
image exists, failure artifacts include reference, actual canonical image,
human-viewable previews, absolute-difference image, threshold heatmap, bounded top
pixel/channel differences and the complete comparison/result manifest. ADR-041
diagnostics and ADR-044 markers join only by exact generation/frame/graph context.

Artifact paths use generated names under the CI/test result root. Manifests use
relative allowlisted paths and checksums; no machine-absolute path or native
handle/address is published. Image artifacts are untrusted data, decoded with
dimension/byte limits and never executed. Publication uses temporary files and an
atomic manifest-last commit. Interrupted output is reported and quarantined.

### 7. Work, storage and retries are finite

Default case limits are 4096x4096, four channels, two captured frames, 256 MiB
readback/intermediate memory, 512 MiB artifacts and 60 seconds after warm-up. Hard
maxima are 8192x8192, four channels, eight frames, 1 GiB memory, 2 GiB artifacts
and five minutes. The suite validates aggregate parallel-case memory, staging and
GPU submission budgets before admission; individually valid cases cannot
oversubscribe a lane.

At most one capture per device is in flight by default and at most two at the hard
limit. CPU comparison runs on cancellable bounded workers over owned buffers.
Backpressure queues finite work or returns `BudgetExceeded`; it never grows an
unbounded image queue or blocks a renderer owner thread on encoding/upload.

The same commit/environment case may retry once only after an explicitly typed
infrastructure interruption such as runner loss. Image mismatch, timeout, device
loss, unsupported capability and crash are test outcomes and are not retried into
a pass. Repeat disagreement on identical identity is classified `NonDeterministic`
by lane orchestration and emits both result sets. It fails the current
qualification; later quarantine may make the job non-blocking under CI policy but
cannot convert either run to `Passed` or mark that backend tuple `Qualified`.

### 8. CI uses explicit native hardware lanes

Pull-request fast lanes always run Null/service/comparator/schema fixtures and
affected CPU-side tests. A native reference subset runs only on a documented,
stable GPU lane whose backend/platform/device class, driver policy, color path and
artifact capacity satisfy the case matrix. Absence of a required protected lane
is `InfrastructureFailed` or a blocked required check, not success.

Protected-branch lanes cover every supported production backend/platform tuple
with representative scenes for opaque/masked/translucent materials, geometry and
depth, lighting/shadows, texture sampling, post-processing/output transform,
resource lifetime/aliasing boundaries and supported presentation paths.
Capability-specific scenes run only where their predicate is admitted; every
required profile has at least one success and expected-unsupported case.

Scheduled qualification expands device/driver classes, cold caches and exhaustive
scenes. The matrix records `Qualified`, `Failed`, `ExpectedUnsupported`,
`NotScheduled` and `InfrastructureFailed` independently. `NotScheduled` may be
non-blocking only when the protected matrix assigns another required lane; it
never means a backend passed.

Artifacts are uploaded on failure and retained under CI policy. Passing runs keep
manifests/metrics and may omit duplicate previews. Baseline promotion is a
separate maintainer-reviewed operation showing old/new/reference/diff metrics and
provenance. Tests and bots cannot overwrite repository baselines, accept the
candidate output automatically or mix baseline updates with unrelated changes.

### 9. Lifecycle and Null behavior are explicit

Cancellation stops future frames/readbacks, waits only for already submitted work
through its bounded fence token, releases owned buffers and publishes
`Cancelled`. Device loss closes admission for the old generation, records exact
coverage and releases readback resources through normal deferred destruction. A
surface/backend replacement cannot satisfy an old request with a new generation.

Shutdown rejects new cases, cancels queued work, drains or abandons bounded
submitted readbacks under renderer policy, joins comparison/publication workers,
releases artifact staging and then destroys backend/test-host services. It is
idempotent after partial initialization and performs no global GPU-idle wait.

Null provides deterministic canonical images and injected success, unsupported,
malformed, timeout, cancellation, device-loss and publication failures. It proves
service state, normalization, comparison math, manifests, budgets and lifecycle,
but cannot qualify native rasterization, shader compilation, readback mapping,
color conversion or driver behavior.

## Migration And Verification

Existing ad hoc visual checks migrate to registered descriptors and canonical
baselines. Material, advanced-rendering, animation and editor screenshot suites
may share comparator/artifact infrastructure, but editor UI baselines remain a
separate domain and cannot serve as renderer scene references.

Tests must cover descriptor/schema/hash validation; exact baseline-key matching;
canonical swizzle/pitch/origin/resolve and SDR/HDR conversion; dimensions,
non-finite values and checked overflow; exact threshold boundaries, outlier counts,
RMSE/relative math and masks; success/expected and unexpected unsupported paths;
capture failure/timeout/device loss/cancel/shutdown; aggregate admission and
backpressure; interrupted atomic artifacts; diagnostic/marker correlation;
baseline promotion authorization; deterministic Null fixtures; and native
backend/platform scenes on documented hardware lanes.

## Consequences

Renderer changes gain reproducible pixel evidence with explicit backend,
capability and artifact provenance. Legitimate implementation variation is kept
separate from semantic parity, and unsupported/infrastructure states cannot look
like success. The cost is curated deterministic scenes, reviewed per-platform
baselines, stable hardware lanes, storage and ongoing triage/promotion ownership.

## Rejected Alternatives

| Option | Decision and reason |
|---|---|
| One golden image for all backends/platforms | Rejected: legitimate compiler/rasterization/output differences create noise and encourage broad tolerances. |
| One baseline per arbitrary driver/machine | Rejected: baseline explosion can bless regressions and makes coverage irreproducible. |
| Compare native swapchain screenshots | Rejected: compositor scaling/color management and window state are outside the canonical renderer oracle. |
| Use JPEG or another lossy oracle | Rejected: codec error contaminates objective renderer differences. |
| Automatically accept current output after failure | Rejected: converts regressions into baselines without review. |
| Retry image mismatches until one passes | Rejected: hides nondeterminism and produces non-reproducible gates. |
| Treat unsupported/no-GPU as pass | Rejected: absence of native evidence is not qualification. |
| Mask all edges or generate masks from the diff | Rejected: systematically hides rasterization and geometry regressions. |
| Run unbounded full-resolution captures in parallel | Rejected: can exhaust GPU/CPU memory, artifact storage and CI time. |
| Use Null results as native image qualification | Rejected: synthetic images prove orchestration/comparison, not drivers or native rendering. |
