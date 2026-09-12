# World Streaming Cell Stability Migration

Use `StreamingCellStabilityPolicy` at the streaming-authority boundary instead of
ad hoc expanded volumes, gameplay timers, or per-provider unload cooldowns.

## Integration

1. Publish one validated policy with a stable identity and exact revision.
2. Reduce source demand normally, then supply the cell's signed distance to the
   strongest relevant source boundary: positive inside, negative outside.
3. Retain the last successful `StreamingCellStabilitySnapshot` in the authority's
   bounded per-cell storage and pass it to the next evaluation.
4. Apply the returned snapshot only on success. Remove the record when its phase is
   `Unloaded`; keep residency while it is `Resident` or `Lingering`.
5. On policy or partition replacement, evaluate new work only with the new fences.
   Old snapshots are stale and do not migrate implicitly.

The enter and exit margins are inclusive. A new cell must reach the enter margin;
an admitted cell remains held until it moves beyond the negative exit margin. Loss
of demand starts linger using unscaled monotonic service time, and renewed demand
cancels that timer. Pins bypass only the geometric thresholds. They do not bypass
the authority's record, memory, scheduler, or provider budgets.

The evaluator performs no allocation, registration, range query, load/unload,
reservation, clock read, or hidden mutation. Cancellation and shutdown reject new
evaluations; canonical cell retirement remains owned by the streaming authority.

## Failure Handling

- `CellStabilityInvalid`: repair malformed policy, observation, or retained state.
- `CellStabilityUnsupported`: migrate the closed version/enum contract.
- `CellStabilityCapacityExceeded`: retire a record or explicitly raise the supported
  tracked-cell ceiling before admitting another.
- `CellStabilityStale`: recapture current policy/partition facts and monotonic time.
- `CellStabilityLifecycleUnavailable`: stop new evaluation and drain through the
  authority's existing retirement path.
