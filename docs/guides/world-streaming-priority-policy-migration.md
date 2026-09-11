# World Streaming Priority Policy Migration

## Purpose

Adopt the typed, immutable WST-002.5 ranking boundary instead of sorting cell work
with camera-local floats, registration order, or gameplay-scaled timers.

## Prerequisites

- Produce validated `StreamingSourceDescriptor` values through the WST-002.1
  ownership boundary.
- Compute exact non-negative source-to-cell distance from the WST-002.2 range data.
- Issue one stable `StreamingPriorityPolicyId` and monotonic non-zero revision from
  the host composition owner.

## Workflow

1. Build a complete `StreamingPriorityPolicyRequest`. The defaults implement the
   normative one-metre epsilon, Camera/Gameplay/Network/Preload multipliers, 0.1 per
   second age slope, and 2.0 age cap.
2. Retain the resulting immutable `StreamingPriorityPolicy`. Creating it performs no
   registration, scheduling, provider selection, or allocation in the frame path.
3. Capture a bounded candidate snapshot. Each row includes the exact source revision,
   canonical cell, distance, explicit override in `[0.5, 2.0]`, and unscaled monotonic
   enqueue timestamp.
4. Call `RankStreamingCellPriorities` with the current policy revision, partition
   epoch, lifecycle state, and caller-owned output storage.
5. Submit only the successful ranked prefix to the separate scheduler and budget
   authority. A higher rank is never budget admission or permission to bypass pins,
   required-content policy, or retirement.

The old pattern:

```cpp
std::sort(work.begin(), work.end(), [](const auto& left, const auto& right) {
    return left.cameraDistance < right.cameraDistance;
});
```

becomes an explicit immutable evaluation:

```cpp
const auto rankedCount = RankStreamingCellPriorities(policy, context, candidates, output);
if (rankedCount.HasError()) {
    return rankedCount.ErrorValue();
}
```

## Troubleshooting

- `world_streaming.priority_policy.invalid`: validate every stable identity, finite
  coefficient, cell/source descriptor, and override before publishing the snapshot.
- `world_streaming.priority_policy.unsupported`: migrate the request to the current
  contract version; no compatibility fallback is selected.
- `world_streaming.priority_policy.stale`: recapture the policy revision or mounted
  partition epoch and rebuild the whole immutable candidate snapshot.
- `world_streaming.priority_policy.capacity_exceeded`: reduce the snapshot or provide
  output storage within the configured fixed ceiling. Output remains unchanged.
- `world_streaming.priority_policy.lifecycle_unavailable`: stop new ranking after
  cancellation begins and let already admitted work retire through its normal owner.

## Limitations

The bounded age term improves the rank of feasible waiting work but cannot guarantee
admission for an oversized cell or during permanent budget pressure. Retry cooldown,
budget reservation, provider readiness and cell lifecycle remain separate contracts.

## Validation Record

Regression coverage includes the normative formula, all intent multipliers, override
bounds, age clamping/capping, canonical ties, input-order independence, policy and
partition replacement, lifecycle closure, capacity boundaries, malformed inputs and
transactional output preservation. Platform compilation and execution are validated
by repository Linux, macOS and Windows CI; no local build was run for this migration.

## References

- [World Streaming Architecture](../architecture/runtime/world-streaming-architecture.md)
- [ADR-012](../adr/012-world-streaming-partition-authority-and-subsystem-boundaries.md)
