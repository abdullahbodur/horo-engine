# ADR-020: Network Target Ownership and Dependency Boundary

- **Status**: Proposed
- **Date**: 2026-08-28
- **Supersedes**: None
- **Scope**: Network target topology, compile-time dependency directions, public API encapsulation, optional linking and composition, threading model, and deterministic lifecycle/shutdown.
- **Issue**: [#1098](https://github.com/abdullahbodur/horo-engine/issues/1098) ([NET-001.1])
- **JIRA**: HORO-1098
- **Normative documents**:
  - [Networking Architecture](../architecture/runtime/networking-architecture.md)
  - [System Design](../architecture/foundation/system-design.md)
  - [Desired Project Trees](../architecture/desired-project-tree.md)
  - [Multiplayer Replication Architecture](../architecture/runtime/multiplayer-replication-architecture.md)

## Context

Horo Engine is a modular C++20 game engine and editor IDE. Real-time multiplayer and networked services require low-latency, reliable and unreliable packet transmission, session negotiation, state replication, and RPC mechanisms. However, networking is not required by every game, tool, or runtime composition (e.g. offline single-player games, offline command-line tooling, asset baking, or isolated simulation testing).

In addition, networking implementations typically depend on platform-specific socket APIs (e.g., Berkeley sockets, Windows Sockets 2) and third-party transport/cryptography libraries (e.g., ENet, OpenSSL, libuv, WebRTC, SteamNetworkingSockets). Exposing raw socket descriptors, native event loops, or third-party headers to gameplay or editor code causes header pollution, fragile platform coupling, undefined concurrency behavior, and ABI instability.

`docs/architecture/foundation/system-design.md` and `docs/architecture/runtime/networking-architecture.md` outline a layered design. Before implementation of [NET-001] proceeds, this ADR ratifies the exact CMake target boundaries, public/private header ownership, optional composition rules, frame scheduling phases, and lifecycle invariants.

## Decision

**The network subsystem is structured into four distinct target tiers with strict compile-time boundaries: `HoroEngine::NetworkApi` (public backend-neutral interfaces and value types), `HoroEngine::NetworkRuntime` (session, authentication, and replication runtime), and private concrete transports (`HoroEngine::NetworkTransportENet`, `HoroEngine::NetworkTransportNull`). `INetworkTransport` is a handle-based packet-transport abstraction; protocol negotiation and authentication belong to `NetworkRuntime`. The host injects a unique `INetworkTransport` into the runtime. Public headers strictly encapsulate all native socket descriptors, TLS contexts, event loops, and third-party transport types. Networking is fully optional; applications and tools may link without network targets or compose `NetworkTransportNull`. Transports own cross-thread queues. Runtime observes inbound events only through `PollEvents()` during `NetworkPoll`, and submits outbound payloads during `NetworkFlush`.**

### 1. Target Topology and Allowed Dependencies

The network subsystem is decomposed into four discrete CMake targets:

| Target | Role | Allowed Direct First-Party Dependencies | Public Surface |
|---|---|---|---|
| `HoroEngine::NetworkApi` | Backend-neutral public types, handles, traits, interfaces | `HoroEngine::Foundation` | Value types (`NetworkId`, `NetworkAddress`, `DeliveryPolicy`), generation handles (`ConnectionHandle`, `ListenerHandle`), error codes, message views, `INetworkTransport`, `TransportEvent`, replication traits |
| `HoroEngine::NetworkRuntime` | Session management, protocol negotiation, authentication, replication | `HoroEngine::NetworkApi`, `HoroEngine::Foundation`, `HoroEngine::Runtime` | `NetworkRuntimeCoordinator`, `NetworkSession`, `SessionStateMachine`, `ReplicationManager`. Owns the injected `unique_ptr<INetworkTransport>` |
| `HoroEngine::NetworkTransportNull` | Deterministic in-memory/loopback/null transport | `HoroEngine::NetworkApi`, `HoroEngine::Foundation` | `NullTransportBackend` factory / descriptor for testing, headless simulation, and network-disabled fallback. No Platform, sockets, or I/O thread |
| `HoroEngine::NetworkTransportENet` | Concrete UDP transport implementation | `HoroEngine::NetworkApi`, `HoroEngine::Foundation`, `HoroEngine::Platform` | `ENetTransportBackend` factory / descriptor. Owns I/O thread and queues. All ENet headers, sockets, and platform dependencies are `PRIVATE` |

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

Rules:

1. `HoroEngine::NetworkApi` depends **only** on `HoroEngine::Foundation`. It contains zero runtime logic, background threads, or socket code.
2. `HoroEngine::NetworkRuntime` depends on `NetworkApi`, `Foundation`, and `Runtime`. It never links a concrete transport backend directly. The composition root instantiates the backend and transfers `std::unique_ptr<INetworkTransport>` into the runtime.
3. `NetworkTransportNull` depends only on `NetworkApi` and `Foundation`. It does not depend on `Platform`, open OS sockets, or create I/O threads. `NetworkTransportENet` depends on `NetworkApi`, `Foundation`, and `Platform` (plus private third-party libraries). Transports do not depend on `NetworkRuntime` or `HoroEngine::Runtime`.
4. Feature modules, gameplay modules, and editor panels consume `NetworkApi` and `NetworkRuntime` interfaces; they **never** depend on concrete transport targets.
5. Public `NetworkApi` exposes handle-based `INetworkTransport` only. `ITransportConnection` and `ITransportListener` are not public types.

### 2. Encapsulation and Native Type Shielding

Public headers under `include/Horo/Network/` and `include/Horo/Runtime/` MUST NOT include or expose:

- OS socket headers (`<sys/socket.h>`, `<netinet/in.h>`, `<arpa/inet.h>`, `<winsock2.h>`, `<ws2tcpip.h>`)
- Native socket descriptors or OS handles (`SOCKET`, `int fd`, file descriptors)
- Cryptography / TLS context types (`SSL*`, `SSL_CTX*`, OpenSSL / mbedTLS headers)
- Event loop types (libuv `uv_loop_t`, epoll, kqueue, IOCP handles)
- Third-party transport types (`ENetHost`, `ENetPeer`, `ENetPacket`, `SteamNetworkingSockets`, etc.)

All OS and third-party types remain strictly internal to the private compilation units (`src/runtime/networking/backends/...`) and are encapsulated via interface implementations or Pimpl idioms. Addresses are represented by `Horo::Network::NetworkAddress` value types; connections and listeners are referenced by generation-checked `ConnectionHandle` and `ListenerHandle` identifiers.

### 3. Optional Link and Host Composition

Networking is an optional engine capability.

- **Offline / Non-Networked Products**: Products (such as single-player offline games, command-line utilities like `horopak`, asset compilers, or headless tools) do not link `NetworkRuntime` or concrete transports. Core engine composition, ECS, asset loading, and renderer pipelines operate normally without networking libraries present in the link graph.
- **Single-Player / Headless Simulation**: If a game project uses replication interfaces or network components locally, the host composition root injects `HoroEngine::NetworkTransportNull`. `NetworkTransportNull` simulates loopback transport in memory without opening OS sockets or creating network I/O threads.
- **Multiplayer Desktop & Dedicated Server**: Composition roots (`HoroEditor`, `horo-engine server`, game client) explicitly instantiate the chosen transport (`NetworkTransportENet` or platform backend) during startup and transfer unique ownership into `NetworkRuntime`. No static discovery, shared_ptr sharing, or global registry side effects are allowed.

### 4. Threading Model and Frame Lifecycle

Network processing is cleanly partitioned between asynchronous I/O and frame-synchronized gameplay execution:

```text
[ Background I/O Thread ]                 // ENet; Null has none
  Socket poll / recv / frame
         |
         v
  +--------------------------------------+
  | Transport-owned inbound event queue  |
  | (bounded, thread-safe)               |
  +--------------------------------------+
         |
         v transport.PollEvents() during NetworkPoll
[ Simulation / Game Main Thread ]
  1. FrameScheduler -> NetworkPoll:
     - Drain transport events
     - Advance transport connection state
     - Create/advance NetworkSession (negotiate, authenticate)
     - Dispatch Active-session messages
  2. FrameScheduler -> Simulation / Physics / Behaviors
  3. FrameScheduler -> NetworkFlush:
     - Collect dirty replicated properties
     - Serialize outbound snapshots and RPCs
     - transport.Send() copies into the outbound queue
  4. FrameScheduler -> RenderExtraction / Render
         |
         v
  +--------------------------------------+
  | Transport-owned outbound send queue  |
  | (bounded, thread-safe)               |
  +--------------------------------------+
         |
         v
[ Background I/O Thread ]
```

The transport owns the inbound event queue and outbound send queue. `NetworkRuntime` does not implement transport-specific synchronization. Queue implementation (mutex, ring buffer, or otherwise) is a backend detail; the contract is a bounded thread-safe queue.

Key concurrency and lifecycle rules:

- **Zero I/O Mutation**: Network I/O threads MUST NEVER mutate Scene, ECS, Entity, Component, Editor, or Gameplay state directly.
- **Async Connect**: `INetworkTransport::Connect()` validates the request, allocates a generation-checked handle, and returns immediately. Establishment (`Resolving` / `Connecting` / `Connected`) is reported through `PollEvents()`. Callers must not treat `Connect()` as a blocking connected-session API.
- **Copy-On-Send**: `Send()` copies payload bytes before returning and does not retain the caller span.
- **Dedicated Frame Phases**: Frame execution includes explicit `NetworkPoll` (pre-simulation) and `NetworkFlush` (post-simulation, before render extraction) stages in `FrameScheduler`. `NetworkFlush` does not depend on render command extraction.
- **Bounded Queues & Backpressure**: Transport queues have strict, configurable capacity limits. Queue overflow triggers explicit message-class policies (e.g. drop oldest unreliable state snapshot, reject send, or disconnect failing peer) rather than unbounded heap growth.
- **Split Lifecycles**: Transport states are `Created -> Resolving -> Connecting -> Connected -> Closing -> Closed`. Session states are `Created -> Negotiating -> Authenticating -> Active -> Closing -> Closed`. Authentication is a runtime/session concern, not a transport backend concern.
- **Continuations**: Transport and session state transitions are observed on the owner thread during `NetworkPoll`.

### 5. Deterministic Cancellation and Shutdown

Network shutdown is deterministic, bounded, and resource-safe:

- **Connection Teardown**: The session transitions `Active -> Closing -> Closed` and then requests transport `Close()`. A graceful disconnect sends a final disconnect packet with a reason code within a configurable bounded deadline derived from measured RTT and clamped by the host policy (default 2 seconds). If the peer does not acknowledge within the deadline, the local transport connection is forcefully terminated. Duplicate `Close()` on a generation-matching terminal handle is success; stale handles return `InvalidHandle`.
- **Cancellation**: Asynchronous operations (resolving DNS, connecting, transferring large assets) accept a `CancellationToken`. Triggering cancellation immediately halts processing and reclaims temporary resources without blocking caller threads.
- **Host Teardown Order**: During engine shutdown, the composition root shuts down `NetworkRuntime` before `Runtime` and `Foundation`:
  1. Stop accepting new connections on all listeners.
  2. Flush critical pending reliable disconnect notices within a bounded grace period.
  3. Signal I/O threads to stop and join all network worker threads.
  4. Release sockets, cryptographic contexts, and transport buffers.
  5. `NetworkRuntime` destroys the unique transport, then the host destroys `NetworkRuntime`.

## Consequences

### Positive

- Strict compile-time insulation: Changing transport libraries (e.g. upgrading ENet or adding WebRTC) never requires recompilation of gameplay or editor headers.
- Safe concurrency: Main simulation and editor threads are completely protected from data races, mutex contention, and socket blocking.
- Clean headless and test execution: `NetworkTransportNull` enables fast, fully deterministic unit and integration testing of replication and session logic without OS socket permissions or network ports.
- Predictable performance: Bounded queues and dedicated frame ticking prevent network traffic bursts from causing memory blowouts or frame-rate hitches.

### Negative / Trade-offs

- Message data must be copied across thread boundaries from I/O queues to the simulation thread (mitigated by bounded buffer pools and move semantics).
- Disconnecting peers requires asynchronous coordination or bounded timeouts rather than instant synchronous socket teardown.

## Rejected Alternatives

- **Monolithic `HoroEngine::Networking` Target**: Combining API, runtime, and concrete socket libraries into one library was rejected because it would force all engine consumers to link socket/transport dependencies and prevent optional headless or null configurations.
- **Direct Sockets in Public APIs**: Exposing `SOCKET` handles, `<sys/socket.h>`, or `ENetHost*` in public headers was rejected as it violates Horo Engine's backend-neutral architecture and leaks third-party dependencies.
- **Direct Callback Invocation on I/O Threads**: Allowing network read callbacks to invoke gameplay code directly was rejected because it introduces widespread concurrency bugs, thread-safety overhead across ECS components, and non-deterministic frame execution.
- **Global / Ambient Network Service Locator**: Using a global singleton `GetNetworkService()` was rejected; network instances must be composed explicitly by the host application.
- **Runtime-Owned Inbound I/O Queue**: Pushing transport receive callbacks directly into a runtime-owned queue was rejected. The transport owns inbound/outbound queues; runtime drains them only through `PollEvents()`.
- **Object-Based Public Connection API**: Public `ITransportConnection` / `ITransportListener` objects were rejected in favor of generation-checked handles on `INetworkTransport`.
- **Transport-Level Application Authentication**: Embedding session tokens or auth state machines in ENet/Null backends was rejected so future transports (SteamNetworkingSockets, WebRTC) can replace packet transport without inheriting game authentication.
