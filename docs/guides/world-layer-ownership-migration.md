# World Layer Ownership Migration

## Purpose

Adopt the WST-006.1 layer contract without deriving identity from names, collapsing
placement into residency, or granting gameplay and network authorities direct cell
control.

## Prerequisites

- Retain each manifest-issued `StreamingLayerId` unchanged across target projections.
- Retain the exact mounted `StreamingRuntimeOwnerToken` for the publication.
- Choose a bounded layer-fact capacity before opening admission.

## Workflow

1. Classify placement independently as `Spatial` or `NonSpatial`.
2. Select `Persistent`, `Streamed` or `RuntimeControlled` residency without using
   current Loaded/Activated state as the classification.
3. Mark the audience as `Runtime` or `EditorOnly`. Filtering may exclude a stable
   identity from a target but must not replace that identity.
4. Bind exactly one control owner. Runtime persistent/streamed layers use World
   Streaming, editor-only layers use an editor document authority, and
   runtime-controlled layers use a gameplay-script or network-replication authority.
5. Validate the descriptor and then its admission against an immutable owner snapshot.
   Publish only the returned `Insert`, `Replace` or `Handoff` decision.
6. Use the exact current revision and non-wrapping successor for replacement. Treat
   every typed failure as a no-mutation result.

## Troubleshooting

- `world_streaming.layer_ownership.invalid` means an identity, revision, owner token,
  authority generation or bounded context is malformed.
- `world_streaming.layer_ownership.unsupported` means classification and control
  authority contradict one another or immutable classification changed.
- `world_streaming.layer_ownership.owner_stale` means the fact belongs to another
  mounted-world lifetime.
- `world_streaming.layer_ownership.revision_stale` means compare-and-swap evidence or
  the successor revision is incorrect.

## Limitations

This contract does not load, activate or filter layers and does not mutate cell
residency. WST-006.2 owns layer state and WST-006.3 owns target filtering.

## Validation Record

The focused world-streaming coverage exercises each placement/residency/audience
class, exclusive owner representation, insertion, replacement, runtime-control
handoff, stale identity and world fences, capacity, cancellation, shutdown,
unsupported combinations and revision exhaustion.

## References

- [World Streaming Architecture](../architecture/runtime/world-streaming-architecture.md)
- [Ownership and Resource Lifetime](../architecture/foundation/ownership-and-resource-lifetime.md)
