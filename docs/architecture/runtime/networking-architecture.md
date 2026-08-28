# Networking Architecture

## Purpose

This document defines the optional network transport, connection lifecycle,
message serialization, asynchronous I/O, security boundaries, runtime
integration, and observability for Horo Engine.

Networking is an optional engine capability. Hosts, tools, and games that do not need it
do not construct, link, or activate concrete transport backends.

## Core Decisions

- **Strict Layered Architecture**: Networking is separated into target-level tiers: public contracts and types (`HoroEngine::NetworkApi`), session coordination and replication runtime (`HoroEngine::NetworkRuntime`), and private concrete transport backends (`HoroEngine::NetworkTransportNull`, `HoroEngine::NetworkTransportENet`).
- **Complete Native Encapsulation**: Public headers expose zero OS socket descriptors (`SOCKET`, `int fd`), OS socket headers, TLS/OpenSSL contexts (`SSL*`), event loops, or third-party transport structures (`ENetHost`, `SteamNetworkingSockets`).
- **Optional Link and Composition**: Single-player games, asset tools, and offline CLI utilities compile and run without linking transport backends. Products can compose `NetworkTransportNull` or omit the network runtime entirely.
- **Thread Isolation & Zero I/O State Mutation**: Network I/O executes on dedicated background worker threads. I/O threads never directly mutate Scene, ECS, Entity, Component, Editor, or Gameplay state.
- **Dedicated Frame Schedule Phases**: Inbound messages and connection state updates are processed on the simulation/main thread during `FrameScheduler::Phase::NetworkPoll`. Outbound replication snapshots and RPCs are serialized and queued during `FrameScheduler::Phase::NetworkFlush`.
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
  - Transport interfaces: `INetworkTransport`, `ITransportListener`, `ITransportConnection`, `TransportCapabilities`.
  - Replication traits and property condition flags: `ReplicationTraits<T>`, `ReplicationCondition`.

### 2. `HoroEngine::NetworkRuntime`

- **Role**: State session management, replication coordination, message dispatching, and queue management.
- **Direct Dependencies**: `HoroEngine::NetworkApi`, `HoroEngine::Foundation`, `HoroEngine::Runtime`.
- **Key Components**:
  - `NetworkRuntimeCoordinator`: Owns active sessions, ties into the `FrameScheduler`, and coordinates transports.
  - `ConnectionStateMachine`: Manages deterministic peer transitions (`Created` -> `Connecting` -> `Authenticating` -> `Active` -> `Closing` -> `Closed`).
  - `ReplicationManager`: Manages dirty property tracking, delta compression, interest management sets, and authoritative snapshot generation.
  - `MessageDispatcher`: Routes incoming RPCs and replication payloads to registered game handlers on the simulation thread.
  - `BoundedMessageQueue`: Thread-safe FIFO ring buffers with explicit overload and snapshot-replacement policies.

### 3. `HoroEngine::NetworkTransportNull`

- **Role**: Deterministic in-memory loopback and null transport backend.
- **Direct Dependencies**: `HoroEngine::NetworkApi`, `HoroEngine::Foundation`.
- **Purpose**: Unit testing, CI verification, single-player offline simulation, and synthetic latency/loss testing harnesses. Does not open OS sockets or spawn network I/O threads.

### 4. `HoroEngine::NetworkTransportENet`

- **Role**: High-performance reliable and unreliable UDP transport implementation.
- **Direct Dependencies**: `HoroEngine::NetworkApi`, `HoroEngine::Foundation` (with private ENet / socket links).
- **Encapsulation**: Links third-party ENet and platform socket libraries strictly as `PRIVATE`. No third-party headers or socket types leak into public includes.

## Public Header Encapsulation

To maintain portability, compile speed, and memory safety, public headers under `include/Horo/` enforce complete encapsulation:

1. **No Socket Headers**: Headers such as `<sys/socket.h>`, `<netinet/in.h>`, `<arpa/inet.h>`, `<winsock2.h>`, and `<ws2tcpip.h>` are forbidden in public headers.
2. **No Native Sockets / Handles**: Sockets are encapsulated as opaque integer handles or private members within backend `.cpp` files. Public APIs operate exclusively on `ConnectionHandle` and `ListenerHandle`.
3. **No TLS / Crypto Contexts**: OpenSSL, mbedTLS, or native TLS context pointers (`SSL*`, `SSL_CTX*`) never appear in public interfaces.
4. **No Native Event Loops**: Event loop primitives (libuv `uv_loop_t`, epoll file descriptors, kqueue, IOCP handles) remain private to backend implementations.
5. **No Third-Party Types**: `ENetHost`, `ENetPeer`, `ENetPacket`, or Steam networking structs are strictly confined to concrete backend sources.

## Capability Interface

```cpp
namespace Horo::Network {

struct ConnectRequest {
    NetworkAddress endpoint;
    std::chrono::milliseconds timeout{5000};
    uint32_t channelCount{2};
    std::span<const uint8_t> authenticationToken;
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

- **Created**: Handle allocated; request validated.
- **Resolving**: Asynchronous DNS / endpoint resolution in progress.
- **Connecting**: Transport-level handshake in progress.
- **Authenticating**: Security token validation and protocol version negotiation.
- **Active**: Fully established session exchanging application messages.
- **Closing**: Graceful disconnect notification sent; awaiting bounded peer acknowledgement or timeout.
- **Closed**: Connection teardown complete; socket and memory reclaimed.
- **Failed**: Network failure, timeout, auth rejection, or protocol error.

Every accepted connection reaches exactly one terminal state (`Closed` or `Failed`).

## Threading Model and Frame Schedule Phases

To prevent race conditions and frame stalls, network operations are strictly partitioned:

```text
[ Background I/O Worker Thread ]
  OS Socket Poll -> Recv Packets -> Decrypt & Validate -> Framing
         |
         v (Enqueues framed messages)
  +-------------------------------------------------------------+
  | Inbound Bounded Queue (Thread-Safe Ring Buffer / Spinlock)  |
  +-------------------------------------------------------------+
         |
         v (Drained during NetworkPoll phase)
[ Simulation / Main Engine Thread ]
  Frame Phase: NetworkPoll
    - Drain inbound queue
    - Update ConnectionStateMachine transitions
    - Dispatch received RPCs and snapshots to Gameplay/Scene/ReplicationManager
  Frame Phase: Simulation & Gameplay Update (Fixed / Variable Tick)
  Frame Phase: NetworkFlush
    - ReplicationManager collects dirty properties
    - Serialize outbound state snapshots & client RPCs
    - Push outbound messages to queue
         |
         v
  +-------------------------------------------------------------+
  | Outbound Bounded Queue (Thread-Safe Ring Buffer / Spinlock) |
  +-------------------------------------------------------------+
         |
         v (Drained by I/O thread)
[ Background I/O Worker Thread ]
  Encrypt & Packetize -> OS Socket Send
```

### Dedicated Frame Scheduler Phases

1. `FrameScheduler::Phase::NetworkPoll`:
   - Runs prior to scene simulation and physics update.
   - Drains inbound transport queues into typed message dispatches.
   - Dispatches connection state continuations onto the main thread.
2. `FrameScheduler::Phase::NetworkFlush`:
   - Runs after scene simulation and render command extraction.
   - Gathers replicated component changes, builds delta frames, and queues outbound packets to background I/O workers.

## Message Schema and Protocol Negotiation

Messages consist of:

- 16-bit Protocol Identifier and 16-bit Schema Version.
- 16-bit Message Type ID.
- 32-bit Sequence / Acknowledgement Numbers.
- Bounded payload bytes with validated length headers.

Peers perform handshake negotiation upon initial connection:

- Minimum and maximum supported protocol versions.
- Supported compression algorithms and MTU payload limits.
- Authentication credentials and capability flags.

Mismatched protocol versions or invalid authentication tokens immediately reject the connection with a typed `NetworkErrorCode::IncompatibleProtocol` or `NetworkErrorCode::AuthenticationFailed`.

## Delivery Semantics and Backpressure

Delivery policies:

- **UnreliableUnordered**: Fast state updates (e.g. high-frequency physics/transform telemetry); latest packet overwrites previous.
- **UnreliableSequenced**: Discards out-of-order packets; only newer packets are accepted.
- **ReliableOrdered**: Guaranteed delivery in order (e.g., game events, inventory actions, chat).
- **ReliableUnordered**: Guaranteed delivery without strict ordering constraints.

### Backpressure and Overload Policies

All internal queues have bounded capacities:

- **DropOldestSnapshot**: When the outbound queue for unreliable state exceeds threshold, older snapshots are discarded in favor of the latest state.
- **RejectSend**: Reliable message requests exceeding queue limits return `NetworkErrorCode::QueueFull`.
- **DisconnectAbusivePeer**: Inbound queues experiencing sustained flood without consumption trigger connection termination.

## Deterministic Cancellation and Shutdown

- **Cancellation**: Connect and resolve requests accept `CancellationToken`. Triggering cancellation immediately halts DNS lookups and connection attempts, returning `NetworkErrorCode::OperationCancelled`.
- **Graceful Teardown**: When closing a connection, the runtime transmits a disconnect frame and initiates a bounded drain timer (e.g., 200 ms). Upon expiry or peer ACK, the socket closes cleanly.
- **Process Shutdown Sequence**:
  1. `NetworkRuntime::Shutdown()` is invoked by the host composition root.
  2. All listener sockets are immediately closed to stop incoming connections.
  3. Active connections are sent disconnect notices.
  4. Background I/O threads are signaled to stop and joined with a bounded timeout.
  5. Transport memory, socket buffers, and session pools are freed.

## Optional Composition and Product Configurations

Horo Engine products compose networking according to their needs:

| Configuration | Composed Targets | Behavior |
|---|---|---|
| **Editor / IDE** (`HoroEditor`) | `NetworkApi`, `NetworkRuntime`, `NetworkTransportENet` | Full multiplayer preview, network debugger panel, local test servers. |
| **Dedicated Server** (`horo-engine server`) | `NetworkApi`, `NetworkRuntime`, `NetworkTransportENet` | Headless execution, high-tick simulation, no rendering or audio dependencies. |
| **Offline Game Client** | `NetworkApi`, `NetworkRuntime`, `NetworkTransportNull` | Uses replication/session API locally; zero socket operations or I/O threads. |
| **Tooling / Packager** (`horopak`) | *None* | Links zero network targets; compiles cleanly with minimal footprint. |

## Observability and Diagnostics

Networking integrates with Horo's diagnostic and metric infrastructure:

- **Counters**: `net.bytes_sent`, `net.bytes_received`, `net.packets_lost`, `net.packets_dropped`.
- **Gauges**: `net.active_connections`, `net.inbound_queue_depth`, `net.outbound_queue_depth`, `net.rtt_ms`.
- **Tracing**: Connection lifecycle events and session handshakes log to the `LogCategory::Network` category. Payloads are scrubbed of sensitive data by default.

## Testing and Verification Strategy

The networking subsystem requires targeted automated verification:

1. **Unit Tests (`tests/unit/runtime/networking/`)**:
   - `NetworkAddressTests`: IPv4, IPv6, loopback, port parsing, serialization.
   - `ConnectionStateMachineTests`: State transitions, invalid transitions, timeout handling.
   - `BoundedQueueTests`: Capacity enforcement, FIFO ordering, snapshot replacement policy.
   - `ReplicationManagerTests`: Dirty property extraction, delta compression, interest management queries.
2. **Deterministic Transport Tests (`NetworkTransportNullTests`)**:
   - Simulated latency, jitter, packet loss, duplicate packets, and out-of-order delivery.
   - Handshake negotiation and rejection of unsupported protocol versions.
   - Graceful disconnect and timeout handling.
3. **Integration & Lifecycle Tests**:
   - Full client-server connect, transfer, and disconnect sequences.
   - Cancellation during active connection attempts.
   - Process shutdown with active connections and pending queued messages without hangs or memory leaks.

## Related Documents

- [ADR-020: Network Target Ownership and Dependency Boundary](../../adr/020-network-target-ownership-and-dependency-boundary.md)
- [System Design](../foundation/system-design.md)
- [Desired Project Trees](../desired-project-tree.md)
- [Multiplayer Replication Architecture](./multiplayer-replication-architecture.md)
- [Runtime Lifecycle](./runtime-lifecycle.md)
- [Concurrency And Job System](../foundation/concurrency-and-jobs.md)
- [Network Debugger UI Reference](./network-debugger.html)
