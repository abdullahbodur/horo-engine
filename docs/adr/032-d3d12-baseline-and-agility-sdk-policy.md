# ADR-032: D3D12 Baseline and Agility SDK Policy

- **Status**: Proposed
- **Date**: 2026-08-31
- **Supersedes**: None
- **Scope**: D3D12 native admission, Agility activation and Windows baseline
- **Jira**: [HORO-330](https://horo-engine.atlassian.net/browse/HORO-330)
- **Issue**: [#330](https://github.com/abdullahbodur/horo-engine/issues/330) ([RND-007.1])
- **Normative document**: [Rendering Architecture](../architecture/runtime/rendering-architecture.md)

## Context

`d3d12` is a planned Windows renderer identity; no active D3D12 backend or
Agility integration exists in the runtime build. Before implementation, the
project needs one contract for OS/device admission and for the runtime that an
independently installable renderer may use inside a host process.

OS support, hardware feature level, Shader Model, Agility package version, and
engine product profile describe different things. This ADR fixes the native
baseline while preserving [ADR-028](028-renderer-capability-limits-and-product-profiles.md)
effective support and the common [parity](../architecture/runtime/render-backend-parity-contract.md)
and [distribution](../architecture/runtime/renderer-distribution-and-availability.md)
contracts. Renderer resource identity is
[ADR-027](027-renderer-resource-identity-and-descriptors.md); that is a different
decision from [ADR-008](008-error-model-exception-boundary-and-registry.md).
It does not choose the engine-wide shader source language, GPU memory
allocator, or render-graph implementation. GPU backing accounting is
[ADR-034](034-gpu-memory-and-residency-ownership.md). Presentation/display
ownership is [ADR-033](033-presentation-and-display-ownership.md).

## Decision

### 1. Windows and hardware baseline

The initial component targets **Windows 11 desktop, native x86_64**, with
**D3D feature level 12_0** and **Shader Model 6.0** as independently checked
minimums. Windows 10, Windows on Arm, translated processes, Xbox/GDK, and other
hosts are outside this initial product scope. The OS floor aligns with
[Developer Environment](../architecture/delivery/developer-environment.md);
it is a Horo support decision, not the minimum OS on which Microsoft exposes
D3D12 or Agility.

Create the device with `D3D_FEATURE_LEVEL_12_0` as the required minimum and query
`D3D12_FEATURE_SHADER_MODEL` for at least 6.0. Device/API creation success does
not establish every feature. Query required formats, usage combinations,
binding limits, and optional feature structures before effective admission.
Microsoft distinguishes these contracts in
[hardware feature levels](https://learn.microsoft.com/en-us/windows/win32/direct3d12/hardware-feature-levels)
and [D3D12 feature queries](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ne-d3d12-d3d12_feature).
An unsupported query for a required contract is a typed rejection, not assumed
support or a retry with feature level 11_0 / Shader Model 5.1.

Hardware adapters are qualified per OS build, adapter/driver identity, component
build, and actual runtime. An explicit adapter request must match or fail.
Without one, use the host's documented DXGI default preference over eligible
hardware adapters, with a stable machine-local identity tie-breaker; do not
infer support from a vendor/model string. A performance preference is explicit
host policy, not a silent backend feature upgrade. Adapter identities remain
machine-local and are not portable project settings.

WARP/software and virtual/remote adapters are explicit test configurations,
not automatic interactive fallback or proof of hardware/performance support.
New OS releases and drivers are eligible for qualification, not automatically
certified. `null` remains the normal headless test/tool backend.

### 2. Shader, binding, and command baseline

The baseline shader artifact is **validated DXIL targeting Shader Model 6.0**
for its stage. RND-007.6 and the canonical shader toolchain pin compiler and
validator identities, stage/profile options, source/content identity, and
required native features in cooked metadata/cache keys. They verify the emitted
target and reject missing/conflicting baseline options rather than accepting
the compiler's newest default. DXIL compatibility is not inferred solely from
the installed Agility package.

Root signature **1.0** and bounded descriptor tables are the baseline binding
path. Root signature 1.1, direct heap indexing/bindless, and newer shader profiles
are optional variants requiring both native support and implementation. This
keeps baseline assets usable without an implicit advanced binding model.
Required shader operations outside the admitted path fail clearly; optional
variants need an implemented, cooked baseline fallback under ADR-028.

The initial command path supports legacy D3D12 resource barriers and fence-based
completion. Enhanced barriers are optional and admitted through their feature
query and implemented graph adapter in RND-007.4. Installing a newer runtime
must not silently change barrier semantics or mix tracking models for a resource
without an explicit supported transition. Normal frames never wait for GPU idle;
the renderer's existing bounded test/teardown/recovery exceptions remain intact
under [ADR-010](010-job-waiting-and-operation-store-ownership.md).

Required interactive parity includes device/queue lifecycle, FIFO presentation,
primary color output, offscreen color/depth viewport targets, indexed meshes,
GUI composition, resize/suspension, and safe teardown. Compute execution,
timestamp instrumentation, DXR, mesh shaders, sampler feedback, variable-rate
shading, HDR/VRR, and newer Shader Models remain individually optional engine
paths. Neither feature level 12_0 nor the name “DirectX 12 Ultimate” selects a
`Baseline`/`High`/`Ultra` product recipe or grants effective support.

Packaged games consume cooked shaders without requiring DXC or a Windows SDK
installation. Explicit editor source compilation uses the canonical shader
service, not a runtime fallback for missing packaged shaders; asynchronous work
follows [ADR-010](010-job-waiting-and-operation-store-ownership.md).

### 3. Pin one Agility contract per host release

The initial dependency selection is the stable NuGet package
**`Microsoft.Direct3D.D3D12` 1.619.5**, with **`D3D12SDKVersion = 619`**.
This is the chosen M0 pin, not an assertion that the renderer is implemented or
qualified. Microsoft's [Agility release table](https://devblogs.microsoft.com/directx/directx12agility/)
maps package **1.619.5** to SDK version **619** (retail table dated 2026-07-30).
RND-007.8 re-checks that mapping against the live table before the CMake pin is
frozen. Preview packages and floating `latest` constraints are not allowed in
product releases.

Delivery records the exact package version, source identity, content digest,
native architecture, redistribution file inventory, license notices, and
associated header identity in the dependency/release manifest. The package
version and integer SDK version are distinct fields. Patch releases sharing the
same SDK integer remain different dependencies and must not be substituted
without review and qualification. No hash is fabricated by this ADR; the build
integration obtains and verifies the pinned artifact before distribution.

The **process host** owns Agility activation. Windows executables that support
D3D12 export `D3D12SDKVersion` and a fixed application-relative `D3D12SDKPath`
pointing to the dedicated application-relative directory with a trailing
backslash: `D3D12SDKPath` uses the C++ string literal `".\\D3D12\\"`.
Generate these values from the same pinned release contract used for packaging. The
renderer DLL and extensions must not provide competing activation settings,
change the SDK path/version at runtime, or activate another device factory as
an implicit workaround. Activation policy is fixed before the first D3D12 use.

The operating system's `D3D12.dll` remains the loader; do not replace it with a
project/package DLL. The application-local payload contains the pinned,
redistributable `D3D12Core.dll` in its dedicated directory. Microsoft requires
the activation exports on the main executable and documents loader selection
in its [Agility integration guide](https://devblogs.microsoft.com/directx/gettingstarted-dx12agility/).
The loader may select a newer inbox core supplied by Windows. That is supported
OS loader behavior, not a Horo SDK downgrade or evidence that the bundled
package changed. Record the actual runtime/OS identity for diagnostics and
qualification, and continue applying effective feature checks.

The trusted package/host preflight still verifies the declared application-local
payload and activation contract. Missing, wrong-architecture, tampered, or
version-mismatched files require repair before backend activation, even if an
inbox runtime might otherwise let device creation succeed. Do not silently
remove the SDK opt-in or retry another SDK/feature level on initialization failure.

### 4. Independent component installation without process conflicts

`horo.renderer.d3d12` remains independently installable, but its activation is
compatible only with a host that declares the matching Agility release
contract. A module cannot redefine the executable's process-wide contract.
The component compatibility record includes that exact host runtime dependency;
component version or module ABI compatibility alone is insufficient.

Delivery owns the shared application-local Agility payload and fixed layout.
The component manager stages/verifies the selected dependency into that layout
before launch; it does not copy renderer files into System32 or mutate a running
process's payload. Atomic install/update publication, rollback, and reference
ownership follow the existing package service. Removing one component cannot
delete runtime files still owned by another installed compatible component/host.

A renderer requiring a different Agility pin returns an explicit host-update or
component-compatibility result. Updating the host exports, pinned payload, and
compatible renderer set is one verified restart transaction. If interrupted,
retain the old coherent installation or report repair-required; never launch
with mixed exports/runtime files. Hosts without D3D12 installed must still
reach the ordinary no-renderer recovery surface without loading the core.
Packaged-game hosts and isolated probe executables use the same release contract,
so probe success is not evidence from a different runtime selection.

Only files authorized by the pinned package's redistribution terms enter shipped
artifacts, with required notices and SBOM provenance. Release packaging validates
that inventory; it does not redistribute the entire Windows SDK, DXC toolchain,
PIX, or GPU drivers just because the renderer uses them during development.
Developer `D3D12SDKLayers.dll` must match its diagnostic runtime contract and is
excluded from ordinary game installers, following the
[Microsoft shipping guidance](https://devblogs.microsoft.com/directx/gettingstarted-dx12agility/).
Debug-layer activation is explicit before device creation; a requested but
unavailable diagnostic configuration fails rather than silently disabling it.

### 5. Effective driver policy and lifecycle

The shared host owns backend selection before window/presentation creation;
`d3d12` does not become the Windows default by this ADR. DXGI/D3D12/COM handles
remain private to backend/platform targets. DXGI factory creation, adapter
enumeration, and `D3D12CreateDevice` run on the host-declared render-capable
owner thread after that thread has initialized COM for the process. They are
not dispatched to arbitrary worker jobs. The backend owns device, queues,
command resources, descriptors, and fences; editor adapters borrow admitted
access rather than creating an independent D3D12 device.

Probe runtime/driver support without retaining live renderer state, then repeat
device and actual-output admission at initialization. Publish readiness only
after required feature/format/queue/presentation checks and effective driver
policy pass. Driver restrictions are versioned and restrictive-only under
ADR-028: disabling a required capability makes the configuration unavailable,
not eligible for a silent lower contract. Do not hardcode universal driver
version thresholds by vendor; qualify actual device/OS/runtime combinations.

Failures preserve stage and native cause while producing actionable Horo errors:
unsupported OS/architecture, incompatible component/Agility contract, missing
runtime, rejected feature level/Shader Model, format/output incompatibility,
device removal, and unavailable diagnostics remain distinguishable. No failure
silently switches to WARP, D3D11, another native backend, or interactive `null`.

Partial failure releases acquired objects and clears borrowed access before
device destruction. Device removal invalidates resource owner/generation and
capability snapshots under
[ADR-027](027-renderer-resource-identity-and-descriptors.md) and ADR-028; recovery
rebuilds device state and never reuses stale GPU identities. DRED diagnostics
and bounded recovery belong to RND-007.8, without extending normal-frame waits.
DXGI present/HDR/tearing/display policy remains with RND-007.5 under
[ADR-033](033-presentation-and-display-ownership.md). Descriptor, heap, and
budget paths in RND-007.3 obey
[ADR-034](034-gpu-memory-and-residency-ownership.md).

### 6. M0 migration and evidence

This ADR adds no DLLs, dependency downloads, native code, or build switches.
The existing `d3d12` ID and future target diagram remain planning contracts.
No current Windows artifact is certified by this decision.

| Owner | Required downstream realization |
|---|---|
| RND-007.2 / #331 | Host activation seam with Platform/Delivery, adapter/device/queue initialization, feature-level/Shader Model checks, and actionable runtime errors. |
| RND-007.3 / #332 | Bounded descriptor/resource/memory paths, format checks, and retirement that work at the baseline binding contract under [ADR-034](034-gpu-memory-and-residency-ownership.md). |
| RND-007.4 / #333 | Legacy barrier baseline, optional enhanced barriers, fence completion, and non-blocking graph execution under [ADR-010](010-job-waiting-and-operation-store-ownership.md). |
| RND-007.5 / #334 | DXGI surface/display admission, presentation lifecycle, and separately gated HDR/VRR under [ADR-033](033-presentation-and-display-ownership.md). |
| RND-007.6 / #335 | Pinned DXIL/profile/validator handling, root signature 1.0 path, optional variants, and cook/cache enforcement. |
| RND-007.7 / #336 | Matching GUI/viewport integration over runtime-owned objects and explicit diagnostics. |
| RND-007.8 / #337 | DRED/removal/rollback tests, hardware qualification and coordinated package/export/runtime verification with Delivery. |

Deterministic tests must reject unsupported OS/architecture, feature level or
Shader Model; missing/mismatched Agility payloads and exports; incompatible
host/module contracts; unavailable requested debug layers; and stale device
identities. Verify rollback at every acquisition stage, no implicit software
fallback, shader option/cache identity checks, and feature reports that remain
unavailable until implemented.

Packaged native lanes verify executable exports against dependency metadata,
payload signatures/digests/architecture, loader-selected runtime identity,
startup without an SDK/toolchain, independent install/remove/update recovery,
and source-less baseline DXIL/pipeline creation. Run shared parity and GPU image
tests on each published hardware/driver/OS combination, including both supported
app-local and newer-inbox runtime selection where available. WARP tests are
separate deterministic evidence, never hardware qualification. M0 validation
checks docs and examples, not future executable or GPU behavior.

## Consequences

Feature level 12_0 plus Shader Model 6.0 excludes older D3D12-capable hardware,
but provides one deliberate baseline without claiming Ultimate features. Root
signature 1.0 and legacy barriers keep advanced native paths optional. Separate
feature admission remains necessary even with a recent Agility runtime.

Pinning Agility makes the build dependency reviewable while allowing supported
Windows loader servicing. The executable-owned activation model deliberately
couples SDK upgrades to a coherent host/component restart transaction; it avoids
multiple renderer packages racing to change process-wide runtime selection.

## Rejected Alternatives

- **Inbox-only or floating-latest Agility:** leaves the release dependency or
  new API availability implicit. Pin the app-local contract and observe actual
  OS loader selection instead.
- **Feature level 11_0 / Shader Model 5.1 fallback:** introduces an additional
  asset/implementation baseline without an M0 requirement.
- **Require DirectX 12 Ultimate / enhanced barriers everywhere:** unnecessarily
  couples optional features to baseline availability.
- **Each renderer/extension activates its own SDK:** conflicts with host-owned
  process initialization and independently verified component composition.
- **Require Windows SDK, DXC, PIX, or debug layers for players:** confuses
  authoring/diagnostic tooling with packaged-game runtime requirements.
- **Silently use WARP or another backend:** changes selected device/product
  behavior and conceals failure rather than providing explicit recovery.
