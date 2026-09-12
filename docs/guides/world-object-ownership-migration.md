# World Object Ownership Migration

## Purpose

Adopt the WST-001.7 ownership policy without ambient registries, pointer identity,
silent persistence promotion or implicit behavior when a cell retires.

## Prerequisites

- Publish authored objects with `WorldAuthoringObjectAddress` and a valid ownership revision.
- Retain the exact mounted `StreamingRuntimeOwnerToken` and cell `StreamingFence` values.
- Choose a bounded ownership-record capacity before opening admission.

## Workflow

1. Classify authored content as `AuthoredAlwaysPresent` or `AuthoredSpatial`; classify
   newly spawned runtime content as `RuntimeSpawned`.
2. Bind exactly one owner. Always-present content uses `World`; spatial authored
   content uses `Cell`; runtime-spawned content uses `World`, `Cell`, or a typed
   non-spatial `Runtime` owner.
3. For a cell-owned runtime spawn, select `Retire` or `RequireHandoff`. Do not infer
   persistence from a missing policy or failed handoff.
4. Validate the descriptor, then validate admission against an immutable owner
   snapshot. Publish only the returned `Insert`, `Replace`, or `Handoff` decision.
5. Use the exact current revision and its non-wrapping successor for replacement.
   Treat stale-owner, stale-revision, capacity, and lifecycle errors as no-mutation results.
6. When unloading a cell, create one `RuntimeEntityCellExitOperation` for each
   cell-owned runtime entity from the exact current ownership fact and retiring-cell
   generation. Keep the operation charged until it becomes terminal.
7. For `Retire`, admit the operation, begin source retirement at the Scene owner safe
   point, and acknowledge retirement with the same operation/entity/cell handle.
8. For `RequireHandoff`, supply the exact next ownership revision and destination owner,
   then prepare and accept destination state before beginning source retirement. Treat
   `BeginSourceRetirement` as the non-cancellable ownership-publication boundary.
9. Before commit, cancellation, failure, replacement, or shutdown preserves the source.
   A prepared destination must reach `RollingBackDestination` and receive its exact
   rollback acknowledgement before capacity is reclaimed. After commit, shutdown drains
   normal source retirement and does not rewrite the committed outcome.

## Troubleshooting

- `world_streaming.object_ownership.invalid` means identities, revisions, or the exclusive owner binding are malformed.
- `world_streaming.object_ownership.unsupported` means the ownership class, owner kind, or cell-exit policy combination is not permitted.
- `world_streaming.object_ownership.owner_stale` means the request belongs to another mounted-world lifetime.
- `world_streaming.object_ownership.revision_stale` means compare-and-swap evidence or the successor revision is incorrect.
- `world_streaming.runtime_entity_cell_exit.stale` means the transaction no longer names
  the current entity, ownership fact, world lifetime, or retiring cell generation.
- `world_streaming.runtime_entity_cell_exit.capacity_exceeded` means the owner must finish
  or roll back an in-flight transaction before admitting another.
- `world_streaming.runtime_entity_cell_exit.transition_invalid` means a caller skipped a
  required preparation, acceptance, retirement, rollback, or acknowledgement phase.

## Limitations

The cell-exit operation sequences host-owned entity transfer but does not copy ECS
components or serialize runtime-spawned state itself. The Scene/runtime host owns those
safe-point adapters, and SAV-004.3 owns durable save identity.

## Validation Record

The world-streaming contract suite covers normal authored/runtime admission, owner
replacement, retirement, required handoff, stale entity/cell generations, cancellation,
failure, replacement, shutdown drain, rollback acknowledgement, capacity, ambiguous
identities, unsupported policies, and revision exhaustion.

## References

- [World Streaming Architecture](../architecture/runtime/world-streaming-architecture.md)
- [Ownership and Resource Lifetime](../architecture/foundation/ownership-and-resource-lifetime.md)
