# Handshake Negotiation Migration

## Purpose

Move protocol and capability compatibility out of transport callbacks and into the
bounded Horo `HandshakeNegotiator`. A successful transport connection remains
pre-active until one immutable session selection is accepted.

## Prerequisites

- Register stable protocol and feature identities with `ProtocolIdentityRegistry`.
- Capture immutable `TransportCapabilities` from the explicitly selected transport.
- Retain the exact `ConnectionHandle` and non-zero session admission generation.
- Supply an absolute deadline in one host-owned monotonic tick domain.

## Workflow

1. Build `HandshakeLocalDescriptor` from host-owned protocol policy. Supported and
   required feature spans are copied during `Create`; their backing storage may then
   be released.
2. Keep the returned negotiator on the session owner thread. It owns no socket,
   transport backend, credential or trust root.
3. Parse the peer hello under the wire codec's byte and field limits, then pass its
   bounded Horo identities and requirements as one `HandshakeOffer`.
4. Call `Accept` with the exact connection/session generation and current monotonic
   tick. Required features, compression and delivery semantics are never weakened.
5. On success, retain the immutable `HandshakeSelection` through authentication and
   activation. Its transport evidence remains tied to the capability revision.
6. On reject, cancellation, timeout or shutdown, close safely. Start another attempt
   only with a new connection and session generation; late completions are stale.

## Troubleshooting

- `network.handshake.invalid`: reject malformed, oversized, duplicate or foreign-
  namespace declarations before authentication work.
- `network.handshake.incompatible`: align the explicit protocol/schema/features/
  compression policy; do not silently drop a mandatory requirement.
- `network.handshake.state_invalid`: retain the first terminal outcome instead of
  reusing a completed negotiator.
- `network.lifecycle.operation_stale`: discard work captured for a replaced handle or
  admission generation.

## Limitations

This contract does not decode packets, establish transport security, authenticate a
principal, calculate a transcript digest or activate gameplay. Those remain in their
own codec, transport and session-admission boundaries.

## Validation Record

`HoroNetworkRuntimeTests` covers valid highest-version selection, canonical feature
intersection, non-downgrade behavior, malformed and oversized offers, schema/version
incompatibility, transport limit propagation, replacement generations, cancellation,
deadline boundaries, explicit rejection and idempotent shutdown.

## References

- [Networking Architecture](../architecture/runtime/networking-architecture.md)
- [Protocol Session and Trust Policy](../adr/098-protocol-session-and-trust-policy.md)
- [Network Lifecycle Migration](./network-lifecycle-migration.md)
