# Render Backend Parity Contract

## Purpose

This document defines the common obligations of every interactive Horo renderer
backend. OpenGL, Metal, Vulkan, and future native backends are equal first-class
implementations of the same engine contracts. No backend is the architectural
base class, compatibility layer, fallback implementation, or privileged path for
another backend.

Implementation status does not change this rule. OpenGL may exist before Metal,
but code shared by the engine and editor must not encode OpenGL lifecycle,
resource, frame, presentation, or GUI assumptions.

Renderer components are independently installable. Discovery, verification,
probing, and no-renderer recovery are defined by
[Renderer Distribution And Availability](./renderer-distribution-and-availability.md).

## Non-Negotiable Invariants

- Every interactive backend implements the same required lifecycle and baseline
  rendering behavior.
- A backend target cannot link another concrete backend target.
- The frontend, scene, editor screens, project model, settings, CLI, and MCP code
  cannot branch on concrete backend C++ types.
- Native API and window-system types remain private to concrete backend or
  platform-adapter targets.
- Backend selection occurs before creation of the presentation-capable host
  window.
- Selection uses a stable `RenderBackendId`; it does not use compile-time
  `#if` branches in editor or runtime feature code.
- Optional capabilities may differ, but required baseline behavior may not.
- Capability differences are reported through typed values and never inferred
  from the backend identifier.
- Unsupported or unavailable backends return typed errors. Silent fallback is
  forbidden.
- Installed state does not imply availability, and availability does not imply
  activation.
- Project renderer changes are restart operations until an explicit live-device
  migration contract is approved.

## Equal Target Topology

Concrete backends are sibling targets:

```text
HoroEngine::RenderApi
        |
        +-- HoroEngine::RenderOpenGL
        |
        +-- HoroEngine::RenderMetal
        |
        +-- HoroEngine::RenderVulkan
        |
        +-- HoroEngine::RenderNull
```

These sibling build targets produce independently packageable renderer
components. An installed editor may contain any supported subset; runtime loading
does not change the dependency direction.

Editor integrations follow the same rule:

```text
HoroEngine::EditorRenderApi
        |
        +-- HoroEngine::EditorRenderOpenGL
        |
        +-- HoroEngine::EditorRenderMetal
```

Allowed dependency direction:

```text
Editor composition -> selected editor integration -> matching render backend
Render frontend    -> RenderApi
Concrete backend   -> RenderApi + private native dependencies
```

Forbidden dependency direction:

```text
RenderOpenGL -> RenderMetal
RenderMetal  -> RenderOpenGL
RenderApi    -> any concrete backend
EditorRenderApi -> any concrete backend
```

Shared implementation belongs in a backend-neutral target only when its
semantics are genuinely API-independent. OpenGL code is not moved into a shared
target merely so Metal can call it.

## Pre-Window Backend Description

The host must know presentation requirements before it creates a window. Every
installed renderer package therefore provides signed immutable manifest metadata
that can be read without loading native code or creating a device, context,
layer, surface, swapchain, queue, or worker thread.

The common value model is:

```cpp
enum class RenderPresentationKind : std::uint8_t {
    None,
    OpenGL,
    Metal,
    Vulkan,
};

struct RenderHostWindowRequirements {
    RenderPresentationKind presentation{RenderPresentationKind::None};
    bool resizable{true};
    bool highPixelDensity{true};
};

struct RenderBackendModuleInfo {
    RenderBackendId id;
    std::string displayName;
    RenderHostWindowRequirements windowRequirements;
    bool supportsInteractivePresentation{false};
};
```

These are backend-neutral policy values. They must not contain `SDL_Window`,
`SDL_GLContext`, `CAMetalLayer`, `MTLDevice`, `VkSurfaceKHR`, `NSWindow`, Win32,
X11, or Wayland types.

`RenderPresentationKind` describes the host surface family required by the
selected module. It does not expose a native surface handle and is not a proxy
for runtime capabilities.

The component service validates and converts package manifest metadata into this
common value model. The Render API does not parse package files or discover
native libraries.

## Startup Sequence

Every interactive host follows one sequence:

```text
Resolve requested RenderBackendId
        |
        v
Resolve installed component record
        |
        v
Verify manifest, signatures, variant, and ABI
        |
        v
Require a current successful availability probe
        |
        v
Create host window from RenderHostWindowRequirements
        |
        v
Load and negotiate the exact verified renderer module
        |
        v
Attach the selected backend's private presentation adapter
        |
        v
Register and seal the selected backend provider
        |
        v
Create RenderFrontend
        |
        v
Initialize matching editor GUI and viewport integration
```

The editor must not create an OpenGL, Metal, or Vulkan window before resolving
its backend selection and availability. Failure at any stage unwinds only
resources acquired by completed stages, in reverse order. Missing, incompatible,
or unhealthy components route to the HoroEditor Welcome/component-manager repair
surface rather than creating a fallback RenderApi graphics window.

## Required Interactive Lifecycle

Every interactive backend must obey the existing `IRenderBackend` lifecycle:

```text
Uninitialized
    -> Initialize
Ready
    -> Resize
    -> BeginFrame
FrameActive
    -> Execute zero or more validated plans
    -> Present or Abort
Ready
    -> Shutdown
Uninitialized
```

Required rules:

- `Initialize` is single-owner and rejects overlapping use of one presentation
  attachment.
- Failed initialization leaves no retained device, context, surface, layer,
  queue, or ownership lease.
- `Shutdown` is idempotent and safe after partial initialization.
- Backend destruction performs defensive shutdown without throwing.
- `Resize` rejects zero extents at the backend boundary; the host suspends frame
  submission while its drawable extent is zero.
- `Resize` during an active frame is rejected.
- `BeginFrame` issues a non-zero frame token unique within the backend instance.
- `Execute`, `Present`, and `Abort` reject stale or foreign frame tokens.
- `Present` completes the active frame exactly once.
- `Abort` returns the backend to a reusable ready state.
- Exceptions from native boundaries are translated to typed Horo errors.

OpenGL context behavior and Metal command-buffer behavior are implementation
details beneath these rules.

## Required Editor Rendering Lifecycle

The editor uses one backend-neutral ordering regardless of the selected API:

```text
Begin backend frame
Begin editor GUI frame
Compile and execute static-mesh viewport and primary-output passes
Encode editor GUI draw data
Present backend frame
```

The editor integration may coordinate private native state required to encode
GUI work, but it cannot expose that state to editor screens, panels, settings,
scene code, or the public Render API.

In particular:

- OpenGL global state is private to the OpenGL integration.
- Metal command buffers, render-pass descriptors, and encoders are private to
  the Metal integration.
- ImGui OpenGL and Metal backend calls are private to matching editor integration
  targets.
- Editor feature code consumes opaque editor image identities, not `GLuint` or
  `MTLTexture*`.
- The static-mesh executor obeys the same target-generation, extent, readiness,
  texture-view, and shutdown contract for every backend.

The integration layer adapts editor GUI work to the selected renderer; it does
not own scene rendering policy or replace `RenderFrontend`.

## Required Baseline Capability

OpenGL and Metal reach parity only when both provide:

- interactive window presentation;
- FIFO presentation mode;
- explicit immediate-mode support or a typed unsupported result;
- primary color output clear/store operations;
- resize and high-DPI drawable extent handling;
- zero-size suspension through the host contract;
- editor GUI rendering;
- offscreen color and depth targets for the editor viewport;
- generic immutable position/normal/UV mesh resources with `uint32_t` indexed
  triangle draws for every core procedural primitive;
- deterministic shutdown and initialization rollback;
- Debug and Release startup validation;
- deterministic GPU readback smoke coverage on supported CI hosts.

A backend may report additional optional capabilities. Optional capability
support does not grant different lifecycle rules or allow editor/runtime code to
special-case its identifier.

## Backend Availability

Built, installed, verified, host-supported, probed, selected, and initialized are
different states. The normative lifecycle is defined by
[Renderer Distribution And Availability](./renderer-distribution-and-availability.md).

Project Settings consumes component-registry snapshots. It may list a known but
not installed renderer and offer installation, but it must not claim runtime
support before verification and probe success. Selecting an unavailable backend
produces a typed install, repair, update, or startup result. The editor does not
silently substitute another backend.

## Parity Test Contract

The same behavioral suite must run against every interactive backend. Tests are
parameterized by backend module identity; backend-specific tests are additional,
not replacements for parity tests.

Required shared cases:

1. module metadata is valid and side-effect free;
2. window requirements are available before native initialization;
3. successful initialize, frame, execute, present, and shutdown;
4. initialization failure rollback;
5. overlapping presentation ownership rejection;
6. invalid and zero extent rejection;
7. resize-during-frame rejection;
8. stale, foreign, and reused frame-token rejection;
9. abort followed by successful frame reuse;
10. viewport initialize, resize, render, selected-instance styling, texture-view, and shutdown;
11. GUI frame encoding and presentation;
12. one-frame editor startup through `--renderer <id>`;
13. GPU smoke output satisfying the same image-level acceptance thresholds;
14. package/module ABI negotiation through the same host adapter contract;
15. startup rejection before window creation when the selected component is not
    available.

Platform-specific tests may verify GL state restoration or Metal resource and
command-buffer lifetime, but those tests do not weaken the shared contract.

## Build Matrix

The intended matrix is:

| Host | OpenGL | Metal | Null |
|---|---:|---:|---:|
| macOS | Build + parity + GPU smoke | Build + parity + GPU smoke | Build + headless tests |
| Linux | Build + parity; GPU smoke on display-capable CI | Not compiled | Build + headless tests |
| Windows | Build + parity; GPU smoke on display-capable CI | Not compiled | Build + headless tests |

Metal being Apple-only is a host availability constraint, not a lower or higher
architectural rank.

Packaged-artifact lanes additionally verify independent component install,
signature/ABI rejection, probe behavior, and editor startup for every supported
installed subset.

## Project Settings Contract

Project Settings stores only the stable backend identifier and pending restart
state:

```text
render.backend = "opengl"
render.backend = "metal"
```

Settings code does not know SDL flags, native handles, ImGui renderer backends,
or concrete backend classes. A committed backend change:

1. resolves the identifier through the component catalog;
2. installs, repairs, updates, or probes the component when required;
3. persists only the stable backend identifier after the chosen workflow policy
   succeeds, or records an explicit unresolved pending request for offline use;
4. marks renderer restart as required;
5. leaves the active renderer untouched;
6. applies the new selection during the next startup sequence.

Machine-local module paths, trust state, install records, and probe results never
enter project settings.

## Transitional State

The current editor composition has OpenGL and Metal sibling GUI bridges and
static-mesh executors. Both consume the same public `RenderSceneView` and execute
from the frontend frame plan. Only ImGui texture resolution remains app-private;
the public renderer API intentionally exposes no native or GUI texture type.

Migration must not preserve either backend as a privileged composition path.
The common module metadata, window requirements, editor lifecycle, generic mesh
snapshot, target handles, submission contract, and parity test harness are shared.

The current in-process static registration path is also transitional for the
installed product. Development and test hosts may retain explicit static module
registration, but product composition loads only exact verified component paths
and adapts the negotiated module ABI into the in-process registry.

## Acceptance Gate

The switchable editor renderer foundation is complete only when:

```text
OpenGL shared parity suite: PASS
Metal shared parity suite:  PASS
OpenGL GPU smoke:           PASS
Metal GPU smoke:            PASS
--renderer opengl:          PASS
--renderer metal:           PASS
No native public leakage:   PASS
No concrete cross-linking:  PASS
Independent install/remove: PASS
Manifest/signature/ABI:     PASS
Probe/crash isolation:      PASS
No-renderer repair routing: PASS
```

Until this gate is met, backend selection remains an engineering/debug control
and is not exposed as a completed Project Settings feature.
