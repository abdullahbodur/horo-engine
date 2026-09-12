# World Layer Filtering Migration

## Purpose

Adopt WST-006.3 target filtering without renumbering stable source layers, hiding
exclusion reasons or mixing filtering with cell scheduling.

## Prerequisites

- Publish valid WST-006.1 layer ownership facts in ascending `StreamingLayerId` order.
- Retain the corresponding version-one manifest flags and exact mounted-world token.
- Capture one immutable filter-policy identity and revision.
- Provide bounded caller-owned storage for every input decision.

## Workflow

1. Select `Editor`, `ClientRuntime` or `DedicatedServerRuntime` and an explicit
   optional-layer policy.
2. Pair each stable ownership fact with its manifest flags without changing the
   source identity or ownership revision.
3. Validate the complete canonical candidate snapshot and write one decision per
   source layer. Cache the exact mounted-world token with the result and each decision;
   layer identity and revision alone are not unique across owner, partition or epoch
   replacement.
4. Keep excluded decisions with their typed reason. Do not collapse missing,
   editor-only, role-filtered and unsupported-optional content into one empty result,
   and do not treat a default Unresolved decision as included.
5. Replace filtering policy with the same stable identity and exact non-wrapping
   revision successor when platform support changes; treat old evidence as stale.

## Troubleshooting

- `world_streaming.layer_filter.invalid` means the policy/context is malformed or
  candidates are not in canonical identity order.
- `world_streaming.layer_filter.unsupported` means target values, flags or persistent
  classification are contradictory.
- `world_streaming.layer_filter.stale` means the world owner or policy revision was
  replaced.
- `world_streaming.layer_filter.capacity_exceeded` means the candidate ceiling or
  caller-owned decision storage is insufficient; no rows were written.

## Limitations

Filtering does not load or activate layers, mutate packages, schedule cells or choose
a fallback target. WST-006.2 owns layer state and existing World Streaming contracts
own physical cell residency.

## Validation Record

Focused coverage exercises editor, client and dedicated-server projections, optional
policy, exact owner/partition/epoch binding, source identity preservation, unresolved
defaults, typed exclusion reasons, transactional output, stale evidence, canonical
ordering, duplicates, unsupported flags, capacity, cancellation and shutdown.

## References

- [World Streaming Architecture](../architecture/runtime/world-streaming-architecture.md)
- [World Layer Ownership Migration](./world-layer-ownership-migration.md)
