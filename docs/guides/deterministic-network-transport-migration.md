# Deterministic Network Transport Migration

## Purpose

Compose a network-disabled facade, in-memory loopback, or seeded impairment
fixture without sockets, platform dependencies, I/O threads, or production
backend types. The same bounded budget, handle-generation, cancellation, and
shutdown rules apply to every mode.

## Prerequisites

- Use `HoroEngine::NetworkTransportNull` only from the host composition root.
- Prepare a complete `TransportLimitPolicyV1` and hard storage capacities.
- Keep session authentication and gameplay authority outside the transport.

## Workflow

1. Select `RejectAll`, `Loopback`, or `Simulated` explicitly. Never treat the null
   transport as an automatic fallback for a failed production backend.
2. Create a complete `DeterministicTransportDescriptor` with a non-zero scenario
   revision and seed, bounded payload/fragment/delivery storage, and channel limit.
3. Open exact lifecycle-owned connection generations, then call `Advance` once per
   strictly increasing network tick.
4. Call `Send` with caller-owned bytes. The transport copies admitted fragments
   before returning and reports loss, duplication, replacement, bounded discard,
   or required close as typed evidence.
5. Consume returned event views before the next mutating call. Copy bytes that must
   outlive that boundary.
6. Close connections explicitly and drain their disconnect event. Shutdown closes
   admission and discards remaining bounded work idempotently.

## Troubleshooting

- `TransportCapabilityUnavailable` is the expected `RejectAll` result.
- `TransportBudgetInvalid` indicates malformed scenario, tick, channel, payload,
  or traffic metadata.
- `TransportBudgetCapacityExceeded` means fragmentation or duplication cannot fit
  prepared delivery storage; no partial message is scheduled.
- Stale handles and shutdown use the same typed errors as the shared lifecycle and
  transport-budget contracts.

## Limitations

Deterministic transport fixtures prove orchestration, ordering, bounds, and error
behavior. They do not qualify OS sockets, native encryption, production latency,
or throughput. Simulated impairment is reproducible test policy, not wire truth.

## Validation Record

Contract coverage includes explicit null rejection, copied loopback fragmentation,
replaceable-state supersession, repeatable seeded loss/duplication/jitter/reorder,
capacity rollback, cancellation, disconnect, stale generation, and shutdown.

## References

- [Networking Architecture](../architecture/runtime/networking-architecture.md)
- [ADR-020: Network Target Ownership and Dependency Boundary](../adr/020-network-target-ownership-and-dependency-boundary.md)
- [Transport Budget Migration](./transport-budget-migration.md)
