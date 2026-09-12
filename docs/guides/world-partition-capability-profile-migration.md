# World Partition Capability Profile Migration

## Purpose

Adopt the typed WST-001.9 boundary for world-partition project settings. The
contract replaces scattered grid/package defaults and limit clamping with one
immutable result tied to exact project-settings and host-capability revisions.

## Prerequisites

- Produce project-owned settings without probing a provider or filesystem.
- Obtain a complete `WorldPartitionCapabilitySnapshot` from the application
  composition root for the selected product artifact.
- Keep revisions non-zero and advance them whenever settings or capabilities change.

## Workflow

1. Select one exact `WorldPartitionProjectProfile`; profiles are identities, not an
   ordered quality preference.
2. Fill `WorldPartitionProjectSettingsRequest` with canonical millimeter grid size,
   LOD and capacity ceilings, `SignedMillimeter64` precision, and one package mode.
3. Call `WorldPartitionProjectSettings::Create(request, capabilities)` before
   publishing a world or submitting dependent work.
4. Retain the returned value with the operation that consumes it. Before new work,
   call `ValidateWorldPartitionSettingsAdmission` with current settings/capability
   revisions and lifecycle evidence.
5. On replacement, resolve a new immutable value. Do not rewrite an existing value
   or reinterpret stale work against the replacement.

Packaged standalone, client, and server profiles require archive chunks. Editor
workflows may explicitly select standalone cell files or archive chunks. Unsupported
modes fail; the contract never silently changes the requested representation.

## Troubleshooting

- `partition_settings.invalid`: repair unknown enum values, zero revisions, or
  incoherent/non-positive limits.
- `partition_settings.unsupported`: select a precision/package mode admitted by both
  the exact profile and host snapshot.
- `partition_settings.capacity_exceeded`: reduce the project grid/capacity request or
  select a product artifact with adequate declared ceilings.
- `partition_settings.stale`: resolve again using current immutable revisions.
- `partition_settings.lifecycle_unavailable`: wait for a new active settings owner.

## Limitations

The contract carries backend-neutral fixed-size facts only. It does not load package
bytes, mount partitions, publish registries, install capabilities, or persist project
configuration. Those remain application, Assets, Configuration, and World Streaming
composition responsibilities.

## Validation Record

Contract tests cover exact-boundary acceptance, malformed and over-capacity inputs,
profile/package incompatibility, stale replacement, cancellation, and shutdown.

## References

- [World Streaming Architecture](../architecture/runtime/world-streaming-architecture.md)
- [Configuration System](../architecture/foundation/configuration-system.md)
- [System Design](../architecture/foundation/system-design.md)
