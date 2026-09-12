# World Layer State Migration

## Purpose

Adopt the WST-006.2 Loaded/Activated state machine without deriving layer state from
physical cell residency or publishing unacknowledged cleanup.

## Prerequisites

- Publish a valid WST-006.1 `WorldLayerOwnershipDescriptor`.
- Retain the exact mounted-world, ownership-revision and state-revision fence.
- Choose a bounded layer-state capacity before opening admission.

## Workflow

1. Admit the stable layer as `Unloaded`; persistent policy does not fabricate load
   completion.
2. Publish the normal ordered path through `Loading`, `Loaded`, `Activating` and
   `Activated` using the exact current fence for every command.
3. Deactivate to `Loaded` before unloading to `Unloaded`. Treat each intermediate
   state as retained work that still needs an explicit completion.
4. During cancellation or shutdown, stop new load and activation work. Cancellation
   enters `Deactivating` or `Unloading`; publish the matching completion before the
   last stable state. Repeated cancellation preserves the exact rollback record and
   revision. Do not infer completion from cell eviction.
5. Retain `FailurePending` when in-flight work fails. Finish deactivation and unload
   before publishing `Failed`; the failure signal alone is not cleanup acknowledgement.
6. Replace layer ownership only from `Unloaded` or cleanup-complete `Failed`, using
   the exact WST-006.1 ownership successor and current state fence.

## Troubleshooting

- `world_streaming.layer_state.invalid` means a record, fence or bounded authority
  snapshot is malformed.
- `world_streaming.layer_state.stale` means the world, layer, ownership revision or
  state revision no longer matches.
- `world_streaming.layer_state.transition_invalid` means an ordered lifecycle edge
  was skipped or requested from the wrong state.
- `world_streaming.layer_state.lifecycle_unavailable` means admission is cancelling
  or closed; only explicit drain transitions remain accepted while cancelling.

## Limitations

This state machine does not stage cells, invoke providers, publish Scene entities or
resolve target filtering. Those actions remain at their existing owner boundaries.

## Validation Record

Focused coverage exercises the exhaustive valid-state, command and authority matrix,
the full load/activate/deactivate/unload path, idempotent cancellation at revision
exhaustion, failure cleanup, stale fences, ownership replacement, capacity and
unsupported transitions.

## References

- [World Streaming Architecture](../architecture/runtime/world-streaming-architecture.md)
- [World Layer Ownership Migration](./world-layer-ownership-migration.md)
