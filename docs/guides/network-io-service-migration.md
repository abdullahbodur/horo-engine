# Network I/O Service Migration

## Purpose

Move transport polling and completion delivery behind the bounded
`NetworkIoService` owner-thread handoff without exposing native event-loop state.

## Prerequisites

- Keep the private transport as the owner of its dedicated I/O thread.
- Normalize native outcomes into generation-checked Horo connection handles,
  prepared `PacketBuffer` leases and `NetworkTerminalRecord` failures.
- Choose finite queue, per-poll and per-owner-drain limits during host composition.

## Workflow

1. Implement `INetworkIoPollSource` in the private transport target. `Poll` may
   inspect native state, but publishes only `NetworkIoCompletion` values and does
   not retain its generation-scoped producer.
2. Create one `NetworkIoService` on the transport's declared owner thread and
   transfer unique ownership of the private polling source.
3. Invoke `PollBackend` from the transport-owned I/O thread with a finite count
   budget. Treat queue-full, stale-producer and cancellation errors as typed
   transport policy inputs.
4. During the owner thread's `NetworkPoll` phase, call `DrainOwnerThread` with a
   finite budget. Apply Scene, Gameplay or editor mutations only from that
   owner-thread consumer.
5. During shutdown, stop new transport requests, call `Shutdown`, and only then
   destroy consumer-owned runtime state. Repeated shutdown is safe.

## Troubleshooting

- `network.io.wrong_thread` means the drain ran outside the construction thread.
- `network.io.poll_stale` means a backend retained a producer after `Poll` returned.
- `network.io.completion_queue_full` means owner drain or explicit overload policy
  did not keep up with the prepared capacity; the service never grows implicitly.
- `network.transport.shutting_down` means admission is closed and late work must
  be discarded rather than redirected to a replacement owner.

## Limitations

NET-001.5 defines polling and handoff only. Listener/connection transition,
timeout and handle-reclamation policy remain NET-001.6. Concrete Null and GNS
backends remain separately owned transport work.

## Validation Record

Regression coverage exercises ordered success and packet ownership, hard queue
and per-call bounds, malformed records, exact backend failure propagation,
cancellation before backend entry, wrong-thread drain, retained producer
rejection and idempotent shutdown with no consumer callback.

## References

- [Networking Architecture](../architecture/runtime/networking-architecture.md)
- [Concurrency And Job System](../architecture/foundation/concurrency-and-jobs.md)
- [ADR-020](../adr/020-network-target-ownership-and-dependency-boundary.md)
