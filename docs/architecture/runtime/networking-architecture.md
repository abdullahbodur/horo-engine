# Networking Architecture

## Purpose

This document defines optional network transport, connection lifecycle,
message serialization, asynchronous I/O, security boundaries, runtime
integration, and observability.

Networking is an optional engine capability. Hosts and games that do not need it
do not construct or link a concrete transport backend.

## Core Decisions

- Networking is layered into transport, protocol, and gameplay/application
  semantics.
- Network I/O never mutates scene, editor, or application state from an I/O
  thread.
- Messages use explicit schemas and version negotiation.
- Buffers, queues, message sizes, connection counts, and rates are bounded.
- Security policy is mandatory for remote communication.
- General engine data-bus events are not serialized automatically.
- Simulation inputs and state replication use dedicated protocols rather than
  arbitrary ECS memory or C++ object serialization.

## Layer Model

```text
Gameplay / Application Protocol
            |
            v
Typed Message And Session Layer
            |
            v
Reliable / Unreliable Transport Interface
            |
            v
Platform Socket / TLS Backend
```

Transport implementations own sockets, encryption sessions, and native event
loops. Protocols own message identity, schema, ordering, and compatibility.

## Capability Interface

```cpp
class NetworkService {
public:
    Result<ConnectionHandle> Connect(const ConnectRequest&);
    Result<ListenerHandle> Listen(const ListenRequest&);
    Result<void> Send(ConnectionHandle, MessageView, SendPolicy);
    Result<void> Close(ConnectionHandle, CloseReason);
};
```

Handles are generation checked. Connection and listener identity is never a raw
socket descriptor.

## Connection Lifecycle

```text
Created
  -> Resolving
  -> Connecting
  -> Authenticating
  -> Active
  -> Closing
  -> Closed

Any non-terminal state -> Failed
```

Every accepted connection reaches one terminal state. Timeouts and cancellation
are distinct from protocol rejection and transport failure.

## I/O Threading

Network I/O runs through an owned I/O service. It:

- reads into bounded buffers
- performs framing and basic validation
- enqueues typed inbound messages
- consumes bounded outbound queues
- publishes connection-state changes through an owner-thread continuation

Application, scene, editor, and gameplay handlers execute on their declared
owner thread or job context.

## Message Schema

Messages have:

- stable protocol and message IDs
- schema version
- bounded encoded size
- required and optional typed fields
- validation before allocation or dispatch
- explicit unknown-field/version behavior

C++ memory layout, vtables, pointers, RTTI names, and unrestricted object graphs
are never serialized.

## Protocol Negotiation

Peers negotiate:

- protocol version range
- required features
- compression
- authentication mechanism
- maximum frame/message sizes
- optional unreliable channel support

Failure to agree closes the session with a safe stable reason.

## Delivery Semantics

Each message type declares:

- reliable or unreliable delivery
- ordering scope
- duplicate handling
- expiration/deadline
- backpressure policy
- maximum rate

The transport does not imply that all application messages require global
ordering.

## Runtime Simulation Integration

Real-time gameplay networking uses typed simulation messages such as:

- input frames
- snapshots
- authoritative corrections
- acknowledgements
- spawn/despawn records with stable network IDs

Network IDs are separate from local ECS entity handles. Resolution is owned by
the active networked scene/session.

Fixed-tick consumption and prediction policy are game protocol decisions built
on [Runtime Lifecycle](./runtime-lifecycle.md) and
[Input Architecture](./input-architecture.md).

## Data Bus Relationship

`EngineDataBus` events are process-local and are not automatically sent over the
network. A protocol adapter may observe a committed authority and create an
explicit versioned network message.

Inbound messages invoke validated application/gameplay operations. They do not
publish fake local commands onto a generic bus.

## Security

Remote communication requires:

- authenticated peers where the protocol changes state
- encryption for credentials and sensitive data
- certificate or key validation
- replay and downgrade resistance where applicable
- connection, rate, size, and resource limits
- safe parsing before dispatch

MCP remote access follows its own protocol and
[Application Security](../security/application-security.md); it is not exposed through a
game networking listener by default.

## Backpressure

Inbound and outbound queues are bounded per connection and globally. Queue-full
policy is declared by message class:

- reject new send
- replace stale state snapshot
- drop expired unreliable message
- close abusive or non-reading peer

The simulation and main thread never block indefinitely on network I/O.

## Configuration

Network configuration includes endpoints, limits, timeouts, and protocol
features. Secrets and private keys are credential references, not ordinary
configuration values.

Binding non-loopback listeners requires explicit application/game policy.

## Observability

Metrics include:

- connection counts and states
- bytes and messages by bounded protocol category
- queue depth and drops
- round-trip time and loss where supported
- parse, authentication, timeout, and rate-limit failures

Logs avoid payload contents by default and correlate safe connection/session IDs.

## Testing

Required tests cover:

- framing fragmentation and coalescing
- malformed size and schema rejection
- version negotiation
- cancellation and timeout
- queue saturation and snapshot replacement
- owner-thread dispatch
- authentication and downgrade failure
- deterministic simulated latency, loss, duplication, and reordering
- stale network ID rejection after scene replacement
- clean shutdown with active connections

## Related Documents

- [Network Debugger UI Reference](./network-debugger.html): replication, bandwidth, RPC log, and latency simulation panel.

- [Runtime Lifecycle](./runtime-lifecycle.md)
- [Input Architecture](./input-architecture.md)
- [Concurrency And Job System](../foundation/concurrency-and-jobs.md)
- [Application Security](../security/application-security.md)
- [Error And Diagnostics](../foundation/error-and-diagnostics.md)

