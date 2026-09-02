# ADR-158: OpenXR Loader, Backend Packaging and Host Composition

- **Status**: Proposed
- **Date**: 2026-09-02
- **Supersedes**: None
- **Scope**: First-party OpenXR target/package topology, loader dependency and dispatch ownership, verified installation, application-host composition, platform-specific packaging, preflight states, lifecycle, unsupported paths, migration and release evidence
- **Issue**: [XRA-002.1](https://github.com/abdullahbodur/horo-engine/issues/2118)
- **Jira**: [HORO-2072](https://horo-engine.atlassian.net/browse/HORO-2072)
- **Related**: [ADR-008](008-error-model-exception-boundary-and-registry.md), [ADR-018](018-command-registration-permissions-threading-and-packaged-build-policy.md), [ADR-028](028-renderer-capability-limits-and-product-profiles.md), [ADR-033](033-presentation-and-display-ownership.md), [ADR-054](054-extension-and-package-authority-boundary.md), [ADR-057](057-package-manifest-v1-typed-model.md), [ADR-157](157-xr-ownership-runtime-composition-and-capability-tier.md)
- **Normative documents**: [XR Architecture](../architecture/runtime/vr-ar-architecture.md), [System Design](../architecture/foundation/system-design.md), [Platform Abstraction](../architecture/foundation/platform-abstraction.md), [Android Platform Host](../architecture/foundation/android-platform-host.md)

## Context

ADR-157 establishes `XRApi`, `XRRuntime` and `XROpenXR`, makes the application host
the sole XR composition authority and keeps native OpenXR state inside the backend. It
does not yet decide how the backend and OpenXR loader reach a product, which owner
validates their installation, whether the backend is a general extension, or how a host
distinguishes a missing package from a missing runtime or unsupported system.

Those distinctions affect build topology, security, diagnostics and downstream
lifecycle work. Treating every failure as “OpenXR unavailable” would hide whether the
product omitted the feature, shipped an incompatible loader, found no active runtime,
or found a runtime without an admissible system. Loading a vendor runtime directly
would bypass the loader contract. Letting `XRRuntime`, Platform or a directory scanner
select native artifacts would create competing composition authorities.

The repository has no production OpenXR backend today. This decision fixes an
implementable first-party packaging and composition contract without declaring any
runtime, headset or platform supported. XRA-002.2 and later tickets specialize loader
discovery, runtime selection, dispatch, event handling and fault behavior within the
owners fixed here.

## Decision

### 1. OpenXR is an optional first-party product component

The logical target topology remains:

```text
HoroEngine::XRApi
        ^
        |
HoroEngine::XRRuntime
        ^
        |
application composition root ------ HoroEngine::XROpenXR
                                           |
                                           +-- private OpenXR headers
                                           +-- private loader linkage/dispatch
                                           +-- private Platform/Renderer bridges
```

`HoroEngine::XROpenXR` is a Horo-owned backend target and optional product component.
It is not part of `XRApi`, not an implicit dependency of `XRRuntime`, and not a general
third-party extension discovered by `ExtensionHost`. The product build and application
composition explicitly include it. A non-XR product omits the target, loader artifact,
native headers, runtime probes, environment policy and background work.

The stable backend identity is a Horo-owned `XRBackendId`; a package filename, shared-
library name, CMake target name, OpenXR runtime name or loader path is never identity.
The first-party OpenXR descriptor declares its backend ID, Horo contract version,
supported target tuples and required bridge-contract versions. Descriptor construction
and validation are inert metadata operations.

This decision does not freeze whether every supported product links the backend code
statically or as a Horo-shipped dynamic module. That is a build-profile detail behind
one composition record. Both forms must preserve the same ownership, verified-input,
single-selection and unload rules; neither permits ambient directory discovery or a
public C++ plugin ABI.

### 2. Artifact installation, native loading and runtime selection have distinct owners

| Responsibility | Sole owner | Deliberate non-owner |
|---|---|---|
| OpenXR source/version pin, build options, hashes and license inventory | Dependency/build policy | XRRuntime and Platform |
| Product inclusion, artifact layout, signing and provenance | Product packaging/release tooling | XROpenXR at runtime |
| Installed-component verification and exact activation candidate | Application package/install services | ExtensionHost and directory scanners |
| Backend selection and complete XR/Platform/Renderer/Input tuple | Application composition root | Loader and feature modules |
| Dynamic-library open/symbol/close primitives | Platform adapter | XRRuntime and gameplay |
| Loader API initialization, function dispatch and teardown | XROpenXR | Platform and Renderer |
| Active-runtime discovery semantics and runtime manifest interpretation | OpenXR loader/runtime ecosystem | Horo package resolver and Platform |
| OpenXR instance, system, session and native object lifecycle | XROpenXR | Loader packaging, XRRuntime and Renderer |
| Engine-visible generations, snapshots, plans and typed results | XRRuntime | XROpenXR native callbacks and diagnostics UI |
| Supported product tuple and qualification claim | Product/release policy | Successful loader/runtime discovery |

Platform lends a bounded library capability and platform-native initialization inputs
through private host adapters. It does not interpret OpenXR manifests, enumerate
runtimes, call OpenXR, choose a backend or own dispatch tables. XROpenXR uses the
official loader contract and never opens a vendor runtime library directly.

### 3. One verified install record is the only backend artifact input

Packaging produces an immutable `XROpenXRInstallRecord` before process composition. It
contains or references bounded typed values for:

- backend identity and Horo backend-contract version;
- product/build identity, platform, CPU ABI and linkage mode;
- backend and loader artifact identities, cryptographic digests and provenance;
- loader contract/API version range and build configuration;
- required Platform and Renderer bridge-contract versions;
- native dependency closure, licenses, signing status and release channel; and
- an explicit loader source policy for the exact target tuple.

The record never contains a live handle, runtime-selected system, vendor runtime
library, user-controlled search directory, process-lifetime callback, permission grant
or claim that a device tuple is supported. Package syntax and filesystem paths remain
private to package/install services. The application passes XROpenXR an exact verified
activation candidate rather than asking it to scan a directory or repair metadata.

A missing, duplicate, wrong-platform, wrong-ABI, unsigned-when-required, digest-mismatched
or contract-incompatible component fails before backend construction. Verification does
not open the loader, inspect a runtime, register services or mutate durable enablement.

The package services from ADR-054/ADR-057 may store and verify the first-party component,
but `ExtensionHost` does not activate it. XR needs a product-selected native backend
with private renderer/platform bridges and process/session ordering, not a runtime-
discoverable editor extension. Any future third-party XR provider requires a separate
versioned C ABI and trust decision; it cannot reuse Horo's private C++ target boundary.

### 4. Loader source policy is explicit per product target

`XRLoaderSourcePolicy` has finite product-owned modes:

- `BundledVerified`: the product ships the pinned, verified loader artifact recorded in
  its installation manifest; and
- `PlatformProvided`: the qualified platform image/distribution supplies a loader whose
  location and compatibility are resolved through a platform-owned system contract.

Every shipping target selects exactly one mode at build/package composition. There is
no “try bundled, then system, then vendor directory” fallback. `PlatformProvided` is
permitted only for a product tuple whose deployment contract and qualification evidence
control that loader; an arbitrary library found on `PATH`, the current directory, a
project directory or an environment-provided path does not qualify.

Windows, Linux and Android product profiles normally use a Horo-recorded
`BundledVerified` loader so the shipped artifact and provenance are deterministic. A
Linux distribution package may deliberately select `PlatformProvided` when its package
manager and qualification matrix own the loader contract. Android records one loader
artifact per shipped ABI and obtains application/activity initialization inputs from the
Android host. macOS and other target tuples remain typed `UnsupportedTarget` until an
explicit loader/runtime/deployment path is implemented and qualified.

The loader discovers an installed/active runtime through the OpenXR platform contract.
Horo does not package a vendor runtime or rewrite runtime manifests. Runtime-selection
overrides, API layers and validation layers are developer/qualification inputs governed
by an explicit non-shipping policy. Shipping profiles reject unapproved overrides and
do not inherit them accidentally from an ambient launch environment.

### 5. The host composes one immutable backend plan

After install verification and before native allocation, the application creates one
`XRBackendCompositionPlan` containing:

```cpp
struct XRBackendCompositionPlan {
    XRBackendId backend;
    XRBackendContractVersion contractVersion;
    XROpenXRInstallRecordId installRecord;
    XRLoaderSourcePolicy loaderSource;
    PlatformHostGeneration platform;
    RendererBackendId rendererBackend;
    RendererDeviceGeneration rendererDevice;
    InputRouterGeneration input;
    XRProductProfileId productProfile;
    XRDiagnosticPolicyId diagnostics;
};
```

The exact schema is finalized by implementation tickets, but these fields and their
separation are normative. The plan references already validated owner generations. It
contains no native handles, raw paths, extension-name lists or runtime product-name
branches. It is immutable for an activation attempt and receives a distinct identity
when any selected owner, loader policy, renderer device or product profile changes.

The composition root validates exactly one backend candidate, constructs Platform and
Renderer private bridges, creates XROpenXR, transfers its frontend port to `XRRuntime`,
and connects Input/UI adapters. No backend self-registers through a static initializer,
service locator, module descriptor or loader callback. No feature module can replace
the selected backend or renderer after composition.

### 6. Installation, discovery, support, activation and availability are separate states

The public/application model distinguishes at least:

```text
Component:  NotInstalled | Installed | Rejected
Backend:    NotComposed  | Composed  | Incompatible
Loader:     Unresolved   | Available | Absent | Incompatible
Runtime:    Unqueried    | Available | Unavailable | Rejected
System:     Unqueried    | Supported | Unsupported | TemporarilyUnavailable
Session:    Inactive     | Activating | Ready | Running | Stopping | Lost
Capability: Unavailable | Available | PermissionRequired | Denied | Admitted | Lost
```

These axes cannot collapse into one boolean. `Installed` does not mean a loader can
open; `LoaderAvailable` does not mean a runtime exists; a runtime does not guarantee an
XR system or product profile; a supported system does not mean a session is active; an
enabled native extension does not prove a Horo capability is admitted or currently
available.

XROpenXR produces bounded native preflight evidence. XRRuntime and the application
publish backend-neutral typed snapshots with an attempt/revision identity. Diagnostics
may render redacted details but cannot reinterpret states or promote discovery into a
support claim.

### 7. Composition and rollback follow one ordered transaction

The host performs an XR activation attempt in this order:

1. resolve product policy and one verified XROpenXR install record;
2. validate the backend descriptor and complete composition plan without side effects;
3. establish Platform lifecycle/library and Renderer device/bridge prerequisites;
4. construct XROpenXR and resolve/initialize the selected loader source;
5. create the OpenXR instance and discover/select the runtime system;
6. resolve the complete required/optional XR capability plan against Platform,
   Renderer, Input, permissions and product policy;
7. create action, space, session, swapchain and Renderer external-resource candidates;
8. connect Input/UI projections; then
9. atomically publish the new Ready XR generation.

Potentially failing work precedes publication. A failure destroys only candidate-owned
resources in reverse order and leaves no partial backend registration, session,
external image, Input projection or Ready generation. A replacing activation prepares
beside the prior generation only where all owners explicitly support overlap; matching
backend/runtime labels never make old handles current.

Shutdown closes frontend admission, disconnects Input/UI projections, retires Renderer
work and imported images, destroys swapchains/session/actions/spaces/instance, releases
loader dispatch and finally returns Platform library/activity leases. Static or dynamic
backend code cannot unload while callbacks, native objects, dispatch users, snapshots or
bridge leases remain.

### 8. Failures are stable, bounded and actionable

Stable typed causes distinguish package absent/rejected, target or ABI mismatch,
backend-contract mismatch, loader artifact missing/digest mismatch/open failure/symbol
mismatch/version incompatibility, runtime unavailable/rejected, system unsupported,
renderer bridge/device mismatch, profile unsupported, permission required/denied,
activation cancellation, instance/session loss and incomplete shutdown.

Each result carries only bounded owner, phase, attempt, target tuple and redacted native
evidence. Raw loader/runtime paths, environment values, native handles, device serials
and unbounded runtime strings do not cross the backend or enter metrics dimensions.
Package-signing or digest failure is not retried as runtime discovery. Runtime absence
does not cause the host to scan other directories, select another backend, change
renderer or silently continue in non-XR mode while claiming XR success.

Developer overrides are explicit typed launch inputs with provenance and visibly mark
the result non-release-qualified. Diagnostics display which loader source policy and
verified install record were selected without exposing sensitive paths.

### 9. Build and release outputs preserve the boundary

The XROpenXR target alone compiles with OpenXR headers and loader-facing definitions.
OpenXR include paths, compile definitions, link libraries and native handles remain
`PRIVATE`; they cannot appear in `XRApi`, `XRRuntime`, repository-wide public include
paths or unrelated consumers. Public-header ownership and consumer tests enforce this
boundary.

Every XR-enabled deliverable records the backend/install record, loader source and
artifact provenance in its build/package manifest. Package inspection verifies exactly
the expected platform/ABI artifacts, no vendor runtime payload, no development API
layers in shipping profiles, and no accidental loader dependency in non-XR products.

A loader or backend update changes the dependency/install record and invalidates prior
qualification for affected target tuples. It does not silently retain a second loader
as compatibility fallback. Security and license updates follow the dependency policy;
support remains a release decision backed by fresh evidence.

### 10. Migration and contract coverage are required

There is no production OpenXR implementation to preserve. Initial implementation must
create the private target/package boundary and host plan first; it must not start by
including OpenXR headers in public/runtime/platform headers or by teaching Platform to
discover runtimes.

Required automated coverage includes:

- target graph and public-header consumers proving native OpenXR dependencies remain
  private to XROpenXR;
- non-XR/headless packages with no backend, loader artifact, OpenXR symbols, probes or
  threads;
- verified install-record acceptance plus missing, duplicate, signature, digest,
  platform, ABI and contract rejection;
- inert descriptor/plan validation and absence of static registration, service-locator
  lookup and raw-directory discovery;
- exact `BundledVerified` and qualified `PlatformProvided` resolution without fallback;
- approved developer override versus shipping override rejection;
- distinct component/backend/loader/runtime/system/session/capability states and typed
  redacted diagnostics;
- construction failure and cancellation at every composition step with reverse-order
  rollback and no published partial generation;
- replacement and shutdown with no callback, dispatch, native object, GPU resource,
  snapshot, bridge or module lease surviving its owner; and
- package inspection and qualification evidence for every declared platform/ABI/
  renderer/runtime/product-profile tuple.

Deterministic fake loaders/runtimes may validate order and fault behavior, but do not
qualify a shipping loader, platform runtime or physical device.

## Consequences

### Positive

- Products have one explicit, auditable route from a verified OpenXR component to an
  application-selected backend.
- Package, Platform, loader, backend, XRRuntime and release authorities no longer blur
  into an “OpenXR available” boolean.
- Native headers, dispatch and handles remain private while diagnostics can identify the
  exact failing layer.
- Non-XR/headless products pay no OpenXR packaging or runtime cost.
- Platform-specific loader policy can vary without creating different XR ownership
  models.

### Negative

- XR-enabled products need target-specific loader packaging, provenance and
  qualification in addition to backend code.
- Dynamic and static linkage forms must implement the same lease/unload contract.
- Ambient runtime overrides and directory probing that may be convenient in local
  development are prohibited in shipping profiles.
- Loader/backend updates deliberately invalidate affected qualification evidence.

## Rejected Alternatives

### Make XROpenXR a general ExtensionHost plugin

Rejected because the first-party backend needs exact process-level Platform/Renderer
composition and native lifecycle ordering. The current extension boundary does not make
a private C++ ABI safe or grant permission for runtime-selected backend replacement.

### Link OpenXR into XRApi or XRRuntime

Rejected because native headers, loader symbols and version policy would leak into the
backend-neutral contract and every non-XR consumer.

### Let Platform own OpenXR discovery and session creation

Rejected because Platform owns OS primitives, not cross-platform XR runtime policy,
native OpenXR objects, capability admission or frame/session semantics.

### Load vendor runtime libraries directly

Rejected because it bypasses the OpenXR loader/runtime-discovery contract, creates
vendor-specific selection/security behavior and makes dispatch/version ownership
inconsistent.

### Search multiple loader locations and fall back silently

Rejected because ambient paths and environment state are neither verified product
inputs nor deterministic support policy. One exact source must fail visibly.

### Treat loader discovery as product support

Rejected because installation, runtime availability, system support, renderer/profile
compatibility, activation and qualification are independent facts.

### Ship one universal loader/backend artifact for every target

Rejected because platform, CPU ABI, packaging, signing and native initialization differ.
Products record and qualify exact target artifacts while preserving one Horo contract.
