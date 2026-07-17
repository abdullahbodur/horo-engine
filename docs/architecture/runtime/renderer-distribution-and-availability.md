# Renderer Distribution And Availability

## Purpose

This document defines how HoroEditor discovers, installs, verifies, probes,
selects, repairs, updates, and activates renderer backend components.

Renderer implementations are optional product components. Installing HoroEditor
does not imply that OpenGL, Metal, Vulkan, or every future renderer component is
installed. A valid product installation may contain any compatible subset, or no
interactive renderer component at all.

The equal rendering behavior of installed interactive backends is defined by
[Render Backend Parity Contract](./render-backend-parity-contract.md). Package
metadata is defined by
[Renderer Module Package Manifest](./renderer-module-package-manifest.md).

## Terminology

The following are distinct:

- **Renderer component**: Horo's implementation package, such as
  `horo.renderer.metal` or `horo.renderer.vulkan`.
- **Native graphics runtime**: operating-system or driver functionality such as
  Metal, OpenGL, or a Vulkan loader and ICD.
- **Development toolchain**: optional authoring tools such as Vulkan SDK,
  validation layers, `glslc`, DXC, or Metal developer tools.

A renderer component may depend on a system runtime. Development toolchains are
not required merely to run HoroEditor unless a feature explicitly invokes those
tools.

Normal renderer selection never stores a raw native-library path. Project and
user settings select a stable `RenderBackendId`. Optional SDK/tool executable
paths belong to typed Toolchains settings and are not renderer identities.

## Core Decisions

- Renderer components are installed independently from the editor core.
- OpenGL, Metal, Vulkan, and future renderer components use the same component
  lifecycle and trust policy.
- The product component registry, not arbitrary filesystem scanning, is the
  source of installed renderer discovery.
- Installation, host support, runtime availability, successful probing, and
  activation are separate states.
- Renderer packages contain signed first-party native code and are never treated
  as data-only packages.
- A backend is probed in a bounded helper process before the editor loads it.
- Backend selection occurs before creation of the editor's presentation-capable
  window.
- Silent fallback is forbidden. Explicit fallback policy remains possible for
  profiles that opt into it.
- The editor workspace cannot run with the Null backend or with no interactive
  backend. The `HoroEditor` bootstrap/Welcome surface handles component setup
  without creating a separate launcher application or depending on a RenderApi
  backend.
- Project files store backend IDs, never installation paths, trust decisions, or
  machine-local probe results.

## Product Boundary

Renderer components are Horo product components, not project `.horopkg`
dependencies. They reuse the transactional staging, integrity, signature,
quarantine, and rollback primitives defined by the package and distribution
systems, but they have separate ownership and activation rules:

- renderer installation is machine/user product state;
- renderer selection may be project policy;
- renderer trust is established by Horo's product trust root or managed
  enterprise policy;
- a project cannot supply or auto-activate a native renderer module;
- renderer updates are coordinated with editor ABI compatibility;
- renderer uninstall cannot mutate project files.

Third-party renderer extensibility is outside this contract. It requires a
separate security, compatibility, and support policy.

## Component Roots

Renderer discovery is restricted to roots owned by the Horo product component
manager:

1. immutable components bundled with the active editor version;
2. verified system-wide Horo component storage;
3. verified per-user Horo component storage;
4. an explicit developer component registry enabled by developer policy.

Exact paths are resolved through platform path services. Product code does not
hardcode one cross-platform directory.

The loader does not recursively search `PATH`, the working directory, project
directories, SDK directories, download directories, or arbitrary user-selected
folders for native renderer libraries.

A developer may explicitly register a local build through a developer-only
component record. That record is visibly untrusted/development state, is never
portable project configuration, and is disabled in release or managed-policy
builds unless policy allows it.

## Availability State Model

A renderer component has one discovery/health state at a time:

```text
NotInstalled
    -> Downloading
    -> Staged
    -> InstalledUnchecked
    -> Verifying
    -> Verified
    -> Probing
    -> Available
    -> Selected
    -> Active
```

Failure and maintenance states:

```text
HostUnsupported
MissingRuntime
AbiMismatch
SignatureInvalid
ProbeFailed
Quarantined
UpdateRequired
RepairRequired
```

State meanings:

| State | Meaning |
|---|---|
| `NotInstalled` | No verified installation record exists for the backend ID and active editor ABI. |
| `InstalledUnchecked` | Files are present, but current integrity and compatibility checks have not completed. |
| `Verified` | Manifest, layout, hashes, signatures, ABI metadata, and platform identity are valid. No device claim has been made. |
| `HostUnsupported` | Package metadata excludes the current OS, architecture, or minimum OS version. |
| `MissingRuntime` | Required system loader, driver capability, or runtime dependency is absent. |
| `AbiMismatch` | Module ABI is incompatible with the active editor/engine ABI. |
| `ProbeFailed` | The helper process returned a typed failure, timed out, crashed, or produced invalid output. |
| `Quarantined` | Security or repeated health failure prevents loading until repair or explicit policy action. |
| `Available` | Verification and the current host probe succeeded. |
| `Selected` | Startup policy chose the backend; no live device is implied yet. |
| `Active` | The editor successfully loaded and initialized the selected backend. |
| `UpdateRequired` | A compatible module update is required before activation. |
| `RepairRequired` | Installed state is incomplete, corrupted, or inconsistent with its install record. |

`Compiled`, `installed`, `verified`, `available`, and `active` are not synonyms.
Capability hints in a manifest are not authoritative device capabilities.

## Resolution And Selection

Selection input priority remains:

1. explicit command-line override;
2. project renderer setting;
3. user renderer preference;
4. host recommendation policy.

Resolution produces a requested backend ID before window creation. The component
registry then resolves that ID to one exact verified package version compatible
with the active editor ABI.

A requested backend that is missing or unavailable does not silently become a
different backend. The result is an actionable typed diagnostic containing:

- requested backend ID;
- component lifecycle state;
- editor and renderer ABI versions;
- host OS and architecture;
- safe failure category;
- install, repair, update, or diagnostics action when applicable.

An explicitly configured fallback list may be evaluated in order. Diagnostics
record every attempted candidate. Interactive hosts never fall back to Null.

## Automatic Detection

Automatic detection ranks only components that are both installed and
`Available`. It must not infer availability solely from operating system or GPU
name.

Host recommendation may consider:

- platform support policy;
- successful probe timestamp and module version;
- required project capabilities;
- known driver/runtime deny lists;
- organization policy;
- user preference.

Recommendation is deterministic for the same component registry, host facts,
and policy snapshot. It is a selection policy, not an architectural priority
between backends.

## Install, Repair, And Update Flow

Renderer component installation is transactional:

```text
Resolve signed component metadata
    -> download to private staging
    -> verify size and archive hash
    -> extract with path and resource limits
    -> verify every declared file
    -> verify platform signature and Horo manifest signature
    -> validate manifest and module ABI
    -> atomically publish installation record and component directory
    -> probe in helper process
    -> mark Available or retain actionable failure state
```

No editor process loads files from an incomplete staging directory. Cancellation
or failure leaves the previous verified component active. Corrupt or
signature-invalid files are quarantined rather than activated.

Repair re-verifies the current install record and replaces damaged files through
the same staged transaction. Update installs a new version beside the old one,
probes it, and atomically changes the active component record only after success.
The previous verified version remains eligible for rollback until retention
policy removes it.

## Probe Process

Native backend loading and minimum device creation are tested in a dedicated
helper process. The helper receives structured arguments and a minimal
environment; it does not search arbitrary library paths.

The probe performs only bounded startup validation:

1. load the exact verified module path from the component record;
2. negotiate the private renderer module ABI;
3. validate required runtime dependencies;
4. enumerate or create the minimum supported device/context state;
5. validate presentation-surface support when the host can provide a probe
   surface;
6. return a versioned structured result;
7. release all native resources and unload or exit.

The parent applies timeout, cancellation, output-size, and crash handling.
Crashes, malformed output, timeout, and signal termination become `ProbeFailed`
and may trigger quarantine after policy-defined repetition. Probe logs contain
safe diagnostics and never include unrestricted environment dumps.

A successful probe is scoped to module version, editor ABI, host OS build,
architecture, and relevant driver/runtime identity. Changes to those inputs
invalidate cached availability and require a new probe.

## No-Renderer Flow

A graphical editor workspace requires an interactive renderer. `RenderNull`
supports headless tools and tests; it is not an editor GUI fallback.

When no interactive backend is `Available`:

- HoroEditor does not create a RenderApi device or enter the editor workspace;
- startup returns a typed `renderer.no_available_backend` diagnostic;
- the bootstrap/Welcome component-manager surface presents install, repair,
  diagnostics, and offline-package actions;
- CLI exposes equivalent non-graphical operations;
- project files remain untouched.

The bootstrap repair surface is part of `HoroEditor`, but its minimal product UI
presentation path must not depend on a Horo engine renderer component. It may use
a platform-native or bounded software/bootstrap adapter, cannot render the editor
workspace or project viewport, and remains outside runtime RenderApi. This
prevents the zero-renderer recovery path from secretly privileging OpenGL, Metal,
or another engine backend while preserving the single-application screen model.

## CLI And Component Manager Operations

The intended product operations are:

```text
horo renderer list
horo renderer install <id>
horo renderer repair <id>
horo renderer remove <id>
horo renderer probe <id>
horo renderer doctor [<id>]
horo renderer select <id> --scope user|project
```

Command names are architectural intent, not a statement that the current CLI
already implements them. The Welcome/component-manager surface and CLI call the
same application services and produce the same lifecycle states and diagnostics.

Removing the active or selected backend requires explicit resolution. Uninstall
is blocked while a running process holds a module lease. Project references do
not block uninstall, but affected projects show `NotInstalled` on their next
resolution attempt.

## Project Settings Presentation

Project Settings consumes component registry snapshots rather than filesystem
paths. Each backend entry shows at least:

- display name and stable ID;
- installed version;
- lifecycle/health state;
- host support result;
- last probe result and safe diagnostic summary;
- install, repair, update, diagnostics, or remove actions;
- active/selected state;
- restart requirement.

Selecting a missing backend starts or delegates to component installation. The
project setting is committed only after policy-defined install/probe success, or
stored explicitly as a pending unresolved request when offline workflow allows
it. The UI must distinguish those outcomes.

## Recent Project Renderer Preflight

The Welcome screen preflights every recent project's requested
`RenderBackendId` against the current component-registry snapshot. It does not
load the project workspace, create the project's renderer, or mutate project
metadata while producing the card state.

Canonical card states include:

```text
Ready
RendererNotInstalled
RendererUnavailable
RendererRepairRequired
RendererUpdateRequired
RendererCapabilityMismatch
ProjectMetadataUnreadable
```

A recent-project card displays the requested backend and actionable local state,
for example:

```text
My Game
OpenGL · Component not installed
```

When the user opens that project, the Welcome screen resolves the mismatch before
navigating to project loading. If OpenGL is requested but missing while Metal is
`Available`, the modal offers:

```text
Renderer Unavailable

This project is configured to use OpenGL, but the OpenGL renderer component is
not installed on this machine. Metal is installed and available.

[Install OpenGL]
[Use Metal for This Project]
[Cancel]
```

There is no separate `Open Once with Metal` action in the normal product flow.
Using a different renderer for a project changes the project's declared renderer
so persisted configuration and actual behavior do not diverge. Explicit CLI
overrides remain engineering/diagnostic controls and do not define this GUI
workflow.

`Install OpenGL` preserves project metadata. The component manager installs,
verifies, and probes OpenGL; project loading begins only after it becomes
`Available`.

`Use Metal for This Project` is a persistent project mutation. The label and
confirmation must state that the project setting will change and may affect other
developers. The Welcome screen invokes a shared application use case rather than
editing project JSON directly. The use case:

```text
Change Project Renderer?

This project currently uses OpenGL. OpenGL is not installed on this machine.
Using Metal will update the project's renderer setting from OpenGL to Metal and
may affect other developers who open this project.

Backend-specific shaders and derived rendering caches will be rebuilt. Source
scenes and assets will not be modified.

[Use Metal for This Project]
[Cancel]
```

After confirmation, the use case:

1. acquires the project metadata/write lock;
2. validates project format and current renderer revision;
3. verifies that Metal is still `Available`;
4. validates project-required capabilities against Metal;
5. computes the derived shader/render-cache invalidation plan;
6. presents the mutation and rebuild consequences for confirmation;
7. transactionally writes `render.backend = "metal"`;
8. invalidates only backend-specific derived data;
9. continues project loading through the normal selected-backend startup path.

Source scenes and assets are not converted merely because the renderer setting
changes. Backend-specific derived shaders, pipelines, and caches are rebuilt.

If Metal cannot satisfy required project capabilities, `Use Metal for This
Project` is disabled and the modal lists the typed missing capabilities. If no
alternative backend is `Available`, only install, repair, diagnostics,
offline-package, and cancel actions are offered.

Repair and update states use the same modal with `Repair OpenGL` or `Update
OpenGL` as the primary action. Renderer changes are never silent, and cancelling
leaves the recent-project card and project files unchanged.

## Runtime And SDK Paths

Runtime renderer selection does not ask users for native library paths.
Toolchain settings may expose validated paths for development tools, for example:

```text
toolchains.vulkan.sdkRoot
toolchains.shader.glslc
toolchains.shader.dxc
toolchains.metal.developerDir
```

Those paths affect shader compilation, validation, capture, or development
features. They do not change the selected renderer module or bypass component
verification. A Vulkan SDK is not required merely because the Vulkan renderer is
selected unless a documented runtime packaging policy explicitly says so.

## Security And Platform Policy

Renderer modules execute native code with editor-process privileges. Required
controls include:

- signed component metadata and artifact hashes;
- trusted product signing identity;
- Windows Authenticode verification where required;
- macOS hardened-runtime-compatible code signing and notarization policy;
- Linux repository/package signature or Horo component signature;
- exact architecture and ABI validation;
- controlled load paths and dependency resolution;
- no current-working-directory native library lookup;
- helper-process probing before editor activation;
- quarantine and rollback support;
- audit-safe install, repair, probe, activation, and failure records.

On macOS, optional modules outside the sealed application bundle must satisfy the
active hardened-runtime library-validation and Team ID policy. Installing a file
does not make it loadable until platform verification succeeds.

## Required Tests

- install any supported subset of renderer components;
- valid editor core installation with no renderer component;
- no-renderer startup routes to a typed repair result without creating a graphics
  window;
- arbitrary project and working-directory libraries are ignored;
- invalid manifest, hash, signature, layout, architecture, and ABI are rejected;
- interrupted install never publishes partial state;
- update failure retains the prior verified component;
- probe success, typed failure, timeout, crash, and malformed output;
- cached probe invalidation after module, OS, or driver/runtime change;
- explicit selection does not silently fall back;
- auto-detection considers only `Available` candidates;
- project settings persist ID but no native path or trust state;
- recent-project preflight reports missing, repair, update, unavailable, and
  capability-mismatch states without loading a project renderer;
- missing requested renderer modal offers install or one explicit persistent
  compatible replacement, with no duplicate session-only action;
- persistent replacement uses transactional project mutation, rejects stale
  revisions/capability mismatch, and invalidates only backend-derived data;
- cancelling renderer resolution leaves project metadata unchanged;
- active module uninstall is blocked by a lease;
- offline signed component install and repair;
- platform signing/library-validation behavior on packaged artifacts.

## Related Documents

- [Render Backend Parity Contract](./render-backend-parity-contract.md)
- [Renderer Module Package Manifest](./renderer-module-package-manifest.md)
- [Rendering Architecture](./rendering-architecture.md)
- [Package Lifecycle](../packages/package-lifecycle.md)
- [Distribution And Update Architecture](../release/distribution-and-update.md)
- [Release Security](../release/release-security.md)
- [Error And Diagnostics](../foundation/error-and-diagnostics.md)
- [GUI Screen Host](../editor/gui-screen-host.md)
- [Project Model](../editor/project-model.md)
