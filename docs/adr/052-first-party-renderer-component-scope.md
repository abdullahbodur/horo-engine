# ADR-052: First-Party Renderer Component Scope

- **Status**: Proposed
- **Date**: 2026-09-01
- **Supersedes**: None
- **Scope**: Independently distributable first-party renderer artifacts, lifecycle ownership and no-renderer recovery
- **Issue**: [RND-002.1](https://github.com/abdullahbodur/horo-engine/issues/152)
- **Jira**: [HORO-152](https://horo-engine.atlassian.net/browse/HORO-152)
- **Companion decisions**: [ADR-028](028-renderer-capability-limits-and-product-profiles.md), [ADR-029](029-opengl-core-profile-and-platform-policy.md), [ADR-030](030-metal-platform-and-feature-baseline.md), [ADR-031](031-vulkan-loader-platform-and-version-baseline.md), [ADR-032](032-d3d12-baseline-and-agility-sdk-policy.md)
- **Normative documents**: [Renderer Distribution And Availability](../architecture/runtime/renderer-distribution-and-availability.md), [Renderer Module Package Manifest](../architecture/runtime/renderer-module-package-manifest.md), [Rendering Architecture](../architecture/runtime/rendering-architecture.md), [Render Backend Parity Contract](../architecture/runtime/render-backend-parity-contract.md), [System Design](../architecture/foundation/system-design.md)

## Context

Horo renderer implementations need independent installation and update without
turning backend-native APIs into public contracts or making projects load native
plugins. “Renderer component” is currently used for several different things:
backend code, package archives, installed records, system drivers and optional
developer SDKs. Treating those as one unit would give installation authority to
runtime rendering, duplicate shared runtimes or imply that a file on disk is
available and active.

Installation, host compatibility, runtime probing, user/project selection and
live activation also change independently. A single linear lifecycle enum cannot
honestly represent, for example, an installed component that is host-unsupported,
a selected component awaiting restart, or an active old version while a new
version is staged.

## Decision

### 1. One first-party component owns one interactive backend identity

The independently distributable unit is a signed Horo product component with one
stable `RenderBackendId`, for example:

```text
horo.renderer.opengl  -> opengl
horo.renderer.metal   -> metal
horo.renderer.vulkan  -> vulkan
horo.renderer.d3d12   -> d3d12
```

One component version may contain multiple OS/architecture variants of that same
backend but cannot contain multiple backend identities, select another backend or
declare a fallback. Package version and private module ABI are independent of the
stable backend ID selected by a project.

The component's signed file allowlist may contain only artifacts required to
realize that backend:

- the backend implementation and private renderer C-ABI entry module;
- matching backend-private presentation, editor GUI/viewport and host adapter
  code/data when those are dynamically delivered with the module;
- backend-owned immutable shader/runtime tables and generated mapping data;
- declared backend-local runtime libraries whose redistribution policy permits
  component ownership;
- signed metadata, platform variants, licenses/notices and bounded diagnostics
  schemas.

Every executable/library file is variant-declared, hashed and signed. The package
does not contain project assets, user configuration, machine probe results,
arbitrary tools, another renderer, an editor executable or unrestricted scripts.
Optional developer validation/capture tools remain separate toolchains/system
software and are not runtime availability prerequisites unless an explicit
feature operation requests them.

### 2. Core renderer contracts and product services are not components

The editor/engine installation owns and versions:

- public `RenderApi`, backend-neutral `RenderFrontend` and graph/resource models;
- private `RenderModuleHost`, C-ABI host tables/adapters and ABI negotiation;
- product component catalog/manager, trust roots, installer, probe orchestrator,
  selection policy and bootstrap recovery surface;
- platform path/window/process primitives and shared application composition;
- `RenderNull` for headless tools/tests; and
- the public extension SDK, which does not expose or promise the renderer module
  ABI.

These artifacts cannot be replaced by a renderer package. A component implements
host-owned contracts but does not ship its own copy as authority. ABI negotiation
may accept compatible table revisions; duplicate core libraries cannot override
the running host.

Application-owned shared runtimes, such as an executable-selected D3D12 Agility
SDK payload, remain product/runtime dependencies with one coordinated version.
A renderer manifest references their exact compatibility record; it cannot bundle
a private competing copy or mutate application runtime selection during load.
System drivers/loaders/frameworks remain host runtime facts, never installed Horo
renderer files unless a separate approved product-runtime component explicitly
owns redistribution.

### 3. Renderer components are product state, not project dependencies or plugins

Installation records live in verified Horo product component roots and are scoped
to the machine/user plus editor ABI. Projects persist only a stable backend ID and
explicit fallback policy. They never persist package paths/versions, signatures,
trust decisions, probe results or native runtime identity.

A project `.horopkg`, imported asset, extension or gameplay module cannot supply,
install, update, select or activate native renderer code. Opening a project cannot
expand trusted component roots or approve a package. Local developer components
require explicit developer policy and visible development provenance; they are
not portable project state or Shipping support evidence.

This private first-party module boundary is not a third-party renderer SDK. Any
future external renderer ecosystem requires a separate trust, compatibility,
support, distribution and ABI decision. The current private ABI may change with
coordinated first-party packages.

### 4. Lifecycle facts are orthogonal immutable snapshots

`RendererComponentSnapshot` contains separate dimensions:

```cpp
struct RendererComponentSnapshot {
    RenderBackendId backend;
    RendererCatalogState catalog;
    RendererInstallState install;
    RendererVerificationState verification;
    RendererHostSupportState hostSupport;
    RendererRuntimeAvailabilityState runtime;
    RendererSelectionState selection;
    RendererActivationState activation;
    RendererComponentRevision revision;
};
```

The dimensions mean:

| Dimension | Representative states | Authority |
|---|---|---|
| Catalog | `Unknown`, `Known`, `Retired` | signed product catalog/release channel |
| Install | `NotInstalled`, `Downloading`, `Staged`, `Installed`, `RepairRequired`, `Removing` | component manager/install record |
| Verification | `Unchecked`, `Verifying`, `Verified`, `AbiMismatch`, `SignatureInvalid`, `Quarantined` | verifier/trust and ABI policy |
| Host support | `Unknown`, `Supported`, `UnsupportedOs`, `UnsupportedArchitecture`, `UnsupportedOsVersion` | signed manifest + platform facts |
| Runtime availability | `Unknown`, `ProbeRequired`, `Probing`, `Available`, `MissingRuntime`, `ProbeFailed`, `ProbeTimedOut`, `ProbeCrashed`, `Stale` | probe service result/identity |
| Selection | `NotSelected`, `Selected`, `SelectedForNextStart` | command/project/user/host policy resolver |
| Activation | `Inactive`, `Loading`, `Negotiating`, `Initializing`, `Active`, `Failed`, `Stopping` | current host composition instance |

Installed means an install record exists; it does not imply verified, supported,
available, selected or active. Host-supported is metadata compatibility, not a
driver/device claim. Runtime-available requires a current successful bounded
probe for exact package/host/runtime identity. Selected means policy chose an
exact candidate for a startup attempt. Active means the current process completed
module negotiation and backend initialization for an exact generation.

Snapshots are immutable/revisioned and may describe multiple installed versions
plus one active instance. Derived UI actions come from the dimensions but do not
replace them with one lossy status. Impossible combinations are rejected at
transaction boundaries; asynchronous work commits only if its source revision is
still current.

### 5. Ownership follows product/application/runtime boundaries

| Responsibility | Owner | Must not own it |
|---|---|---|
| Catalog/release metadata and signed archives | Delivery/product distribution | render frontend, project |
| Download/install/verify/repair/update/rollback/remove | application component manager | backend module, editor tab |
| Host metadata compatibility | component resolver + Platform facts | native probe/device |
| Bounded runtime/device probe | application probe service + helper process | editor workspace, render frontend |
| Selection/fallback resolution | application startup policy | package, backend, render feature/profile |
| Exact module load/private ABI negotiation | `RenderModuleHost` at composition root | `RenderFrontend`, project/plugin |
| Device initialization and reported capabilities | selected backend instance | installer/catalog |
| Effective capability admission | `RenderFrontend` policy over initialized facts | package hints/probe alone |
| No-renderer recovery UI/CLI | HoroEditor bootstrap/application services | Null or another interactive backend |

Module metadata and registration descriptors remain inert. Reading a manifest,
constructing a provider or registering it cannot create windows/devices, inspect
ambient service locators, activate lifecycle callbacks or mutate component state.
Side effects occur in explicit install/probe/startup operations.

### 6. Selection precedes presentation-capable window creation

Startup resolves one exact candidate in this order:

1. command-line request;
2. project backend setting;
3. user preference; then
4. deterministic host recommendation.

The resolver evaluates only exact installed versions whose verification,
host-support and current probe dimensions permit activation. It reads signed
backend-neutral window requirements before native module load. Interactive window
and renderer creation begin only after selection commits.

Selection does not make the component active. The composition root loads the
exact verified path, negotiates the private ABI, attaches the matching private
presentation/editor integration, registers/seals one provider, constructs the
frontend and initializes the device. Every step has a typed result and rollback;
failure releases completed steps in reverse order without mutating project files.

Dynamic device capabilities are authoritative only after initialization. Manifest
hints and probe results may reject impossibility early but cannot grant a feature,
format or limit. ADR-028 effective capability admission remains required before
scene/editor renderer resources are created.

### 7. Fallback is explicit policy, never component behavior

A failed/missing requested backend returns its exact state dimensions and action:
install, verify, repair, update, re-probe, choose another backend or open
diagnostics. It does not silently select the platform-default API or load a library
found on `PATH`.

An ordered fallback list may be evaluated only when the initiating host/profile
explicitly owns one. Each candidate goes through the full independent resolution
and startup sequence, and diagnostics retain every attempt. A renderer package,
shader/material quality fallback, probe helper or backend cannot append/reorder
that list. Interactive editor/game hosts never fall back to Null. Headless tools
may request Null explicitly or include it in an explicit headless policy.

Changing project/user selection while a renderer is active records
`SelectedForNextStart`; it does not hot-swap the active device or unload code still
leased by the process. Device-loss recovery within the same active backend follows
renderer lifecycle policy and does not imply selecting another product component.

### 8. No-renderer recovery remains inside HoroEditor without RenderApi

If no interactive candidate can activate, HoroEditor does not create a renderer-
specific window, enter the workspace or modify project selection. It publishes a
typed aggregate containing requested ID, candidate dimensions, attempted versions,
safe causes and allowed actions.

The HoroEditor bootstrap/Welcome component-manager surface provides install,
offline install, verify, repair, update, re-probe, diagnostics and explicit backend
selection. CLI exposes equivalent application operations. This minimal surface
uses a bounded platform-native/software bootstrap presentation path outside
RenderApi; it cannot render project/editor workspace content and does not privilege
OpenGL, Metal, Vulkan or D3D12.

Recovery actions are transactional and cancellable. Failure preserves the last
verified installed version and active selection record. Restart into the workspace
occurs only after an exact candidate reaches activation prerequisites; the recovery
surface cannot claim success from installation alone.

### 9. Resource limits, concurrency and shutdown are bounded

Catalog/install snapshots, installed versions, variants, files and diagnostics
have schema count/byte limits. Component operations use one writer transaction per
component ID and bounded global download/extract/probe concurrency. Checked size
and path validation precede allocation/extraction. Native code is never loaded
from staging or quarantined roots.

Install/probe/activation each own cancellation tokens and explicit leases. Remove/
rollback cannot delete a version held by an active/probe/module lease. Cancellation
before commit removes private staging; after publication it cannot pretend the
committed record did not exist. Probe or activation timeout terminates/rolls back
through its owner, not by leaking a worker or module callback.

Shutdown rejects new operations, cancels queued work, terminates bounded helpers,
waits for component transactions, stops/deinitializes the active backend, releases
module callbacks/leases and then destroys registries/services. It is idempotent
after partial startup and never waits for network, editor UI or global GPU idle.

## Migration And Verification

The existing linear availability state migrates to orthogonal snapshot dimensions.
UI and CLI may derive one summary badge/action, but persistence and control flow use
the full typed snapshot/revision. Current statically linked developer/test backends
remain explicit composition fixtures; installed products load only verified
component records through the same provider lifecycle.

Tests must cover component file classification; one-backend package identity;
forbidden core/project/plugin/native leakage; every state dimension and stale-
revision commit; installed-but-unverified/unsupported/unavailable/selected-
inactive combinations; selection priority and explicit fallback attempts; pre-
window metadata; activation rollback at each step; no-renderer bootstrap and CLI
equivalence; Null headless-only policy; active leases during remove/update;
cancellation, timeout, helper crash, partial startup and shutdown; OpenGL-only,
Metal-only where supported, combined and no-interactive-component installations.

## Consequences

Renderer implementations can ship independently without weakening backend-neutral
contracts or giving projects native loading authority. Product health, policy and
live runtime truth stay distinguishable, and zero-renderer recovery is possible
without a privileged fallback API. The cost is a richer state snapshot, private
module/adapter packaging, explicit composition and coordinated product-runtime/
ABI versioning.

## Rejected Alternatives

| Option | Decision and reason |
|---|---|
| Bundle every renderer permanently into HoroEditor | Rejected: forces unused native dependencies and prevents independent repair/update. |
| Put multiple backends in one renderer component | Rejected: couples lifecycle/trust and enables hidden selection/fallback. |
| Let projects or plugins ship renderer modules | Rejected: project content would gain native product-code authority. |
| Publish the private renderer C ABI as the extension SDK | Rejected: freezes an unsupported trust/support boundary. |
| Treat installed as available/active | Rejected: files do not prove host support, runtime/device health, selection or initialization. |
| Model all lifecycle facts as one enum | Rejected: independent install, health, policy and process states cannot be represented honestly. |
| Search `PATH` or project folders for renderer libraries | Rejected: bypasses signed records, deterministic resolution and trust roots. |
| Silently choose the platform-default backend | Rejected: hides failures and privileges one backend. |
| Use Null as the editor recovery renderer | Rejected: Null is headless and cannot satisfy interactive parity. |
| Create a separate launcher solely for recovery | Rejected: HoroEditor owns one bootstrap/application recovery surface without RenderApi. |
