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

## Troubleshooting

- `world_streaming.object_ownership.invalid` means identities, revisions, or the exclusive owner binding are malformed.
- `world_streaming.object_ownership.unsupported` means the ownership class, owner kind, or cell-exit policy combination is not permitted.
- `world_streaming.object_ownership.owner_stale` means the request belongs to another mounted-world lifetime.
- `world_streaming.object_ownership.revision_stale` means compare-and-swap evidence or the successor revision is incorrect.

## Limitations

This policy does not execute entity transfer or serialize runtime-spawned state.
WST-005.8 owns runtime handoff execution, and SAV-004.3 owns durable save identity.

## Validation Record

The world-streaming contract suite covers normal authored/runtime admission, owner
replacement, required handoff, stale generations, cancellation, shutdown, capacity,
ambiguous identities, unsupported policies, and revision exhaustion.

## References

- [World Streaming Architecture](../architecture/runtime/world-streaming-architecture.md)
- [Ownership and Resource Lifetime](../architecture/foundation/ownership-and-resource-lifetime.md)
