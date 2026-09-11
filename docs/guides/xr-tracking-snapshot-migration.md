# XR Tracking Snapshot Migration

## Purpose

Migrate XR tracking producers and consumers from native device paths, mutable pose
maps, or last-known-pose caches to `Horo::XR::XRTrackingSnapshot`.

## Producer Workflow

1. Retain the exact active `XRSessionId`, `XRWorldOriginRevision`, and a monotonic
   `XRTrackingSnapshotRevision`. Device identifiers are session-owned live values;
   never serialize them or derive them from a native runtime path.
2. Normalize backend poses into validated simulation-purpose `XRPoseSample` values.
   Their runtime sample timestamp must equal the snapshot timestamp.
3. Publish device records in strict `XRDeviceId` order and poses in strict
   `(XRDeviceId, XRTrackedPoseKind)` order. Advertise only the closed Horo capability
   set appropriate for each semantic role.
4. Choose admitted limits within `XRTrackingSnapshotHardLimits`, then call
   `XRTrackingSnapshot::Create`. Overflow rejects the whole candidate; it is never
   partially published or silently truncated.
5. On tracking loss, remove invalid pose records. Permission/session loss publishes
   no device or pose evidence. Never substitute identity transforms or retain a prior
   valid pose as current evidence.

## Consumer Workflow

Call `ValidateXRTrackingSnapshot` before retaining a publication across a lifecycle
boundary. Use `QueryXRTrackedPose` with the exact device, snapshot revision, session,
and origin revision. The query is bounded and allocation-free; copy the returned
`XRPoseSample` only into state whose authority and lifetime are explicit.

Typed outcomes must remain distinct:

- `xr.operation.unsupported`: the selected device does not advertise that pose kind.
- `xr.operation.unavailable`: the known device or pose is temporarily absent.
- `xr.identity.stale`: the session or device generation was replaced.
- `xr.tracking_snapshot.stale`: the retained publication revision was replaced.
- `xr.origin_revision.stale`: the world-origin adapter changed.
- `xr.operation.capacity_exceeded`: the complete publication exceeds admitted bounds.
- `xr.tracking_snapshot.invalid`: evidence is malformed, contradictory, or non-canonical.

## Validation Record

`HoroXRApiTests` covers immutable ownership, exact confidence, allocation-free query,
canonical ordering, hard/admitted bounds, contradictory evidence, loss neutralization,
unsupported/unavailable distinctions, replacement, origin change, and shutdown.
`HoroXRApiPublicHeaderConsumer` verifies the owned installed header boundary.

## References

- [XR Architecture](../architecture/runtime/vr-ar-architecture.md)
- [XR Coordinate and Pose Migration](xr-coordinate-pose-migration.md)
- [ADR-159: XR Action, Tracking and Input Projection Ownership](../adr/159-xr-action-tracking-and-input-projection-ownership.md)
