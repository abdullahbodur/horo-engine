# Networking Architecture

## Purpose

This document defines the optional network transport, connection lifecycle,
message serialization, asynchronous I/O, security boundaries, runtime
integration, and observability for Horo Engine.

Networking is an optional engine capability. Hosts, tools, and games that do not need it
do not construct, link, or activate concrete transport backends.

## Core Decisions

- **Strict Layered Architecture**: Networking is separated into target-level tiers: public contracts and types (`HoroEngine::NetworkApi`), session coordination and replication runtime (`HoroEngine::NetworkRuntime`), and private concrete transport backends (`HoroEngine::NetworkTransportNull`, `HoroEngine::NetworkTransportENet`).
- **Transport vs Session Split**: `INetworkTransport` is a packet-transport abstraction. Protocol negotiation, authentication, replication, and `NetworkSession` live in `NetworkRuntime`. Transport backends do not implement application authentication.
- **Handle-Based Public Transport API**: Public transport operations use `INetworkTransport` plus generation-checked `ConnectionHandle` / `ListenerHandle` values. `ITransportConnection` and `ITransportListener` are not public types.
- **Complete Native Encapsulation**: Public headers expose zero OS socket descriptors (`SOCKET`, `int fd`), OS socket headers, TLS/OpenSSL contexts (`SSL*`), event loops, or third-party transport structures (`ENetHost`, `SteamNetworkingSockets`).
- **Optional Link and Composition**: Single-player games, asset tools, and offline CLI utilities compile and run without linking transport backends. Products can compose `NetworkTransportNull` or omit the network runtime entirely.
- **Host-Owned Injection**: The composition root instantiates a concrete transport and transfers unique ownership into `NetworkRuntime`. `NetworkRuntime` never links a concrete backend directly.
- **Thread Isolation & Zero I/O State Mutation**: Network I/O executes on dedicated background worker threads owned by the concrete transport that needs them. I/O threads never directly mutate Scene, ECS, Entity, Component, Editor, or Gameplay state.
- **Transport-Owned Cross-Thread Queues**: The transport owns inbound event and outbound send queues. `NetworkRuntime` observes inbound traffic only by calling `PollEvents()` on the simulation thread.
- **Dedicated Frame Schedule Phases**: Inbound transport events and session state updates are processed on the simulation/main thread during `FrameScheduler::Phase::NetworkPoll`. Outbound replication snapshots and RPCs are serialized and submitted during `FrameScheduler::Phase::NetworkFlush`. These phases do not depend on render command extraction.
- **Copy-On-Send Payloads**: `Send()` copies caller payload bytes before returning. The transport does not retain the caller-provided span.
- **Bounded Queues and Backpressure**: Inbound and outbound message queues enforce fixed capacity bounds and deterministic drop/rejection policies for overloaded connections.
- **Explicit Schemas & Negotiation**: Messages use typed schemas and handshake negotiation. Raw C++ memory layout, vtables, pointers, and unstructured object graphs are never serialized.
- **Deterministic Cancellation and Shutdown**: Every connection reaches a definitive terminal state. Disconnections, timeouts, cancellations, and engine shutdown follow bounded, leak-free teardown protocols.

## Target Topology and Module Ownership

The network subsystem is organized into four distinct CMake targets with strict compile-time boundaries:

```text
               +---------------------------+
               |   HoroEngine::Foundation  |
               +-------------+-------------+
                             ^
                             |
               +-------------+-------------+
               |   HoroEngine::NetworkApi  |
               +------+--------------+-----+
                      ^              ^
                      |              |
+---------------------+----+   +-----+-------------------------+
| HoroEngine::NetworkRuntime|   | Concrete Transports (Private) |
| (depends also on Runtime) |   | - NetworkTransportNull        |
+---------------------------+   | - NetworkTransportENet        |
                                +-------------------------------+
```

### 1. `HoroEngine::NetworkApi`

- **Role**: Backend-neutral public interface and data type definitions.
- **Direct Dependencies**: `HoroEngine::Foundation` only.
- **Public Header Location**: `include/Horo/Network/`
- **Exposed Contracts**:
  - Stable value types: `NetworkAddress`, `NetworkId`, `DeliveryPolicy`, `ChannelId`, `NetworkRole`.
  - Generation-checked opaque handles: `ConnectionHandle`, `ListenerHandle`.
  - Result and error types: `Result<T, NetworkErrorCode>`, `NetworkErrorCode`, `DisconnectReason`.
  - Message abstractions: `MessageView`, `ConstMessageSpan`, `IMessageSerializer`.
  - Transport interface and events: `INetworkTransport`, `ITransportEventConsumer`, `TransportEvent`, `TransportConnectionState`, `TransportCapabilities`.
  - Replication traits and property condition flags: `ReplicationTraits<T>`, `ReplicationCondition`.
- **Not Public**: `ITransportConnection`, `ITransportListener`, native peers, OS sockets, and backend queue types.

### 2. `HoroEngine::NetworkRuntime`

- **Role**: Session management, protocol negotiation, authentication, replication coordination, and simulation-thread dispatch.
- **Direct Dependencies**: `HoroEngine::NetworkApi`, `HoroEngine::Foundation`, `HoroEngine::Runtime`.
- **Does Not Own**: Transport I/O threads, native connection objects, or the cross-thread inbound/outbound queues. Those remain inside the injected transport.
- **Construction**: Receives `std::unique_ptr<INetworkTransport>` from the host composition root. The runtime destroys the transport only after `Shutdown()`.
- **Key Components**:
  - `NetworkRuntimeCoordinator`: Owns the injected transport and active sessions, and ticks during `NetworkPoll` / `NetworkFlush`.
  - `NetworkSession`: Application-facing session that starts after the transport reports `Connected`.
  - `SessionStateMachine`: Manages session transitions (`Created` -> `Negotiating` -> `Authenticating` -> `Active` -> `Closing` -> `Closed`).
  - `ReplicationManager`: Manages dirty property tracking, delta compression, interest management sets, and authoritative snapshot generation.
  - `MessageDispatcher`: Routes incoming RPCs and replication payloads to registered game handlers on the simulation thread.

### 3. `HoroEngine::NetworkTransportNull`

- **Role**: Deterministic in-memory loopback and null transport backend.
- **Direct Dependencies**: `HoroEngine::NetworkApi`, `HoroEngine::Foundation`.
- **Must Not Depend On**: `HoroEngine::Platform`, OS sockets, or a network I/O thread.
- **Purpose**: Unit testing, CI verification, single-player offline simulation, and synthetic latency/loss testing harnesses. `PollEvents()` and `Send()` run on the caller thread against in-memory queues.

### 4. `HoroEngine::NetworkTransportENet`

- **Role**: High-performance reliable and unreliable UDP transport implementation.
- **Direct Dependencies**: `HoroEngine::NetworkApi`, `HoroEngine::Foundation`, `HoroEngine::Platform`.
- **Encapsulation**: Links third-party ENet and platform socket libraries strictly as `PRIVATE`. No third-party headers or socket types leak into public includes.
- **Owns**: I/O thread, native ENet/socket state, inbound event queue, and outbound send queue.

## Public Header Encapsulation

To maintain portability, compile speed, and memory safety, public headers under `include/Horo/` enforce complete encapsulation:

1. **No Socket Headers**: Headers such as `<sys/socket.h>`, `<netinet/in.h>`, `<arpa/inet.h>`, `<winsock2.h>`, and `<ws2tcpip.h>` are forbidden in public headers.
2. **No Native Sockets / Handles**: Sockets are encapsulated as opaque integer handles or private members within backend `.cpp` files. Public APIs operate exclusively on `ConnectionHandle` and `ListenerHandle`.
3. **No TLS / Crypto Contexts**: OpenSSL, mbedTLS, or native TLS context pointers (`SSL*`, `SSL_CTX*`) never appear in public interfaces.
4. **No Native Event Loops**: Event loop primitives (libuv `uv_loop_t`, epoll file descriptors, kqueue, IOCP handles) remain private to backend implementations.
5. **No Third-Party Types**: `ENetHost`, `ENetPeer`, `ENetPacket`, or Steam networking structs are strictly confined to concrete backend sources.

## Host Composition and Transport Ownership

The host composition root selects and instantiates the backend. `NetworkRuntime` never links `NetworkTransportNull` or `NetworkTransportENet`.

```cpp
auto transport = CreateENetTransport(config); // or CreateNullTransport(config)
NetworkRuntime runtime({
    .transport = std::move(transport),
});
```

Ownership rules:

- The host transfers unique ownership of `INetworkTransport` into `NetworkRuntime`.
- Shared ownership is forbidden. The runtime is the sole owner after injection.
- The host must not call `PollEvents`, `Send`, `Close`, or `Shutdown` on a transport after transferring it.
- `NetworkRuntime::Shutdown()` shuts down the transport, joins any I/O thread the backend owns, then destroys the `unique_ptr`.

## Capability Interface

```cpp
namespace Horo::Network {

struct ConnectRequest {
    NetworkAddress endpoint;
    std::chrono::milliseconds timeout{5000};
    uint32_t channelCount{2};
    CancellationToken cancellation{};
};

struct ListenRequest {
    NetworkAddress bindAddress;
    uint32_t maxConnections{32};
    uint32_t channelCount{2};
};

class INetworkTransport {
public:
    virtual ~INetworkTransport() = default;

    virtual Result<void> Initialize(const TransportConfig& config) = 0;
    virtual Result<void> Shutdown() = 0;

    virtual Result<ListenerHandle> Listen(const ListenRequest& request) = 0;
    virtual Result<ConnectionHandle> Connect(const ConnectRequest& request) = 0;
    virtual Result<void> Send(ConnectionHandle handle, ChannelId channel, ConstMessageSpan payload, DeliveryPolicy policy) = 0;
    virtual Result<void> Close(ConnectionHandle handle, DisconnectReason reason) = 0;

    virtual void PollEvents(ITransportEventConsumer& consumer) = 0;
};

} // namespace Horo::Network
```

Handles are 64-bit generation-checked identifiers preventing ABA handle reuse issues.

### Connect and Listen Semantics

`Connect()` is asynchronous:

- After request validation, `Connect()` allocates a `ConnectionHandle` and returns it immediately.
- The returned connection is `Created`, or `Resolving` when `endpoint` contains a hostname.
- DNS, socket connect, and transport handshake continue in the backend. They must not block the caller.
- State changes and inbound packets are reported later through `PollEvents()`.
- A successful `Connect()` return is not a connected session and is not an authenticated `NetworkSession`.

`Listen()` likewise returns a `ListenerHandle` immediately after bind-request validation. Accept events arrive through `PollEvents()`.

### Payload Lifetime

- `Send()` copies the caller-provided `ConstMessageSpan` before returning. The transport does not retain that span.
- The caller may destroy or reuse the source buffer as soon as `Send()` returns.
- `MessageView` values delivered to `ITransportEventConsumer` are valid only for the duration of that callback. Consumers that retain payload bytes must copy them.

### Handle Lifetime

| Call | Result |
|---|---|
| `Connect()` / `Listen()` validation failure | No handle is issued; typed `NetworkErrorCode` |
| `Send` / `Close` with never-issued or generation-mismatched handle | `NetworkErrorCode::InvalidHandle` |
| `Send` on a handle whose connection is `Closing`, `Closed`, or `Failed` | `NetworkErrorCode::ConnectionClosed` |
| `Close` on an already `Closed` or `Failed` handle with matching generation | Success (idempotent) |
| `Close` on `InvalidHandle` | `NetworkErrorCode::InvalidHandle` |

A handle remains generation-valid until the transport reclaims the slot. After reclaim, the same bit pattern is `InvalidHandle`.

## NetworkAddress

`NetworkAddress` is a first-class endpoint value. It must represent:

- IPv4 literals: `127.0.0.1:7777`
- IPv6 literals: `[::1]:7777`
- Hostnames: `game.example.com:7777`

Literal addresses skip `Resolving`. Hostname endpoints require asynchronous DNS as part of the transport connection lifecycle. Resolution failure transitions the transport connection to `Failed` with `NetworkErrorCode::NameResolutionFailed`.

Public `NetworkAddress` stores host text or numeric form plus port. It never exposes `sockaddr`, `addrinfo`, or other OS types.

## Transport and Session Lifecycles

Transport connection state and session state are distinct machines. Authentication is not a transport state.

```text
TransportConnection
  Created
    -> Resolving          // hostname only
    -> Connecting
    -> Connected
    -> Closing
    -> Closed

  Any non-terminal state -> Failed
```

```text
NetworkSession
  Created                 // allocated when transport reaches Connected
    -> Negotiating        // protocol version, channels, capabilities
    -> Authenticating     // runtime/session token validation
    -> Active
    -> Closing
    -> Closed

  Any non-terminal state -> Failed
```

- **Transport `Created`**: Handle allocated; request validated. `Connect()` has already returned.
- **Transport `Resolving`**: Asynchronous DNS for a hostname endpoint.
- **Transport `Connecting`**: Native connect and transport-level handshake (for example ENet peer connect). No application token is exchanged here.
- **Transport `Connected`**: Packets can flow. `NetworkRuntime` now creates a `NetworkSession`.
- **Session `Negotiating`**: Schema/protocol version, compression, and MTU agreement.
- **Session `Authenticating`**: `NetworkRuntime` validates application credentials. Tokens are runtime/session data, not `ConnectRequest` fields on the transport.
- **Session `Active`**: Gameplay messages, RPCs, and replication are admitted.
- **Closing / Closed / Failed**: Each layer tears down independently. Session close requests transport `Close()`. Transport `Failed` fails the associated session.

Future transports (SteamNetworkingSockets, WebRTC, and similar) replace only the transport machine. They must not absorb session authentication.

## Queue Ownership

Model A is normative:

```text
Transport I/O thread (ENet) or caller thread (Null)
    -> transport-owned inbound event queue
    -> NetworkRuntime calls transport.PollEvents(consumer)

NetworkRuntime NetworkFlush
    -> transport.Send(...)            // copies into transport-owned outbound queue
    -> transport I/O thread sends
```

The transport owns:

- I/O thread, when the backend needs one
- raw/native connection state
- inbound event queue
- outbound send queue

`NetworkRuntime` does not know the queue's mutex, lock-free, or ring-buffer implementation. The architecture requires a bounded thread-safe queue. Spinlock versus mutex is a backend implementation choice, not a public contract.

`PollEvents()` may be called only on the simulation/main thread during `NetworkPoll`. It drains the transport-owned inbound queue into `ITransportEventConsumer` callbacks. Those callbacks must not block, allocate unboundedly, or re-enter the transport.

## Threading Model and Frame Schedule Phases

To prevent race conditions and frame stalls, network operations are strictly partitioned:

```text
[ Background I/O Worker Thread ]          // ENet only; Null has none
  OS Socket Poll -> Recv Packets -> Framing
         |
         v
  +--------------------------------------+
  | Transport-owned inbound event queue  |
  | (bounded, thread-safe)               |
  +--------------------------------------+
         |
         v  NetworkRuntime::PollEvents during NetworkPoll
[ Simulation / Main Engine Thread ]
  Frame Phase: NetworkPoll
    - transport.PollEvents(consumer)
    - advance TransportConnection states
    - create/advance NetworkSession (negotiate, authenticate)
    - dispatch Active-session messages to ReplicationManager / gameplay
  Frame Phase: Simulation & Gameplay Update
  Frame Phase: NetworkFlush
    - ReplicationManager collects dirty properties
    - serialize outbound snapshots and RPCs
    - transport.Send(...) for Active sessions
  Frame Phase: RenderExtraction
  Frame Phase: Render
         |
         v
  +--------------------------------------+
  | Transport-owned outbound send queue  |
  | (bounded, thread-safe)               |
  +--------------------------------------+
         |
         v
[ Background I/O Worker Thread ]
  Packetize -> OS Socket Send
```

### Dedicated Frame Scheduler Phases

1. `FrameScheduler::Phase::NetworkPoll`:
   - Runs before scene simulation and physics update.
   - Drains transport events and advances session state on the simulation thread.
2. `FrameScheduler::Phase::NetworkFlush`:
   - Runs after scene simulation and before render command extraction.
   - Submits outbound packets from post-simulation dirty state.
   - Has no dependency on render extraction. Flush is ordered before extract so replication latency is not coupled to rendering work.

Allowed adjacent orders are `NetworkPoll -> Simulation -> NetworkFlush -> RenderExtraction -> Render`. `NetworkFlush` after `RenderExtraction` is forbidden as the default because it adds render-extract latency to the network path without a transport requirement.

## Message Schema and Protocol Negotiation

Messages consist of:

- 16-bit Protocol Identifier and 16-bit Schema Version.
- 16-bit Message Type ID.
- 32-bit Sequence / Acknowledgement Numbers.
- Bounded payload bytes with validated length headers.

`NetworkRuntime` performs handshake negotiation after the transport reports `Connected`:

- Minimum and maximum supported protocol versions.
- Supported compression algorithms and MTU payload limits.
- Authentication credentials and capability flags.

Mismatched protocol versions or invalid authentication tokens fail the **session** with `NetworkErrorCode::IncompatibleProtocol` or `NetworkErrorCode::AuthenticationFailed`. The transport connection is then closed. Transport backends must not interpret authentication tokens.

## Delivery Semantics and Backpressure

Delivery policies:

- **UnreliableUnordered**: Fast state updates (e.g. high-frequency physics/transform telemetry); latest packet overwrites previous.
- **UnreliableSequenced**: Discards out-of-order packets; only newer packets are accepted.
- **ReliableOrdered**: Guaranteed delivery in order (e.g., game events, inventory actions, chat).
- **ReliableUnordered**: Guaranteed delivery without strict ordering constraints.

### Backpressure and Overload Policies

All transport queues have bounded capacities:

- **DropOldestSnapshot**: When the outbound queue for unreliable state exceeds threshold, older snapshots are discarded in favor of the latest state.
- **RejectSend**: Reliable message requests exceeding queue limits return `NetworkErrorCode::QueueFull`.
- **DisconnectAbusivePeer**: Inbound queues experiencing sustained flood without consumption trigger connection termination.

## Deterministic Cancellation and Shutdown

- **Cancellation**: Connect and resolve requests accept `CancellationToken`. Triggering cancellation immediately halts DNS lookups and connection attempts. The transport connection moves to `Failed` with `NetworkErrorCode::OperationCancelled`, reported through `PollEvents()`.
- **Graceful Teardown**: Session close asks the transport to `Close()`. The transport transmits a disconnect frame and starts a bounded drain timer. Upon expiry or peer ACK, native resources are reclaimed.
- **Process Shutdown Sequence**:
  1. The host calls `NetworkRuntime::Shutdown()`.
  2. Runtime fails/closes all sessions and stops admitting gameplay messages.
  3. Runtime calls `INetworkTransport::Shutdown()`: listeners stop accepting, connections send disconnect notices, I/O threads stop and join with a bounded timeout.
  4. Runtime destroys the unique transport.
  5. Session pools and dispatcher tables are freed.

## Optional Composition and Product Configurations

Horo Engine products compose networking according to their needs:

| Configuration | Composed Targets | Behavior |
|---|---|---|
| **Editor / IDE** (`HoroEditor`) | `NetworkApi`, `NetworkRuntime`, `NetworkTransportENet` | Full multiplayer preview, network debugger panel, local test servers. |
| **Dedicated Server** (`horo-engine server`) | `NetworkApi`, `NetworkRuntime`, `NetworkTransportENet` | Headless execution, high-tick simulation, no rendering or audio dependencies. |
| **Offline Game Client** | `NetworkApi`, `NetworkRuntime`, `NetworkTransportNull` | Uses replication/session API locally; zero socket operations, Platform sockets, or I/O threads. |
| **Tooling / Packager** (`horopak`) | *None* | Links zero network targets; compiles cleanly with minimal footprint. |

## Observability and Diagnostics

Networking integrates with Horo's diagnostic and metric infrastructure:

- **Counters**: `net.bytes_sent`, `net.bytes_received`, `net.packets_lost`, `net.packets_dropped`.
- **Gauges**: `net.active_connections`, `net.inbound_queue_depth`, `net.outbound_queue_depth`, `net.rtt_ms`.
- **Tracing**: Transport connection events and session handshakes log to the `LogCategory::Network` category. Payloads are scrubbed of sensitive data by default.

## Testing and Verification Strategy

The networking subsystem requires targeted automated verification:

1. **Unit Tests (`tests/unit/runtime/networking/`)**:
   - `NetworkAddressTests`: IPv4, IPv6, hostname, loopback, port parsing, serialization.
   - `TransportConnectionStateTests`: `Connect()` returns immediately; `Created` / `Resolving` / `Connecting` / `Connected`; invalid transitions; timeout.
   - `SessionStateMachineTests`: `Negotiating` / `Authenticating` / `Active`; auth failure does not require transport-level auth.
   - `NetworkHandleTests`: stale handle -> `InvalidHandle`; send-after-close -> `ConnectionClosed`; duplicate `Close` is idempotent.
   - `SendPayloadLifetimeTests`: caller buffer may be overwritten after `Send()` returns.
   - `ReplicationManagerTests`: Dirty property extraction, delta compression, interest management queries.
2. **Deterministic Transport Tests (`NetworkTransportNullTests`)**:
   - No `Platform` link; no OS sockets; no I/O thread.
   - Simulated latency, jitter, packet loss, duplicate packets, and out-of-order delivery.
   - `PollEvents()` drains the transport-owned in-memory queue on the caller thread.
   - Graceful disconnect and timeout handling.
3. **Integration & Lifecycle Tests**:
   - Full client-server connect, session authenticate, transfer, and disconnect sequences.
   - Cancellation during DNS and connect; `Connect()` already returned a handle.
   - Unique-ptr injection: runtime shutdown destroys the transport exactly once.
   - Process shutdown with active connections and pending queued messages without hangs or memory leaks.

## Related Documents

- [ADR-020: Network Target Ownership and Dependency Boundary](../../adr/020-network-target-ownership-and-dependency-boundary.md)
- [System Design](../foundation/system-design.md)
- [Desired Project Trees](../desired-project-tree.md)
- [Multiplayer Replication Architecture](./multiplayer-replication-architecture.md)
- [Runtime Lifecycle](./runtime-lifecycle.md)
- [Concurrency And Job System](../foundation/concurrency-and-jobs.md)
- [Network Debugger UI Reference](./network-debugger.html)
