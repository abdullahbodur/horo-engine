# XR Coordinate and Pose Contract Migration

## Purpose

Migrate XR producers and consumers from native or loosely typed pose data to the
backend-neutral `Horo::XR::XRPoseSample` contract.

## Prerequisites

- Hold the current `XRSessionId` and `XRWorldOriginRevision` from the owning XR
  composition root.
- Convert backend coordinates to metres in Horo's right-handed, Y-up,
  negative-Z-forward convention before publishing them.

## Workflow

1. Describe source and target spaces with `XRCoordinateSpace`. Preserve the exact
   session identity and world-origin revision; never store a native space handle in
   this public value.
2. Construct runtime, simulation, and render-prediction timestamps through their
   distinct strong types. Publish exactly one authoritative purpose: simulation
   input or presentation prediction.
3. Put only finite evidence in `XRPoseComponents`. A tracked or inferred component
   has a value; an invalid component has no value. Remove linear velocity when
   position is lost and angular velocity when orientation is lost.
4. Call `XRPoseSample::Create`. Treat its typed stale, incompatible, time-domain,
   and invalid-pose results as terminal for that candidate; do not repair evidence
   with identity or last-known values.
5. Compose conversions with a caller-owned contiguous span passed to
   `ComposeXRSpaceTransforms`. Keep chains contiguous, on one session/origin/time
   snapshot, and at or below `XRCoordinateHardLimits::MaximumTransformHops`.

## Troubleshooting

- `xr.identity.stale` means a runtime or session was replaced. Resolve new spaces
  from the active owner.
- `xr.origin_revision.stale` means the world-origin adapter changed. Relocate the
  pose through the current adapter.
- `xr.time_domain.incompatible` means clock evidence is missing, mixed, or differs
  across a composition chain. Correlate clocks explicitly at the owning boundary.
- `xr.pose.invalid` means values disagree with validity, confidence, loss, or
  finite/unit requirements. Correct the producer; do not suppress the failure.

## Limitations

This contract does not own OpenXR handles, sample native runtime state, interpolate
poses, or select a prediction policy. Those responsibilities remain with the XR
backend and host composition boundaries.

## Validation Record

`HoroXRApiTests` covers valid publication, time-domain separation, tracking loss,
non-finite inputs, replacement, shutdown, stale origins, deterministic composition,
gaps, and bounded overflow. `HoroXRApiPublicHeaderConsumer` verifies the installed
header boundary.

## References

- [Coordinate Precision and Origin Rebasing](../architecture/runtime/coordinate-precision-and-origin-rebasing.md)
- [ADR-157: XR Ownership, Runtime Composition and Capability Tier](../adr/157-xr-ownership-runtime-composition-and-capability-tier.md)
- [ADR-159: XR Action, Tracking and Input Projection Ownership](../adr/159-xr-action-tracking-and-input-projection-ownership.md)
