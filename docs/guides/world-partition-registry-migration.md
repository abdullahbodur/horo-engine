# World Partition Registry Snapshot Migration

## Purpose

`WorldPartitionRegistry` is the explicit publication boundary for runtime code that
previously retained a `WorldPartitionDescriptor` borrow or searched manifest cells
without a publication fence.

## Prerequisites

- One validated `WorldPartitionDescriptor` for the mounted partition.
- The exact host-issued `StreamingRuntimeOwnerToken` and registry identity.
- Positive cell and query-result ceilings within the implementation hard limits.

## Workflow

### 1. Publish the descriptor

1. Create one registry for an exact `StreamingRuntimeOwnerToken` and select finite
   cell/query limits before mounting consumers.
2. Build and validate a complete `WorldPartitionDescriptor` off the publication
   path.
3. Publish revision `1`, then only exact successors. Publication failure preserves
   the previous snapshot and does not move from the candidate descriptor.
4. On world replacement, create a new registry for the new partition epoch. Do not
   reuse handles from the old mounted incarnation.

### 2. Capture and query

Capture a `WorldPartitionRegistrySnapshot` and retain it for the complete operation
that consumes its results. `Find` returns a generation-pinned
`WorldPartitionCellHandle`; resolve that handle only through the same snapshot.
Replacement never changes an already retained snapshot, and a newer snapshot
rejects the old handle as stale.

Spatial queries accept exact inclusive world-millimeter bounds and caller-owned
handle storage. Choose the output capacity from an explicit product ceiling. If the
query would exceed it, the call returns
`world_streaming.partition_registry.capacity_exceeded` without changing any output
element. Split the query deliberately or increase the configured ceiling; do not
silently truncate results.

### 3. Close admission safely

`BeginCancellation` closes publication and new snapshot admission. `Shutdown`
releases the registry's current publication. Snapshots already retained by admitted
work remain valid until their owners release them, so cancellation does not create
use-after-free and shutdown does not fabricate completion. The registry owns only
immutable index metadata; residency, provider resources, ECS objects, and I/O remain
with their existing authorities.

## Troubleshooting

| Symptom | Check | Recovery |
| --- | --- | --- |
| Stale handle | Registry identity, revision, partition, and epoch | Repeat lookup against the snapshot that will consume the result |
| Unsupported query | Requested layer membership and LOD range | Use an explicitly declared filter; no fallback is selected |
| Capacity exceeded | Matching cell count and configured result ceiling | Narrow the query or select a larger supported ceiling before publication |

## Limitations

The registry is an in-memory index, not the persistent `world.index` format, a
residency authority, or a provider selector. Queries traverse the immutable spatial
index built during publication and expose bounded work evidence in
`WorldPartitionSpatialQueryResult`; use `result.matches` instead of treating the
return value as a bare count. Queries operate over
the immutable cell array; later internal layouts may evolve without changing the
generation-fenced query semantics or typed work evidence.

## Validation Record

Validated on 2026-09-12 with the world-streaming unit and public-header consumer
targets on macOS. Coverage includes lookup, bounded spatial query and branch pruning,
replacement, cancellation, shutdown, stale handles, invalid filters, capacity
failure, and concurrent snapshot capture.

## References

- [World Streaming Architecture](../architecture/runtime/world-streaming-architecture.md)
- [ADR-012](../adr/012-world-streaming-partition-authority-and-subsystem-boundaries.md)
- [Repository Working Contract](../../AGENTS.md)
