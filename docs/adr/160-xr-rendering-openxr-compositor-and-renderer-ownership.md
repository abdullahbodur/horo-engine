# ADR-160: XR Rendering, OpenXR Compositor and Renderer Ownership

- **Status**: Proposed
- **Date**: 2026-09-02
- **Supersedes**: None
- **Scope**: Runtime-driven XR views, native swapchains, external image import, render targets, synchronization, frame/composition submission, bounded N-view admission, depth/motion inputs, dynamic resolution, foveation/VRS/density paths, runtime compositor behavior, lifecycle, migration and validation
- **Issue**: [XRA-004.1](https://github.com/abdullahbodur/horo-engine/issues/2138)
- **Jira**: [HORO-2092](https://horo-engine.atlassian.net/browse/HORO-2092)
- **Related**: [ADR-026](026-large-world-precision-and-floating-origin-strategy.md), [ADR-027](027-renderer-resource-identity-and-descriptors.md), [ADR-028](028-renderer-capability-limits-and-product-profiles.md), [ADR-033](033-presentation-and-display-ownership.md), [ADR-034](034-gpu-memory-and-residency-ownership.md), [ADR-037](037-scene-color-and-hdr-architecture.md), [ADR-040](040-reconstruction-frame-generation-and-latency-providers.md), [ADR-157](157-xr-ownership-runtime-composition-and-capability-tier.md), [ADR-158](158-openxr-loader-backend-packaging-and-host-composition.md), [ADR-159](159-xr-action-tracking-and-input-projection-ownership.md)
- **Normative documents**: [XR Architecture](../architecture/runtime/vr-ar-architecture.md), [Rendering Architecture](../architecture/runtime/rendering-architecture.md), [Render Backend Parity](../architecture/runtime/render-backend-parity-contract.md), [Post-Processing and Effects Architecture](../architecture/runtime/post-processing-and-effects-architecture.md)

## Context

ADR-157 assigns native OpenXR swapchains/composition to XROpenXR and scene/GPU work to
Renderer. The XR and rendering architecture documents already require runtime-driven
views, external resources and generation-safe acquire/release. They do not yet freeze
the complete handoff: who selects formats and view configurations, who calls native
acquire/wait/release, which owner decides a render target is safe, how composition layers
are formed, or where foveation, dynamic resolution, depth and motion belong.

Ambiguity here can create double ownership of images, normal-frame CPU/GPU waits,
fixed two-eye public arrays, native handles in frontend contracts or submission before
Renderer work completes. It could also turn optional foveation/space-warp features into
post-process switches, let Renderer own the XR session, or let XR code record general
scene rendering. Runtime asynchronous reprojection and guardian/chaperone visuals are
often adjacent to Horo rendering but remain compositor/platform behavior.

The repository has no production OpenXR renderer bridge. This decision fixes the
owners, bounded schemas, frame transaction, synchronization and capability boundaries
before XRA-004.2 and later implementation tickets specialize concrete view, swapchain,
layer and test contracts.

## Decision

### 1. XR owns runtime presentation; Renderer owns Horo rendering

The frame path is:

```text
OpenXR runtime
  view configurations, formats, predicted timing, native swapchain images
        |
        v
XROpenXR + XRRuntime
  session/frame state, accepted view set, image acquisition, composition intent
        |
        v
private XROpenXR/Renderer bridge
  native image/synchronization translation into typed external leases
        |
        v
RenderFrontend + selected render backend
  N-view extraction, graph resources/passes, GPU submission and retirement
        |
        v
XROpenXR
  release images, encode layers, end frame
        |
        v
OpenXR runtime compositor
  asynchronous reprojection, device composition and physical presentation
```

XRRuntime owns the engine-visible XR frame transaction and generation-safe Horo plans.
XROpenXR owns every native wait/begin/end-frame, view-location, swapchain, image acquire/
wait/release and composition-layer call. RenderFrontend owns backend-neutral scene/view
extraction, graph compilation, external-resource use and completion evidence. The
selected renderer backend owns native GPU resources, commands, queues, barriers and
deferred retirement.

The private bridge translates native images and synchronization for the exact selected
XROpenXR/renderer-backend tuple. Translation does not make it a second frame, image,
session or resource authority.

### 2. Every XR rendering responsibility has one owner

| Responsibility | Sole owner | Deliberate non-owner |
|---|---|---|
| Product-required XR profile, renderer/backend tuple and optional-feature policy | Application composition | Runtime discovery and post-processing |
| Runtime view-configuration/format/blend-mode discovery | XROpenXR | Renderer and Platform |
| Complete capability/admission plan across runtime and Renderer | XRRuntime/application coordinator | Native callbacks and feature passes |
| Horo view/configuration/frame/target identities and generations | XRRuntime/XRApi contract | Array indexes and native handles |
| Native swapchain creation, image enumeration and API-valid destruction | XROpenXR | Renderer resource registry |
| Native wait/begin/locate/acquire/wait/release/end-frame order | XROpenXR | RenderFrontend and Input |
| External image descriptor/lease translation | Private XR/renderer bridge | Public XRApi and general Platform |
| Horo external resource registration, graph use and GPU completion proof | RenderFrontend/selected backend | XROpenXR session state |
| Scene extraction, per-view culling, passes, shading, post-processing and UI pixels | Renderer and owning feature adapters | OpenXR runtime |
| Backend-neutral composition intent and admitted layers | XRRuntime/application feature owners | Renderer backend native code |
| Native layer encoding/submission | XROpenXR | Runtime UI and post-processing |
| Asynchronous reprojection/timewarp, guardian/chaperone and device display composition | OpenXR runtime/platform | Horo Renderer and XRRuntime |
| Window/display mirror output | ADR-033 host/Renderer surface owners | XR swapchain and compositor |
| Diagnostics/qualification evidence and support claim | Observability/release owners | Successful frame submission alone |

Renderer never calls `xrEndFrame`, releases a native swapchain image, selects a reference
space or changes session state. XROpenXR never records general scene draws, owns material/
post-process histories or destroys Renderer resources still in flight.

### 3. The public contract is bounded N-view, not a two-eye array

Discovery publishes finite view-configuration descriptors with stable configuration
identity, type/capability identity, runtime order, view count, per-view recommended/max
extent and sample bounds, supported blend modes and current runtime/system revisions.
The admitted renderer plan additionally records the configured Horo maximum view count.

One `XRViewSetPlan` contains a bounded ordered span of descriptors. Each view has a
stable `XRViewId`, semantic role, source/reference-space generation, pose/projection,
render rectangle, target subresource and per-view capability data. Runtime ordering is
preserved but an index is never eye identity. Public contracts cannot expose two-element
arrays, `left/right` booleans or fixed matrix pairs.

The first production `XRProjection1_0` implementation admits exactly one primary opaque
stereo configuration with exactly two runtime views. One-view data may be used only by
an explicit simulator/test profile; it does not satisfy `XRProjection1_0`. Quad-view,
foveated-inset or any other greater-than-two configuration is reported as discovered but
unsupported until its full Renderer path and product profile are implemented/qualified.
Even in this two-view profile, first-party bridge storage, validation and graph
construction iterate the bounded N-view descriptor set and map by `XRViewId`; no internal
two-element array or left/right branch is permitted.

View count, ordering, extents and renderer limits are validated before swapchain/image
acquisition. An unsupported count returns a typed preflight result and creates no
truncated plan, partial targets or two-eye fallback. A later N-view implementation can
admit the same descriptors without changing public identity or ownership.

### 4. Swapchain and external resource identities are distinct

XROpenXR owns native swapchains and their image arrays under exact instance/system/
session/configuration generations. XRRuntime exposes Horo `XRSwapchainTargetId` and
`XRSwapchainImageId` values that carry owner and generation but reveal no native handle.
Renderer imports an acquired image as its own generation-safe external resource lease;
the imported `RenderResourceId` is correlated with, but not interchangeable with, the
XR image identity.

The external descriptor records at least:

- XR frame/session/configuration/swapchain/image generations;
- view/array-slice/subresource mapping and color/depth/motion/density role;
- native-resolved format translated to a Horo typed format;
- physical allocation extent, active render rectangle and sample count;
- declared read/write/attachment/copy/shading-rate usages;
- required initial/final external state and queue-family/ownership semantics abstracted
  as backend-neutral synchronization contracts;
- acquire token, completion/retirement requirement and release owner; and
- color representation, alpha/blend and composition requirements.

Format matching validates the complete format/usage/sample/layout tuple against both
runtime enumeration and Renderer effective capabilities before native swapchain
creation. A format numeric value, image pointer, array index or matching extent cannot
stand in for the typed identity.

Renderer external registration borrows the native allocation for the lease duration. It
does not adopt or destroy it. XROpenXR cannot release/reuse the image until Renderer
returns completion evidence for every queue/reference covered by the lease.
That evidence prefers backend-neutral GPU synchronization primitives that the selected
Renderer/OpenXR bridge can hand to the compositor asynchronously. CPU fences or waits
are used only when the admitted backend/runtime contract has no qualified GPU handoff;
ordinary submission never requires the CPU to wait for GPU completion.

### 5. One XR frame scope owns the cross-system transaction

The admitted frame protocol is:

```text
Idle
  -> Waited
  -> Begun
  -> ViewsLocated
  -> ImagesAcquiredAndReady
  -> RendererSubmitted
  -> ImagesReleased
  -> LayersSubmitted
  -> Idle
```

Every transition carries one `XRFrameGeneration`, predicted display time, accepted
configuration and capability-plan generation. A frame that the runtime marks as
non-rendering still follows its legal begin/end contract with zero admitted Horo render
work and an explicit layer result.

XROpenXR performs native wait/begin, locates the admitted views at predicted time and
acquires/waits required images. The bridge opens external leases. XRRuntime publishes
one immutable render plan. Renderer performs N-view extraction and graph execution,
then returns a typed submission/completion token. The bridge closes Renderer use;
XROpenXR releases images only when the native/Renderer synchronization contract permits,
forms admitted native layers and ends the frame.

An image-ready wait is the bounded native acquire contract, not permission for a normal-
frame device-idle or queue-idle wait. Renderer uses semaphores/fences/events and deferred
retirement appropriate to the selected backend. CPU blocking, if an unavoidable native
API step, is bounded, timed, cancellable where supported and measured separately from
GPU completion.

Failures after begin resolve the frame through a legal abort/zero-layer/end path when the
runtime permits it. An acquired image is released exactly once. No exception, cancelled
job or skipped render leaves an open image or frame; no fabricated success advances
temporal history.

### 6. Renderer plans views, passes and histories from immutable XR input

Renderer receives a complete `XRRenderPlan`, never an incremental callback sequence. It
contains the accepted view set, predicted pose/projection snapshot, external targets,
scene-presentation epoch, render scale/rect, optional depth/motion/foveation inputs,
layer intent and synchronization requirements.

All views of one XR scene-presentation epoch consume one committed simulation/extraction
state. Per-view culling, LOD, lighting, effects, UI projection, histories and output
conversion remain Renderer/feature responsibilities. Rendering the second or Nth view
does not advance simulation, animation, VFX, audio or input again.

Histories are keyed by XR session/configuration/view, Renderer device/plan, projection,
extent/render rectangle, origin, color/exposure and relevant feature generations. View
reorder/count/role, reference-space reset, session replacement, render-scale change,
foveation-plan change or incompatible target replacement resets/migrates only through a
declared history policy. An array index never preserves history identity.

The Renderer may implement admitted stereo as multipass or instanced/multiview. That is
an effective backend capability/performance decision. It cannot change view semantics,
omit a view, combine histories or claim support for a view configuration the frontend
rejected.

### 7. Composition layers are XR intent, not arbitrary Renderer submission

Feature owners contribute finite backend-neutral layer candidates through XRRuntime.
The application/XR capability plan admits layer kinds, ordering bands, blend semantics,
spaces, view coverage and budgets before the frame. Projection content references
completed XR external targets; Runtime UI may contribute an admitted quad/cylinder/etc.
intent only through its own typed adapter. Native handles are absent.

XRRuntime validates one immutable composition plan for the current frame generation.
XROpenXR encodes that plan into native structures and calls the runtime. It cannot reorder
semantic bands, add vendor layers, retain frame-local pointers or reinterpret a missing
required layer as success. Renderer produces pixels and completion evidence; it does not
call the compositor directly.

Layer count/capacity, space/session generation, pose validity, blend mode, alpha/color
representation, image rectangle and target completion are validated before submission.
Optional-layer rejection follows the captured fallback plan. Required projection-layer
failure ends the frame with a typed failure; it never presents a partial view set as a
successful XR frame.

### 8. Depth, motion and auxiliary targets have explicit semantics

The external-target model admits roles for projection color, runtime-submitted depth,
motion/vector data, shading-rate/density inputs and future qualified auxiliary layers.
Each role declares format, units, coordinate convention, direction, valid range,
background/invalid encoding, sample/resolve policy, render rectangle and consumer.

Depth submission is an optional 1.0 capability only when runtime and Renderer agree on
the exact depth contract. Renderer produces the declared per-view depth; XROpenXR encodes
native composition depth metadata. Scene depth used internally by lighting/post effects
does not automatically satisfy the runtime contract.

Motion/depth/timing for runtime space warp is post-1.0 until an explicit capability plan
defines generation, units, disocclusion and submission. Renderer may produce inputs but
does not own runtime space-warp activation or completion. Missing auxiliary input cannot
be replaced by zeros or ordinary motion blur data while reporting the feature active.

Auxiliary targets remain bounded and generation-correlated. They never grant same-frame
CPU readback, gameplay visibility authority or permission to expose native resources.

### 9. Dynamic resolution and foveation are joint XR/Renderer capabilities

The application resolves one immutable XR render-quality plan from runtime limits,
Renderer capabilities, product policy, privacy state and measured budget. It separates:

- swapchain allocation extent and sample count;
- active per-view render rectangle/scale;
- dynamic-resolution controller and finite scale/rate bounds;
- fixed foveation level/provider;
- gaze-driven foveation with current consent/tracking validity;
- Renderer VRS versus runtime-provided density-map mechanism; and
- fallback plus history-invalidation policy.

Dynamic resolution changes only at the declared frame-plan safe point and stays inside
runtime/Renderer bounds. It does not recreate native swapchains per frame or relabel a
smaller render rectangle as a different physical extent. The current plan/revision is
captured by every frame and temporal history.

Foveation is not a generic post-process. XROpenXR owns runtime extension calls and
runtime-provided density resources; Renderer owns compatible shading-rate/density use
and graph binding. Gaze-driven foveation additionally depends on ADR-159 privacy and a
valid current gaze sample. Loss follows only an admitted fixed/non-foveated fallback;
the system never retains stale gaze or reports foveation active after capability loss.

Backend absence of VRS/multiview is not automatically XR failure if the accepted profile
has a qualified multipass/non-foveated path. Required product policy fails preflight
instead of silently degrading.

### 10. Runtime compositor and Horo post-processing are different owners

Horo post-processing runs inside each admitted Renderer view before the projection
target reaches its declared external final state. It owns effects, exposure, color and
histories under the rendering architecture. It does not discover XR, select view
configuration, acquire/release images, enable foveation or submit composition layers.

Asynchronous reprojection/timewarp, distortion, device scan-out and platform guardian/
chaperone/boundary composition remain native runtime/platform behavior. Horo does not
model them as render-graph passes, post-process effects, generated frames or simulation
ticks. Runtime behavior may be observed through bounded timing/capability evidence but
does not transfer ownership.

A desktop mirror is a separate ADR-033 output surface and frame plan. It may consume a
qualified copy/resolution of Horo scene output without borrowing an acquired XR image
beyond its lease or delaying XR release. Mirror failure cannot corrupt the XR session;
XR failure cannot be reported as successful because the mirror presented.

### 11. Loss, replacement and shutdown are generation-fenced

View-configuration, format, blend-mode, capability, render-scale, reference-space,
Renderer device, swapchain/image, session and instance changes advance their relevant
generations. New frames close admission against the old plan. Outstanding Renderer work
and image leases retire before XROpenXR destroys or reuses native objects.

Replacement prepares a complete compatible candidate plan/swapchain/resource registry
before atomic publication. Failure preserves the prior generation only if the runtime
still declares it valid; otherwise the XR output becomes Lost. Matching native handles,
formats or view counts never revalidate old Horo identities.

Shutdown stops new XR frames/layers, resolves any begun frame legally, joins Renderer
submissions, closes external leases, releases acquired images, retires Renderer external
registrations, destroys native swapchains/session resources and finally releases bridge/
backend dependencies. It never uses an unconditional device-idle wait as routine
teardown when generation/fence retirement can prove safety.

### 12. Migration and contract coverage are required

There is no production XR rendering implementation to preserve. Initial work must add
typed XR/Renderer bridge contracts and external resources without leaking native types
or adding fixed-eye arrays. Existing stereo examples are profile examples, not license
to narrow the public contract.

Required automated coverage includes:

- dependency/public-header tests proving native OpenXR/graphics handles remain private;
- runtime-driven descriptor bounds/order/role/generation validation;
- first-party bridge N-view iteration/mapping tests even under the two-view 1.0 profile;
- exact primary-stereo admission plus one-view test-profile handling and greater-than-two
  rejection before image acquisition with no truncation/partial targets;
- format/usage/sample/layout/color-role negotiation across every selected renderer;
- swapchain versus RenderResource identity, stale/wrong-owner rejection and external
  lease lifetime;
- wait/begin/locate/acquire/wait/render/complete/release/end ordering, fault injection at
  every boundary and exactly-once image/frame cleanup;
- multipass and multiview semantic parity without repeated simulation/extraction;
- layer ordering/bounds/space/pose/blend/completion validation and required/optional
  failure behavior;
- depth, motion, density/VRS and foveation schemas, privacy loss, fallback and history
  invalidation;
- dynamic-resolution scale/rectangle changes without per-frame swapchain recreation;
- no post-processing ownership of foveation/reprojection/composition and no Horo
  guardian/chaperone pass; and
- session/device/swapchain replacement and shutdown with no GPU/native/resource/frame/
  layer/bridge lease surviving its owner.

Deterministic fake runtime/renderer combinations validate ordering, bounds and faults.
Physical device/runtime/driver evidence remains required for each supported tuple and
for timing, visual parity, foveation, depth or motion claims.

## Consequences

### Positive

- Native swapchains/composition and Horo rendering have one explicit lease-based handoff.
- Public contracts support bounded N-view evolution while first production scope remains
  exact and testable.
- Unsupported view configurations fail before acquisition instead of truncating views.
- External descriptors anticipate depth, motion, dynamic resolution and foveation
  without exposing native types.
- Runtime reprojection/guardian behavior and Horo post-processing remain separate.

### Negative

- Every renderer backend needs a private OpenXR bridge and external synchronization
  qualification.
- Swapchain, XR image and Renderer resource identities require explicit correlation and
  lifecycle tracking.
- Multipass/multiview, dynamic-resolution and foveation paths expand the renderer test
  matrix even when the initial product admits only stereo.
- Optional depth/motion/foveation features cannot be enabled until complete semantics and
  fallback evidence exist.

## Rejected Alternatives

### Let Renderer own the OpenXR session and swapchains

Rejected because Renderer owns GPU work, not runtime events, reference spaces, native
session state, actions or composition submission.

### Let XROpenXR record scene rendering directly

Rejected because it would duplicate RenderFrontend graph/resource/material/history
ownership and make one graphics backend privileged.

### Publish fixed left/right arrays

Rejected because runtime view configurations are variable and semantic view identity is
not an array index. Fixed arrays force a public migration for quad/foveated views.

### Truncate unsupported view configurations to stereo

Rejected because omitted views change the runtime's required composition and can produce
invalid or unsafe presentation. Admission fails before acquisition.

### Treat native images as ordinary Renderer-owned textures

Rejected because the runtime retains allocation/reuse/destruction authority and requires
exact acquire/release order. Renderer receives a bounded external lease.

### Release images immediately after CPU command recording

Rejected because command recording is not GPU completion. Release follows the selected
native/Renderer synchronization proof without normal-frame global idle waits.

### Implement foveation, space warp or reprojection as generic post-processing

Rejected because they require XR/runtime/Renderer capability and submission contracts;
asynchronous reprojection and device composition remain native runtime behavior.

### Treat mirror presentation as XR presentation success

Rejected because the mirror and XR compositor are separate output owners, clocks,
surfaces and failure domains.
