# Network Lifecycle Migration

## Purpose

Migrate private transport adapters from callback-owned connection flags to the
bounded Horo `NetworkLifecycleRegistry`. The registry makes listener, resolution,
connect, authentication-ready, close, cancellation, timeout, and shutdown results
observable without exposing native handles or allowing late callbacks to mutate a
replacement generation.

## Prerequisites

- Normalize private backend work through `NetworkIoService`.
- Issue generation-checked `ListenerHandle` and `ConnectionHandle` values.
- Supply a monotonic caller-owned tick domain for connect deadlines.
- Keep session authentication and gameplay publication outside the transport.

## Workflow

1. Create one registry with finite listener and connection capacities during host
   composition. Construction is the only storage-growth point.
2. Pair each admitted bind, resolve, or connect with a non-zero
   `NetworkOperationGeneration`. Retain that pair in the private native callback
   context.
3. Drain normalized I/O completions on the registry owner thread. Advance only
   through the documented states and reject any mismatched handle or operation
   generation as stale.
4. Treat `AuthenticationReady` as a bounded handoff to the session admission
   controller. It does not grant authenticated session or gameplay authority.
5. Publish one immutable `NetworkLifecycleTerminal`. A graceful close has no
   failure record; failed, cancelled, timed-out, and shutdown outcomes carry a
   canonical `NetworkTerminalRecord`.
6. Run deadline scans from the same monotonic clock domain and call `Shutdown`
   before stopping the normalized I/O service. Both close requests and shutdown
   are idempotent; duplicate terminal completions cannot replace the first reason.

## Troubleshooting

- `network.lifecycle.operation_stale`: discard the callback; its handle or work
  generation is no longer current.
- `network.lifecycle.transition_invalid`: preserve the current state and inspect
  adapter ordering rather than skipping a phase.
- `network.lifecycle.capacity_exceeded`: reduce concurrent work or raise the
  finite setup limit within the public hard bounds.
- `network.terminal.already_resolved`: retain the first terminal result; do not
  reinterpret the duplicate callback.

## Limitations

The registry does not resolve DNS, own sockets, authenticate peers, activate a
`NetworkSession`, or schedule threads. Concrete transports retain native work;
the session admission controller owns negotiation and authentication after the
authentication-ready handoff.

## Validation Record

`HoroNetworkApiTests` covers valid listener and connection flows, illegal phase
changes, malformed terminal evidence, finite capacity, exact timeout boundaries,
cancellation races, stale handle and operation generations, replacement, and
idempotent shutdown.

## References

- [Networking Architecture](../architecture/runtime/networking-architecture.md)
- [Default Real-Time Transport Backend](../adr/097-default-real-time-transport-backend.md)
- [Network I/O Service Migration](./network-io-service-migration.md)
