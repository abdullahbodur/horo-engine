# Transport Budget Migration

## Purpose

Adopt the versioned transport-budget contract for connection admission, queued
bytes and messages, per-tick rates, and sustained-overload handling. The contract
keeps reliable and replaceable traffic behavior explicit and prevents a slow or
abusive peer from creating unbounded engine work.

## Prerequisites

- Use generation-safe `ConnectionHandle` values issued by the network lifecycle
  owner.
- Select hard storage capacity during host composition, before transport traffic
  is admitted.
- Keep transport payloads and native handles behind the private backend boundary.

## Workflow

1. Create one owner-thread `TransportBudgetController` with hard prepared
   `TransportBudgetCapacity` and a complete `TransportLimitPolicyV1` snapshot.
2. Open and close exact connection generations as lifecycle ownership changes.
3. Start each network tick with a strictly increasing tick value.
4. Call `Admit` before copying a payload or mutating a native send queue. Retain
   the returned ticket until the admitted work is sent or explicitly discarded.
5. Treat reliable backpressure as a typed rejection. For replaceable state,
   honor `Replaced` and `DroppedReplaceable` without inventing an unbounded retry
   queue. Close a peer when admission returns `ConnectionMustClose`.
6. Replace policy only with the exact current revision and a complete candidate.
   A failed replacement leaves the prior policy active.
7. Call `Shutdown` before releasing the transport; late tickets then fail safely.

## Troubleshooting

- `TransportBudgetInvalid` indicates malformed version, revision, tick, traffic,
  or finite limits.
- `TransportBudgetCapacityExceeded` indicates that setup capacity, current usage,
  or a single submission cannot satisfy the candidate policy.
- `TransportReliableBackpressure` means required traffic was not queued; retain it
  only under a separate bounded owner policy or close the peer.
- `TransportBudgetPolicyStale` and `TransportBudgetTicketStale` are revision and
  generation fences. Refresh the snapshot or discard the late completion.

## Limitations

The controller owns accounting and admission decisions, not payload memory,
transport threads, sockets, or connection lifecycle transitions. Calls are
serialized by one owner thread. The policy does not provide a hidden retry queue.

## Validation Record

Contract tests cover setup bounds, exact connection generations, reliable
rejection, replaceable coalescing and discard, rate resets, sustained saturation,
atomic policy replacement, cancellation, shutdown, and stale tickets.

## References

- [Networking Architecture](../architecture/runtime/networking-architecture.md)
- [ADR-101: Interest, Priority and Network Budget Model](../adr/101-interest-priority-and-network-budget-model.md)
- [Network Lifecycle Migration](./network-lifecycle-migration.md)
