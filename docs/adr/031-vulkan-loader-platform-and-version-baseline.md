# ADR-031: Vulkan Loader, Platform and Version Baseline

- **Status**: proposed
- **Date**: 2026-08-31
- **Supersedes**: None
- **Scope**: Vulkan loader, 1.3 admission, WSI and Windows/Linux baseline
- **Issue**: [#322](https://github.com/abdullahbodur/horo-engine/issues/322) ([RND-006.1])
- **Jira**: [HORO-322](https://horo-engine.atlassian.net/browse/HORO-322)
- **Normative document**: [Rendering Architecture](../architecture/runtime/rendering-architecture.md)

## Context

The renderer architecture reserves the `vulkan` identity, but the active runtime
has no Vulkan backend target or platform adapter yet. Loader installation, API
version, device features, presentation support, and engine implementation must
not become one ambiguous “Vulkan available” flag during realization.

This ADR defines native admission and deployment policy. It consumes
[ADR-028](028-renderer-capability-limits-and-product-profiles.md) for effective
capabilities and product profiles, and the common
[parity](../architecture/runtime/render-backend-parity-contract.md) and
[distribution](../architecture/runtime/renderer-distribution-and-availability.md)
contracts for lifecycle and selection. Renderer resource identity is
[ADR-027](027-renderer-resource-identity-and-descriptors.md).
GPU backing follows [ADR-034](034-gpu-memory-and-residency-ownership.md).
Presentation follows [ADR-033](033-presentation-and-display-ownership.md).
It does not choose a new shader source language, allocator, render-graph
scheduler, or public extension ABI.

## Decision

### 1. Vulkan 1.3 core with explicit enabled features

The initial native backend requires **Vulkan 1.3** at both instance and selected
physical-device level. Query loader instance-version support before instance
creation and request `VK_API_VERSION_1_3`; a missing version-query entry point or
a reported version below 1.3 fails admission. Check the selected device's API
version independently. Newer compatible implementations may satisfy the floor,
but do not raise the requested API version merely because newer headers or a
newer loader are installed. Only the standard Vulkan API variant is admitted.

The required device feature bits are `dynamicRendering`, `synchronization2`,
and `timelineSemaphore`. Query them through the appropriate features structures
and explicitly enable them at device creation. The initial command path uses
the core 1.3 dynamic-rendering and synchronization2 commands. There is no hidden
1.2-plus-extensions or legacy-render-pass retry. This bounds the initial native
implementation matrix and must be reflected in RND-006.2/.4 realization.

Instance and device versions differ, and promotion to core does not replace
feature enablement. These distinctions follow the Khronos
[version guidance](https://docs.vulkan.org/guide/latest/versions.html) and
[feature enablement contract](https://docs.vulkan.org/guide/latest/enabling_features.html).
Device creation enables only required features and admitted optional features,
not every feature reported by the driver.

Timeline support is an internal synchronization prerequisite, not permission to
wait on the normal frame thread. Presentation still uses the required WSI binary
semaphores and their separate lifetime rules; a graphics submission's completion
does not by itself prove a presentation wait semaphore is reusable. RND-006.4/.5
own that synchronization under the
[Khronos swapchain semaphore guidance](https://docs.vulkan.org/guide/latest/swapchain_semaphore_reuse.html).

Shader artifacts target the Vulkan 1.3 environment with SPIR-V no newer than
1.6. Khronos [version guidance](https://docs.vulkan.org/guide/latest/versions.html)
requires a Vulkan 1.3 implementation to support SPIR-V 1.6 and below; the 1.6
ceiling is that required mapping, not a silent raise to Vulkan 1.4. Individual
SPIR-V capabilities/extensions must also match enabled device features and the
cooked variant requirements. The SPIR-V version ceiling alone does not admit
arbitrary shader operations. RND-006.6 and the canonical shader toolchain
record/validate the target environment and features; they do not add a
Vulkan-specific asset registry or silently recompile failed packaged assets.

### 2. Required extensions and surface-specific admission

| Scope | Required contract |
|---|---|
| Interactive instance | `VK_KHR_surface` and the selected host adapter's WSI instance extensions. |
| Windows Win32 WSI | `VK_KHR_win32_surface`. |
| Linux X11 WSI | The adapter's selected `VK_KHR_xlib_surface` or `VK_KHR_xcb_surface` path; do not require both when only one is used. |
| Linux Wayland WSI | `VK_KHR_wayland_surface`. |
| Interactive device | `VK_KHR_swapchain`, the required core feature bits above, and compatible graphics/presentation queues for the actual surface. |
| Explicit diagnostics | `VK_EXT_debug_utils` and `VK_LAYER_KHRONOS_validation` when the corresponding native diagnostic mode is requested; absence is a typed diagnostic-unavailable failure, not silent disablement. |

Enumerate and validate required instance/device extensions in their respective
scopes before creating the corresponding object. Optional extension groups
must declare all dependencies and associated feature bits. Promoted core paths
do not require the former extension name to remain advertised. Ray tracing,
descriptor indexing/bindless, mesh shaders, HDR, timestamp queries, and engine
compute execution remain individually optional under ADR-028; API version and
the backend ID do not grant them.

The selected adapter supplies native WSI requirements through a private Horo
seam after host video initialization and before Vulkan instance creation. For
SDL, consume [SDL's required extension list](https://wiki.libsdl.org/SDL3/SDL_Vulkan_GetInstanceExtensions)
rather than guessing from an OS name. Deduplicate the list, validate it against
the supported platform paths above, and reject missing or unsupported WSI
requirements with a typed startup diagnostic. Native window handles and Vulkan
extension strings do not enter project settings or public Render API types.

Select a graphics-capable queue and validate presentation support against the
actual surface. A common graphics+present queue family is preferred. When the
host has no explicit adapter request, eligible-device ordering prefers a device
that can present from its graphics family over an otherwise eligible device that
requires a split present family. A split present family is still admitted when
it is the only eligible option or when an explicit adapter request names that
device; RND-006.4/.5 then own sharing and synchronization. Do not assume
graphics capability implies present capability or require a dedicated
compute/transfer queue. Validate surface formats, usages, extents,
image counts, and presentation modes before swapchain creation. FIFO is the
required parity mode; unsupported explicit alternatives return typed results.
Output color/HDR policy remains with the display/presentation owners.

### 3. Platform and portability scope

The initial product scope is native **Windows 11 x86_64** and **Linux x86_64**
desktop. Ubuntu 24.04 LTS is the Linux reference qualification distribution,
matching [Developer Environment](../architecture/delivery/developer-environment.md).
Other distributions require their own package/runtime compatibility evidence;
an OS-family label is not a universal ABI or driver guarantee. X11 and Wayland
are separate qualification lanes. Those Linux rows live in the same
[Renderer Distribution And Availability](../architecture/runtime/renderer-distribution-and-availability.md)
matrix as [ADR-029](029-opengl-core-profile-and-platform-policy.md) OpenGL Linux
lanes; Vulkan and OpenGL add backend columns, not two independent matrices.
Additional architectures and platforms need an explicit scope/migration decision
before being advertised.

macOS/iOS MoltenVK and other portability-subset implementations are **deferred**
from this initial component. The baseline does not enable
`VK_KHR_portability_enumeration` or its enumeration flag, and rejects a selected
device advertising `VK_KHR_portability_subset`. Portability-subset rejection
outranks explicit adapter match-or-fail: an explicit request for a
portability-subset device still fails as an unsupported API variant, not as a
successful identity match. An engineering host may test a separate experimental
configuration but cannot label it native qualification.

Future portability support must explicitly enable enumeration, enable the
device subset extension when advertised, inspect subset features/limits, and
qualify each required engine operation. Khronos documents those obligations in
[portability enumeration](https://docs.vulkan.org/refpages/latest/refpages/source/VK_KHR_portability_enumeration.html)
and [portability subset](https://docs.vulkan.org/refpages/latest/refpages/source/VK_KHR_portability_subset.html).
It also needs a verified package/runtime policy; the manifest's `horo.moltenvk`
identifier is a vocabulary example, not current product support. Metal remains
the explicit Apple backend governed by
[ADR-030](030-metal-platform-and-feature-baseline.md); no Vulkan failure silently
switches the project to Metal or OpenGL.

### 4. One loader lease and private dispatch ownership

Use the platform-installed Vulkan loader and ICD discovery for native product
variants. The component does not bundle a second loader or GPU driver, search a
project directory for native libraries, or require a Vulkan SDK at runtime.
Pinned Vulkan headers and optional validation/shader tools are build/toolchain
dependencies private to their owning targets. Package metadata declares
`khronos.vulkan-loader` as a system-loader requirement; actual version/device
admission still occurs at probe and initialization.

The host platform adapter owns a loader lease. The backend receives its
`vkGetInstanceProcAddr` through a private interface, builds instance/device
dispatch tables with the correct dispatch owner, and rejects missing required
entry points. No backend-wide mutable global device dispatch table, implicit
static loader initialization, or cross-device function-pointer reuse is allowed.
The loader lease outlives instances, devices, surfaces, and callbacks created
through it; release it last. The backend does not load a second library behind
the adapter's back. See the
[Khronos loader architecture](https://github.com/KhronosGroup/Vulkan-Loader/blob/main/docs/LoaderInterfaceArchitecture.md).

For SDL, load the library explicitly after video initialization and before the
first Vulkan window, and obtain the entry point from the same SDL loader.
Serialize load/unload operations and pair each successful load with its unload;
do not let the first window implicitly select a different library.
[SDL's loader contract](https://wiki.libsdl.org/SDL3/SDL_Vulkan_LoadLibrary)
defines its reference counting and lack of thread safety. Platform library
resolution uses the controlled system-loader path, not a user-supplied project
or Toolchains path. Any alternative development loader is an explicit,
machine-local engineering configuration, outside product qualification.

System loader discovery can involve native ICDs and layers, including externally
configured layers. This is not a sandbox or a claim that all driver code is
verified by Horo's package signature. Probes use the existing isolation policy;
qualification records loader/ICD/layer identities and relevant overrides.
Unexpected or modified configurations require re-probe and cannot inherit a
different environment's qualification result. Horo does not change process-wide
loader environment variables during a live renderer session.

### 5. Admission, failures, and driver policy

Composition retains CLI/configuration/host-default precedence and verified
component selection before graphics-window creation. Metadata reading and
provider registration remain inert; an isolated availability probe may acquire
temporary native objects but releases them before returning its provisional
result. The final surface/device admission must be repeated at initialization.

Enumerate candidate devices, record typed rejection reasons, and apply the host's
explicit adapter preference only among eligible devices. An explicit requested
adapter must match or fail. With no preference, use a documented stable ordering
of eligible machine-local device identities; do not depend on loader enumeration
order or infer support from vendor/model strings. Adapter identity is not
portable project data. RND-006.2 realizes selection without changing the shared
backend-selection policy or creating implicit multi-GPU execution.

Driver qualification is per OS/window system, GPU/device/driver identity, loader,
component build, and effective capabilities. ADR-028's versioned driver rules
may restrict support, never invent it. If a restriction removes a required
feature, the backend becomes unavailable; do not downgrade to a different
native contract. Software ICD and virtual GPU test results are labeled fixtures,
not hardware/performance qualification.

Publish readiness only after required dispatch, feature enablement, queue,
surface, and effective-support checks succeed. Failures identify the requested
backend, failed stage, required versus observed contract, and repair guidance.
Missing loader, no compatible ICD/device, missing extension/feature, validation
unavailability, and surface incompatibility remain distinguishable typed results.
No normal frame blocks for GPU idle; bounded recovery/teardown/test exceptions
remain those of the renderer architecture and
[ADR-010](010-job-waiting-and-operation-store-ownership.md).

Partial failure unwinds acquired native objects in reverse ownership order and
clears borrowed editor access. Device loss invalidates resource owner/generation
and effective snapshots under
[ADR-027](027-renderer-resource-identity-and-descriptors.md) and ADR-028. Recovery
rebuilds state explicitly, including dispatch ownership, without reusing old
resources/plans. Runtime/window ownership stays with its established host,
backend, and platform-adapter layers.

### 6. M0 realization and verification

This decision adds no runtime backend, dependency, or build option. The current
`vulkan` catalog ID, future target diagram, and SDK path examples are planning
contracts, not evidence of implemented support. Downstream owners are:

| Ticket | Required realization |
|---|---|
| RND-006.2 / #323 | Loader/dispatch seams with Platform, independent version queries, candidate admission, explicit feature enablement, queue/device ownership and diagnostics. |
| RND-006.3 / #324 | Required resource/format/descriptor paths and retirement using effective support under [ADR-034](034-gpu-memory-and-residency-ownership.md), without a mandatory bindless implementation. |
| RND-006.4 / #326 | Core dynamic-rendering/synchronization2 path, timeline completion, queue ownership, and non-blocking frame execution under [ADR-010](010-job-waiting-and-operation-store-ownership.md). |
| RND-006.5 / #325 | Private WSI requirements, actual-surface checks, swapchain binary-semaphore lifetime, resize and recovery under [ADR-033](033-presentation-and-display-ownership.md). |
| RND-006.6 / #327 | Vulkan 1.3 SPIR-V target validation, required shader capabilities, cooked variants and pipeline diagnostics. |
| RND-006.7 / #328 | Matching GUI/viewport integration that borrows the admitted backend, never creates an independent loader/device. |
| RND-006.8 / #329 | Windows/Linux X11/Wayland qualification, validation layers, loss/rollback tests, and package/runtime evidence with release owners. |

Deterministic tests must cover independent loader/device version failures,
missing entry points, wrong API variant, missing instance/device extensions,
queried-but-not-enabled required features, portability rejection, adapter
selection independent of enumeration order, separate graphics/present families,
and required driver-rule restrictions. Also verify dispatch/loader lifetime,
partial rollback, stale generations, and unchanged project selection on failure.

Native lanes must cover each shipped WSI path, shared parity/GPU smoke, actual
surface-format support, clean validation results for the synchronization path,
resize/suspension, device loss where inducible, and packaged launch with a
system loader/driver but without the SDK. Validate shader target/features and
archive metadata during cook. Pure fake-runtime and software-ICD lanes do not
replace those tests. M0 verifies documentation consistency; it does not report
future runtime/GPU tests as passed.

## Consequences

A 1.3 core path avoids maintaining parallel extension/legacy implementations of
the initial rendering and synchronization model. It excludes older drivers
even when some extensions could emulate that path; this is an explicit product
trade-off. Windows/Linux WSI and package compatibility still require distinct
evidence, rather than one cross-platform availability claim.

Using a single system loader keeps driver ownership with the platform and
avoids SDK installation as a gameplay prerequisite. Driver/layer configuration
remains a native trust boundary and must be reflected in diagnostics. Deferring
portability avoids silently weakening required semantics while preserving a
clear future admission/packaging route.

## Rejected Alternatives

- **Vulkan 1.2 plus assorted extensions:** expands startup and command-path
  combinations before the first backend exists; rejected for this baseline.
- **Require the latest Vulkan release:** raises driver requirements without an
  M0 feature need and makes SDK updates change product policy.
- **Accept loader presence or version alone:** cannot establish device, feature,
  enabled-operation, or actual-surface support.
- **Bundle an SDK/driver or load arbitrary project libraries:** confuses tools,
  system runtime ownership, and trusted component activation.
- **Enable every extension or infer features from vendor names:** bypasses typed
  effective support and unnecessarily expands the backend's promised contract.
- **Silently use MoltenVK on Apple hosts:** conceals portability restrictions,
  packaging, and qualification work; Metal selection remains explicit.
