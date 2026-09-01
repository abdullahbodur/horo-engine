# ADR-030: Metal Platform and Feature Baseline

- **Status**: Proposed
- **Date**: 2026-08-31
- **Supersedes**: None
- **Scope**: Metal native admission, GPU family, MSL and macOS deployment
- **Jira**: [HORO-314](https://horo-engine.atlassian.net/browse/HORO-314)
- **Issue**: [#314](https://github.com/abdullahbodur/horo-engine/issues/314) ([RND-005.1])
- **Normative document**: [Rendering Architecture](../architecture/runtime/rendering-architecture.md)

## Context

Horo's Metal backend owns its device and command queue and exposes private
presentation/editor seams. However, successful device creation alone does not
establish OS, GPU, shader-language, or feature support. The current viewport
compiles source with default Metal compile options, and the backend's capability
report is transitional. Downstream work needs one explicit admission policy.

The [renderer manifest](../architecture/runtime/renderer-module-package-manifest.md)
already illustrates a macOS 14.0 minimum and native architecture variants.
The [developer baseline](../architecture/delivery/developer-environment.md) also
starts at macOS 14. This decision preserves that floor and defines the Metal
contract rather than treating those examples as hardware qualification.

[ADR-028](028-renderer-capability-limits-and-product-profiles.md) owns reported,
implemented, effective, and product-profile support. Renderer resource identity
is [ADR-027](027-renderer-resource-identity-and-descriptors.md); that is a
different decision from
[ADR-008](008-error-model-exception-boundary-and-registry.md). GPU backing
follows [ADR-034](034-gpu-memory-and-residency-ownership.md). Metal is an equal
backend under the
[parity contract](../architecture/runtime/render-backend-parity-contract.md),
not a higher feature tier or an automatic replacement for OpenGL.

## Decision

### 1. macOS desktop scope and GPU admission

The initial `metal` product component targets **macOS 14.0 or later**, with
native `arm64` and `x86_64` variants eligible for release qualification.
Eligibility is not a promise that both variants are currently distributed.
Only variants with published package and GPU evidence may be advertised as
supported; a universal binary must qualify each advertised architecture slice.

| Host/device class | Native admission requirement | Product boundary |
|---|---|---|
| Apple silicon macOS, native arm64 | Selected device reports support for `MTLGPUFamilyApple7` | M1-class baseline and successors satisfying that query; still require effective feature and release qualification. |
| Intel macOS, native x86_64 | Selected device reports support for `MTLGPUFamilyMac2` | Qualify actual Intel/AMD GPU and OS combinations; CPU architecture alone does not identify GPU support. |
| macOS below 14.0 | Rejected | No automatic lower deployment-target or language retry. |
| iOS, iPadOS, tvOS, visionOS, Mac Catalyst | Outside this component's initial product scope | Shared Apple API names do not establish host, surface, packaging, or input support; require a separate scope decision. |
| Windows/Linux, translated processes, simulators, virtual/remote GPUs | Not qualified by this baseline | May have explicit engineering fixtures, but their results do not certify a native desktop release. |

The backend queries the selected device using `supportsFamily`; it never orders
raw family enum values across Apple and Mac families or parses marketing/model
names to grant support. A newer GPU that satisfies the same query can be
admitted, but a newly released OS/GPU is not automatically release-qualified.
Apple documents family queries, overlapping membership, and feature-specific
checks in [Detecting GPU Features and Metal Software Versions](https://developer.apple.com/documentation/metal/detecting-gpu-features-and-metal-software-versions).
The [Apple feature tables](https://developer.apple.com/metal/capabilities/)
identify M1-series devices with Apple7; Horo's admission floor is a product
decision, not a claim that older devices cannot run any Metal application.

Do not infer unified memory, a storage mode, texture format support, argument
buffer tier, or ray tracing from the host CPU. Query the actual device. In
particular, the Intel lane must not inherit Apple silicon memory assumptions.

### 2. API model and shader language are distinct baselines

The baseline uses the conventional **`MTLCommandQueue` / `MTLCommandBuffer` /
render, blit, and compute encoder API model**, restricted to APIs available on
macOS 14.0. It does not require Metal 4 command allocators/queues or every feature
marketed as Metal 3. The OS/API floor, family predicate, and required operations
below jointly define admission; a single “Metal version” string does not.

The default cooked shader target is **Metal Shading Language 2.4**, selected
explicitly through the toolchain's language option or
`MTLCompileOptions.languageVersion = MTLLanguageVersion2_4` for permitted source
compilation. Apple exposes [MSL 2.4](https://developer.apple.com/documentation/metal/mtllanguageversion/version2_4)
from macOS 12; the Horo runtime floor remains 14.0. Using a newer SDK must not
silently raise the language or deployment target.

RND-005.6 enforces this through one validated shader-target descriptor shared by
offline cooking and editor compilation. Both compiler adapters must consume its
explicit MSL/deployment options, reject absent or conflicting baseline options,
and include them in artifact/cache identity. Adapter tests inspect the emitted
compiler arguments or native compile options; native qualification compiles and
loads a baseline fixture on the minimum runtime. These checks prevent toolchain
defaults from silently becoming the policy.

Newer language/API paths are separately declared optional variants. They require
OS availability checks, native support, backend implementation, and ADR-028
effective admission before use. Optional variants need an implemented and
cooked baseline fallback. A required feature without an admitted variant
returns a typed error; the compiler must not retry with a different language
version or rewrite material semantics merely to make compilation succeed.

Cooked library compatibility records must include target OS/architecture,
minimum OS, MSL version, compiler/toolchain identity, compile options, shader
content identity, and required features. Binary library loading and pipeline
creation are validated on the minimum supported runtime for each shipped
variant. A successful compiler invocation on a newer development Mac is not
compatibility evidence. RND-005.6 and the shader/cook owner realize this metadata
within the canonical shader artifact model, not a Metal-only asset registry.

Packaged games use cooked libraries; Xcode and the offline shader compiler are
build/cook tools, not runtime installation prerequisites. Editor/development
source compilation remains allowed as an explicit authoring path with pinned
options and actionable diagnostics. It runs outside normal frame-hot work and
uses the [ADR-010](010-job-waiting-and-operation-store-ownership.md) job/operation
contract when asynchronous. It is never an implicit packaged-library fallback.

### 3. Required parity and optional feature profile

The minimum is the shared interactive parity contract: device/queue lifecycle,
window presentation, FIFO, drawable resize/suspension, primary color rendering,
offscreen color/depth viewport targets, indexed mesh rendering, GUI composition,
typed failures, and safe teardown. Immediate presentation either has verified
support or returns a typed unsupported result, as required by that contract.

The baseline resource path supports ordinary explicitly bound buffers,
textures, and samplers. It must not depend on bindless indexing or argument
buffers tier 2. Required format/usage/sample-count combinations are admitted
through ADR-028's typed predicates, not by treating every native pixel format
as universally usable.

| Operation or feature | Baseline obligation |
|---|---|
| Raster, viewport, and GUI parity | Required before advertising the interactive component as complete. |
| Compute dispatch, timestamp queries, indirect execution | Optional until each backend path is implemented and effectively admitted; native availability alone is insufficient. |
| Argument buffers tier 2, sparse resources, ray tracing, mesh shading, MetalFX | Optional individually; no product-profile or GPU-family shortcut. |
| HDR/EDR presentation | Separate display/surface admission under RND-005.5 and presentation policy; GPU support alone is insufficient. |
| Native API/shader validation and GPU capture | Diagnostic configuration, not a gameplay feature or universal runtime prerequisite. |

`Baseline`, `Standard`, `High`, and `Ultra` remain ADR-028 rendering recipes.
Selecting `metal` grants none of them automatically. An unimplemented native
operation remains unavailable; do not mark compute true simply because the
chosen API model includes a compute encoder.

### 4. Initialization, identity, and selection

The component/host checks OS and architecture before loading native code and
uses the existing verified probe lifecycle. Metadata discovery and provider
registration stay inert. Final initialization revalidates the selected device;
a successful cached probe is not an active device or capability snapshot.

Without an explicit adapter selection, the initial policy validates the system
default device. It does not search for a more capable adapter behind the user's
back. An explicit adapter request, when implemented by RND-005.2, must match the
requested machine-local identity or fail; adapter identities are not portable
project settings. Match-or-fail is identity matching, not a Metal-specific slot
budget; registry slot/generation exhaustion remains ADR-027. This does not add
a new public selection API in M0.

This default deliberately follows the OS's device choice rather than forcing a
discrete GPU and its power/display trade-offs on dual-GPU Macs. Functional parity
does not promise equal frame rates across devices. Qualification records which
GPU was selected and measures that configuration; diagnostics expose the
selected adapter and admission failure so an explicit adapter choice can be
offered. A failing default device is not silently replaced by another candidate.
A future high-performance or low-power preference must be an explicit host
selection policy, tested by RND-005.2, rather than a model-name heuristic hidden
inside Metal initialization. An OS-initiated GPU change (automatic graphics
switching, external-display routing, or power-management reassignment) is
device loss/replacement under ADR-027: invalidate the old generation and
rebuild; do not keep old resources, queues, or snapshots on the new GPU.

After acquiring the matching presentation attachment, initialization requires:

1. a non-null device satisfying the OS/architecture/family admission above;
2. reported device facts, implemented support, and driver-adjusted effective
   support satisfying the required backend contract;
3. a usable queue and matching layer/device binding, with validated required
   formats and presentation settings;
4. publication of readiness and private editor access only after those checks.

Failure releases only acquired resources and clears the private bridge before
releasing device/queue ownership. The host owns its window; the presentation
adapter owns the view/layer attachment; the runtime owns device, queue,
drawables, command buffers, and encoders. Editor adapters borrow access and must
not create a second device. Native types remain private. Surface work follows
platform affinity on the host's declared render-capable owner thread; metadata
probing must not leave a retained surface or change active editor state.

Device loss, removal, or replacement invalidates owner/generation and capability
snapshot identities under [ADR-027](027-renderer-resource-identity-and-descriptors.md)
and ADR-028. Recovery rebuilds device-owned state explicitly; it never reuses an
old plan/resource on a different GPU. No normal frame waits for GPU idle or
shader compilation. Completion and retirement use the existing renderer
lifecycle; bounded tests/teardown retain the documented wait exceptions.

An explicit native-validation request is strict: configure the host's diagnostic
environment before device creation, verify activation as supported by the
platform, or return an actionable validation-unavailable result. Do not silently
disable requested validation or mutate process environment during initialization.
Production startup without that request does not require developer diagnostics.
There is no OpenGL-style retry-once without the validation request: Metal
validation is a process/environment contract that must exist before
`MTLCreateSystemDefaultDevice`. Retrying without it would silently drop
requested diagnostics. OpenGL retries the debug *context attribute* because the
same 4.1 Core profile can still succeed; that is a different native seam, not
an inconsistency to copy.

Backend selection remains CLI, configuration, then host default. The
[ADR-029](029-opengl-core-profile-and-platform-policy.md) macOS OpenGL
warning can offer Metal installation/probing and explicit migration; it does
not select Metal, persist settings, or qualify this component. Missing or
unsupported Metal follows normal repair/error handling and authorized fallback
policy, never silent OpenGL or interactive `null` substitution.

### 5. Deployment and release qualification

Each signed variant declares macOS 14.0 as its minimum OS and its actual native
architecture. Matching host executables, modules, dependencies, and cooked
libraries must have compatible deployment targets. Set the deployment target
explicitly; do not inherit the CI machine's OS or SDK default. Newer SDK APIs
remain guarded unless the baseline is revised. Package validation rejects
mislabeled architecture slices and higher binary minimum-OS requirements.

`apple.metal` denotes the system framework. Its mere presence does not prove a
GPU family or an effective feature profile. Do not encode a marketing-level
`minimumVersion: "3"` as a substitute for minimum OS and device admission. Metal
frameworks/drivers are supplied by the OS, not downloaded as renderer plugins.
The existing package-signature, library-validation, ABI, and controlled-load
contracts remain authoritative.

RND-005.8 publishes qualification evidence per shipped architecture, OS version,
GPU/family, driver/OS build, component build, toolchain, language target, and
validation mode. Both the minimum runtime and representative newer runtimes
need parity and GPU smoke evidence. Intel/discrete and Apple silicon lanes
cannot qualify each other. Headless/fake-runtime tests and ordinary compilation
do not establish native GPU support. Unqualified variants stay engineering-only
until evidence exists; support withdrawal or a raised baseline requires an
architecture update and release/migration notice.

### 6. M0 migration ownership and verification

This PR defines policy only. It does not change build defaults, runtime code,
shader compilation, or the set of currently shipped artifacts.

| Owner | Required downstream work |
|---|---|
| RND-005.2 / #315 — Adapter, Device and Capability | Actual OS/device/family checks, typed failures, effective capabilities/limits/formats, adapter identity, and loss/recreation invalidation. |
| RND-005.3 / #316 — Resource, Heap and Binding | Portable binding baseline, queried memory properties, format admission, and safe retirement without unified-memory assumptions, under [ADR-034](034-gpu-memory-and-residency-ownership.md). |
| RND-005.4 / #317 — Commands, Synchronization and Graph | Implement admitted operation paths and non-blocking completion without requiring the Metal 4 model. |
| RND-005.5 / #318 — Presentation, HDR and Drawable | Validate layer/device settings, surface lifetime and affinity, display capability, and recovery under [ADR-033](033-presentation-and-display-ownership.md). |
| RND-005.6 / #319 — Shader, Pipeline and Material | Pin MSL/deployment options, cooked library metadata and admission, baseline/optional variants, and minimum-runtime pipeline validation. |
| RND-005.7 / #321 — Editor GUI and Viewport | Consume runtime readiness and borrowed device access; remove implicit source-compiler defaults and integrate explicit diagnostics. |
| RND-005.8 / #320 — Apple Hardware and GPU Qualification | Coordinate explicit deployment targets with build/package owners; verify binary metadata and publish native OS/architecture/GPU evidence. |

Current gaps are concrete: `MetalRuntime.mm` creates a system-default device
without family qualification; `MetalRenderBackend.cpp` still has a static
capability report; `EditorViewportRendererMetal.mm` passes `options:nil` to
source compilation. The release workflow labels an arm64 artifact without
establishing the full matrix above. These facts are migration inputs, not
production-completeness claims or a reason to lower the policy.

Downstream deterministic tests must reject an old OS, wrong architecture,
unsupported family, missing requested adapter, null device, failed queue,
required-format failure, and stale snapshot. They must also prove native-only
features stay unavailable before implementation, library/language mismatch is
actionable, requested validation is not silently dropped, and every partial
initialization clears borrowed access and releases its ownership lease.

Native lanes must verify shared parity, GPU image thresholds, shader compilation
with explicit MSL 2.4, cooked-library loading on the minimum runtime, source-less
packaged startup without Xcode, and device/surface lifecycle where supported.
M0 verification covers document consistency and links; it does not count as
implementation tests or GPU coverage.

## Consequences

The OS floor matches existing package/developer guidance while making the native
GPU and language requirements explicit. Retaining qualified Intel Mac2 and
Apple silicon Apple7 paths costs separate testing and forbids unified-memory
assumptions, but avoids an unannounced Apple-silicon-only product change.

MSL 2.4 and conventional encoders provide a bounded common implementation
target. Advanced paths can evolve without raising the minimum for every user,
at the cost of variant/cook validation and explicit fallback assets. A modern
GPU or SDK alone is never a promise that Horo implements every native feature.

## Rejected Alternatives

- **Require Metal 4 or the latest MSL by default:** unnecessarily excludes the
  retained baseline and makes toolchain upgrades change product requirements.
- **Support every Apple OS because the framework name is shared:** hides host,
  lifecycle, packaging, and qualification work outside this ticket.
- **Apple silicon only without migration:** drops the existing x86_64 variant
  model without a product support decision; qualify it separately instead.
- **Accept any non-null Metal device:** omits required family, language, format,
  implementation, and driver-policy checks.
- **Infer feature rank from Metal/GPU branding:** bypasses ADR-028's typed
  effective support and can choose unimplemented paths.
- **Use compiler defaults or runtime source fallback in packaged games:** makes
  artifacts depend on the host toolchain/runtime and conceals cook failures.
