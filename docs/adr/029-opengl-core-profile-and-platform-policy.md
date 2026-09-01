# ADR-029: OpenGL Core Profile and Platform Policy

- **Status**: Proposed
- **Date**: 2026-08-31
- **Supersedes**: None
- **Scope**: OpenGL native admission, platform qualification and context negotiation
- **Jira**: [HORO-306](https://horo-engine.atlassian.net/browse/HORO-306)
- **Issue**: [#306](https://github.com/abdullahbodur/horo-engine/issues/306) ([RND-004.1])
- **Normative document**: [Rendering Architecture](../architecture/runtime/rendering-architecture.md)

## Context

Horo has an `opengl` backend and matching editor adapters, but a compiled module
does not establish which contexts, drivers, or platforms the product supports.
The private `OpenGLContextDescriptor` currently defaults to OpenGL 4.1 Core.
Without a normative policy, downstream device, surface, shader, and qualification
work could independently lower that requirement or infer features from it.

“Compatibility renderer” describes OpenGL's product role: maintaining a desktop
rendering option alongside the other native backends. It does **not** mean the
OpenGL Compatibility Profile. The document filename is
`029-opengl-core-profile-and-platform-policy.md` for that reason. OpenGL remains
an equal backend under the
[parity contract](../architecture/runtime/render-backend-parity-contract.md).

[ADR-028](028-renderer-capability-limits-and-product-profiles.md) already separates
native reports, implemented operations, effective support, and product profiles.
Renderer resource identity is
[ADR-027](027-renderer-resource-identity-and-descriptors.md); that is a different
decision from [ADR-008](008-error-model-exception-boundary-and-registry.md)
(error model). This decision supplies OpenGL's native admission policy; it does
not replace those contracts or assign a product profile to an API version.

## Decision

### 1. Desktop OpenGL 4.1 Core is the minimum

The `opengl` component requests **desktop OpenGL 4.1 Core**. A context is
admissible only when its actual API family is desktop OpenGL, its actual version
is at least 4.1, and its actual profile is Core. A newer Core context may satisfy
the minimum, subject to the same implementation, driver, and release
qualification checks. A newer version alone does not qualify a driver or enable
an optional path.

The following are not admitted under this component's identity:

- OpenGL versions below 4.1, including a 3.3 Core retry;
- legacy or Compatibility Profile contexts;
- OpenGL ES, WebGL, or an implicitly substituted translation backend;
- a context whose required entry points cannot be loaded or whose mandatory
  interactive parity requirements cannot be satisfied.

There is no fixed-function fallback. Meshes, shader stages, resources, and
commands still enter through backend-neutral Render API contracts. Core profile
semantics are defined by the
[Khronos OpenGL 4.1 Core specification](https://registry.khronos.org/OpenGL/specs/gl/glspec41.core.pdf).

The baseline shader variant must be valid for the admitted 4.1 Core contract.
The shader/cook pipeline records each variant's language and feature
requirements; a higher shader requirement needs a separately admitted variant
and a declared fallback when optional. A 4.1 Core context may run GLSL 150 only
when that source uses Core-legal 3.2 constructs and compiles and links against
the admitted context. Existing editor GLSL `150` shaders do **not** lower the
context minimum and are **not** grandfathered as admitted variants. Whether those
current editor shaders satisfy 4.1 Core is RND-004.6 evidence: if they use
removed Compatibility-profile features they fail admission and must be recooked
or replaced. This ADR does not introduce a new shader compiler, asset schema, or
public setting for native context versions.

Compute, storage resources, bindless access, and other optional operations are
not inferred from `opengl`, a version string, an extension string alone, or a
`Baseline`/`High` product preference. They require the native prerequisites,
loaded entry points, implemented path, and effective admission from ADR-028.

### 2. Platform scope and qualification

The supported platform families for this component are desktop Windows, Linux,
and macOS. Support for an individual release is conditional on its published
qualification matrix; this ADR does not certify every OS, GPU, or driver in
those families.

| Platform family | Native policy | Qualification boundary |
|---|---|---|
| Windows desktop | Desktop OpenGL 4.1 Core or later through the installed driver | Qualify each shipped OS/architecture and driver family; a system loader or successful build alone is insufficient. |
| Linux desktop | Desktop OpenGL 4.1 Core or later through the installed driver and selected window system | X11 and Wayland are separate qualification entries; success on one does not establish support on the other. |
| macOS desktop | System OpenGL 4.1 Core where available and qualified | Retained as a compatibility option with a platform deprecation warning; no promise that future macOS versions retain it. |
| Mobile, browser, and other hosts | Not supported by this component policy | OpenGL ES/WebGL or other providers require their own explicit architecture, package, and qualification decision. |

The current build reference environments are listed in
[Developer Environment](../architecture/delivery/developer-environment.md).
Those development baselines are inputs to qualification, not a substitute for
runtime support evidence. RND-004.8 owns the versioned release matrix, including
OS version, architecture, window system, GPU/driver identity, component build,
actual context/profile, and parity/smoke results. An unqualified combination may
be used by an explicit engineering/test host, but cannot be advertised as a
qualified product configuration.

Software GL implementations may be used in explicitly identified test lanes.
Their results do not stand in for hardware GPU qualification or establish
interactive performance support. `RenderNull` remains the ordinary headless
test/tool backend; it is not an interactive recovery renderer.

The component packages Horo's adapter and approved private dependencies. It
does not download arbitrary driver libraries or treat installation of the
component as installation of a GPU driver. Native loader availability, context
admission, and effective feature support remain separate checks under
[Renderer Distribution And Availability](../architecture/runtime/renderer-distribution-and-availability.md).

### 3. Context negotiation has one owner

Rendering owns the native requirements; the selected platform presentation
adapter realizes them. The host owns selection, window lifetime, and startup
failure handling. SDL, GLAD, native contexts, and profile constants stay private
to their backend or platform-adapter targets.

Startup obeys the common component lifecycle with these OpenGL obligations:

1. Resolve and verify the selected component and its immutable pre-window
   description. A successful availability probe is provisional, not a retained
   context or a final capability snapshot.
2. Before creating the OpenGL window, the selected host adapter applies the
   required context version/profile and presentation attributes. The adapter
   receives private, immutable setup requirements from the verified component's
   pre-window metadata; it must not create a second version-policy default.
   Extend that private metadata/adapter seam during realization without adding
   native fields to public Render API or project settings.
3. Create the window and context, make the context current on the designated
   native owner thread, load the required entry points, and query the actual
   version, profile, limits, and supported operations. Do not treat requested
   attributes or the editor's loader initialization as device evidence.
4. Validate native admission and construct the implemented/effective support
   snapshot under ADR-028. Only then publish a ready backend. Failure unwinds
   the context, window, and ownership lease according to their owners, in
   reverse acquisition order; no partial snapshot becomes active.

SDL requires GL attributes to be set before window creation, allows returned
attributes to differ from requested ones, and restricts attribute setup to the
main thread. The SDL host therefore performs this setup on its main thread;
its declared render-capable ownership and native create/destroy rules must also
be satisfied. Attribute configuration and window/context creation are serialized
so two adapters cannot interleave SDL's shared setup state.
[SDL's attribute contract](https://wiki.libsdl.org/SDL3/SDL_GL_SetAttribute)
is authoritative for this adapter requirement.

Native context work is not dispatched to arbitrary worker jobs. Rendering and
native teardown stay on the host-declared owner thread. Context recreation
invalidates the old device/snapshot identity and resources under
[ADR-027](027-renderer-resource-identity-and-descriptors.md) and ADR-028; a cached
probe cannot revive them. Native threading does not weaken the non-blocking
core-loop rules in [ADR-010](010-job-waiting-and-operation-store-ownership.md).

Debug contexts are diagnostic aids, not baseline availability requirements.
Normal validation-enabled startup may attempt a debug context and, if that
attempt fails, retry once without the debug flag after complete rollback of
that attempt's resources. Both attempts require the same version, Core profile,
and presentation contract. Report the loss of native debug diagnostics once;
Horo's own validation remains enabled. A qualification test explicitly requiring
a debug context fails instead of retrying. If the non-debug retry also fails,
context negotiation ends: unwind per step 4 and return the typed
startup/availability failure from the non-debug attempt, retaining the
debug-attempt failure as a diagnostic cause. There is no third attempt, no retry
that lowers the API/profile, drops required rendering features, or substitutes
another backend.

### 4. Selection and deprecation are explicit

Selection retains the precedence in
[Rendering Architecture](../architecture/runtime/rendering-architecture.md#backend-selection):
explicit CLI request, configuration, then host default. The project persists
`opengl`, not an OS context handle, GL version, driver name, or module path.
The **transitional host default** is the current interactive-editor default of
`opengl` because that is the implemented editor migration path; headless tools
and CI default to `null`. Rendering Architecture names that editor bootstrap
explicitly transitional. This decision does not change that default or the
restart requirement for renderer changes.

Apple deprecated OpenGL in macOS 10.14 and recommends Metal for new development;
Apple also documents its
[4.1 Core profile](https://developer.apple.com/documentation/appkit/nsopenglprofileversion4_1core).
See Apple's
[OpenGL migration guidance](https://developer.apple.com/library/archive/documentation/GraphicsImaging/Conceptual/OpenGL-MacProgGuide/UpdatinganApplicationtoSupportOpenGL3/UpdatinganApplicationtoSupportOpenGL3.html).
Horo retains qualified macOS OpenGL use without hiding that platform risk.

When OpenGL is selected on macOS, the host presents one localized warning per
startup, through its existing diagnostics/recovery surface, explaining that
the platform API is deprecated. It may recommend Metal and offer the existing
explicit install/probe/switch workflow. It must not claim Metal is available
without component verification and probe success, change project settings,
restart, or silently choose Metal merely because the warning was shown.
Headless diagnostics do not require creating an OpenGL window to show it.

Deprecation is distinct from failure. An admitted macOS context may continue;
a missing runtime, unsupported profile, failed context creation, or required
capability failure returns the appropriate typed startup/availability result.
Diagnostics include the requested backend, required contract, actual context
facts when available, failure stage, and actionable driver/component guidance.
No context is manufactured solely to fill an error message.

Any user-authorized fallback follows the common explicit fallback policy and
is reported. An interactive host cannot recover by selecting `null`. macOS API
deprecation does not deprecate Horo's Windows/Linux component. Removing macOS
support, raising the minimum version, or changing host defaults requires a new
architecture decision, migration guidance, and release notice; no removal date
is implied here.

### 5. M0 boundary and downstream migration

This is an architecture decision, not a claim that native qualification or the
complete backend is implemented. Current source defaults align with 4.1 Core,
but the following gaps remain explicit implementation work:

| Owner | Required realization |
|---|---|
| RND-004.2 / #308 — Device, Context and Capability | Validate actual version/profile and required entry points in backend initialization; produce reported and effective facts, typed failures, diagnostic retry handling, and generation invalidation. |
| RND-004.5 / #311 — Presentation and Surface Lifecycle | Move complete context requirements into the private pre-window setup seam; serialize SDL setup, verify attribute results, and prove rollback/recreation ownership. |
| RND-004.6 / #310 — Shader, Pipeline and Material | Admit cooked shader variants against the 4.1 Core baseline and optional effective paths without leaking GLSL policy to materials. |
| RND-004.7 / #312 — Editor GUI and Viewport | Consume the selected backend's readiness; remove reliance on editor-only GL loading for runtime readiness; integrate the macOS warning through the common host workflow. |
| RND-004.8 / #313 — Compatibility and GPU Qualification | Publish the release matrix and evidence, including actual contexts, platform diagnostics, driver restrictions, and negative admission tests. |

Resource/memory realization (RND-004.3) follows
[ADR-034](034-gpu-memory-and-residency-ownership.md) for charges, heaps/pools and
retirement; it does not invent a second OpenGL budget. Command/synchronization
adaptation (RND-004.4) consumes this policy and ADR-027/028; they do not choose
another native minimum. Other renderer APIs, product profile recipes, package
trust, driver rule governance, and native shader implementation retain their
existing owners.

Linux X11 and Wayland remain separate qualification entries. Those rows live in
the shared
[Renderer Distribution And Availability](../architecture/runtime/renderer-distribution-and-availability.md)
matrix together with the Vulkan Linux lanes from
[ADR-031](031-vulkan-loader-platform-and-version-baseline.md); each backend adds
columns, not a second independent Linux matrix.

The current SDL adapter sets version/profile during `CreateContext` after the
host has created its window. The current backend forwards requested options
without completing actual-context qualification, while editor integrations
perform GLAD loading. The migration above must close these gaps before treating
the component as satisfying this policy. M0 does not silently label the current
backend production-qualified or add runtime code to simulate completion.

### 6. Verification obligations

Downstream implementation is complete only with evidence for:

- admission of a valid 4.1 Core context and a qualified newer Core context;
  rejection of a lower version, Compatibility Profile, ES context, missing
  entry point, and missing required effective capability before readiness;
- a native extension reported but not implemented remaining unavailable, and
  required shader variants failing instead of silently lowering requirements;
- pre-window attribute ordering, actual/requested mismatch, debug retry bounds,
  strict debug-test rejection, rollback after each failed stage, repeated
  shutdown, and context recreation rejecting old resources/snapshots;
- explicit `--renderer opengl`, configuration precedence, unavailable component
  recovery, macOS warning without implicit switch, and unchanged project state
  when the user cancels an offered migration;
- the shared parity and GPU smoke suites on each published platform/driver
  combination, identifying software-rendered lanes separately.

Fake presentation/query ports cover deterministic negative paths; native GPU
lanes establish real context and image behavior. Neither replaces the other.
Docs-only M0 verification checks the decision's links and agreement with the
owning architecture; it is not GPU coverage evidence.

## Consequences

One desktop native minimum keeps platform negotiation and shader baseline work
consistent, including the retained macOS path. Older GL hardware and legacy
contexts are deliberately excluded. Core-only support avoids a second
fixed-function implementation and keeps backend-neutral rendering contracts
intact, at the cost of requiring a qualifying driver.

Higher-capability drivers can enable separately implemented optional paths
without redefining the baseline. This requires honest effective support and
per-platform qualification rather than treating a version number as readiness.
macOS compatibility incurs warning, testing, and eventual migration costs;
retention today is not a guarantee about future Apple platform availability.

## Rejected Alternatives

- **OpenGL 4.6 everywhere:** would exclude the retained macOS 4.1 path and raise
  the desktop requirement without an M0 need. Optional features belong to
  effective capability admission.
- **Lower the baseline to 3.3 or retry legacy/Compatibility contexts:** expands
  the support and shader matrix and makes startup requirements ambiguous;
  it is not justified by the current 4.1 Core implementation contract.
- **Automatically switch macOS projects to Metal:** changes project intent and
  may select a missing or unqualified component. Offer an explicit migration.
- **Remove macOS OpenGL immediately:** discards an existing compatibility path
  without a product migration decision or release notice.
- **Treat OpenGL ES/WebGL or translation layers as the same component:** hides
  different API, surface, deployment, and qualification requirements behind one
  support claim.
- **Let each host infer a version or hardcode feature tiers:** creates divergent
  policy and bypasses ADR-028's effective support and product profile separation.
