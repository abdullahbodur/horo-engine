# XR View and External Render-Target Contract Migration

## Purpose

Migrate XR view producers and Renderer bridges from fixed stereo arrays or native
swapchain image values to `Horo::XR::XRViewRenderPlan`.

## Prerequisites

- Retain the exact active `XRSessionId`, `XRViewConfigurationId`,
  `XRViewConfigurationRevision`, and `XRWorldOriginRevision`.
- Normalize located view poses through the XR coordinate/time contract and translate
  native image formats and usages into Horo RenderApi values.
- Acquire the complete runtime image set before publishing a plan; native image
  handles remain in the private XROpenXR/Renderer bridge.

## Producer Workflow

1. Publish one `XRViewConfigurationDescriptor` for the complete runtime-required
   configuration. Do not truncate a greater-than-two configuration to stereo.
2. Emit `XRViewDescriptor` values in runtime order. Preserve semantic role and stable
   `XRViewId`; an order index is never eye identity. Every pose uses
   `PresentationPrediction` and the plan's exact predicted display time.
3. Emit one projection color binding for every view, followed by its optional depth
   binding. Use `XRSwapchainTargetId` and `XRSwapchainImageId` rather than a native
   handle or Renderer resource identity. Reuse the same identities when several views
   address distinct array layers of one acquired runtime image.
4. Declare the Horo texture format, usage, allocation extent, active render rectangle,
   array layer, and sample count. A depth role requires a depth format and explicit
   implementation admission.
5. Call `XRViewRenderPlan::Create` with the active owner fences, immutable system view
   limit, explicit implementation policy, and the bounded set containing each unique
   acquired image exactly once. Failure publishes no partial plan or target.

## Consumer Workflow

Before Renderer import or graph use, call `ValidateXRViewRenderPlan` with current
session, configuration, origin, and acquired-image evidence. Revalidate after any
session/configuration/image replacement or shutdown boundary. The operation is bounded
and allocation-free; it performs no native call or CPU/GPU synchronization.

Typed outcomes remain distinct:

- `xr.operation.unsupported`: the complete configuration or optional depth path is not
  implemented by the explicit admission policy.
- `xr.operation.capacity_exceeded`: the complete view set exceeds a hard, system, or
  captured implementation bound.
- `xr.identity.stale`: a session, configuration identity, target, or acquired image was
  replaced.
- `xr.view_configuration.stale`: the immutable configuration publication changed.
- `xr.operation.unavailable`: a required image is not in the current acquired set.
- `xr.view_plan.invalid` or `xr.external_target.invalid`: producer evidence is malformed
  or non-canonical and must not reach Renderer.

## Limitations

This contract does not acquire, wait, import, synchronize, release, or destroy native
images. XRA-004.3 owns that lifecycle. Primary stereo is the initial production profile;
the bounded public representation retains mono simulator and N-view evolution seams
without claiming quad-view product support.

## Validation Record

`HoroXRApiTests` covers mono, stereo, quad-shaped bounded evidence, exact first-slice
admission, unsupported no-truncation behavior, count/order/role/format/usage/extent
validation, allocation-free creation/revalidation, replacement, shutdown, stale origin,
and stale acquired-image generations. The generated XRApi public-header consumer proves
the RenderApi dependency and absence of native graphics/OpenXR types.

## References

- [XR Architecture](../architecture/runtime/vr-ar-architecture.md)
- [Rendering Architecture](../architecture/runtime/rendering-architecture.md)
- [ADR-160: XR Rendering, OpenXR Compositor and Renderer Ownership](../adr/160-xr-rendering-openxr-compositor-and-renderer-ownership.md)
