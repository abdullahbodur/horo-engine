# Multiplayer Replication Architecture

## Purpose

This document defines the multiplayer replication subsystem for Horo Engine.
It covers client-server architecture, object replication, RPC, authority model,
prediction and reconciliation, interest management, dedicated server, and
network transport.

## Client-Server Model

Horo Engine uses an authoritative server model:

- The server owns game state and is authoritative for all gameplay decisions
- Clients send input and receive replicated state
- The server runs the full simulation; clients run a predicted simulation
- Listen servers (one client is also the server) are supported
- Dedicated servers (headless, no rendering) are the production target
- Replication and RPC dispatch accept only sessions published `Active` by the
  ADR-098 admission gate; transport connectivity is not gameplay authority

```cpp
enum class NetworkRole {
    ServerOnly,        // exists only on server
    ClientOnly,        // exists only on client (e.g., local VFX)
    ServerAuthoritative, // server owns, replicates to clients
    ClientSubmittedMessage, // client submits non-authoritative messages validated by server
};
```

## Object Replication

### Replicated Objects

Objects marked for replication have their state synchronized:

```cpp
struct ReplicatedObject {
    NetworkId          networkId;
    NetworkRole        role;
    ReplicationOwner   owner;          // which peer has authority
    ReplicationGroup   group;          // interest management group
    bool               isRelevant;     // current interest state
};
```

### Property Replication

Individual properties are marked for replication:

```cpp
struct ReplicatedProperty {
    PropertyPath       path;           // e.g., "Transform.Position"
    ReplicationCondition condition;    // when to replicate
    float              lerpTime;       // interpolation duration
};
```

Replication conditions:

- **Always**: Replicated on every change
- **InitialOnly**: Sent once on spawn
- **OwnerOnly**: Only to the owning client
- **SkipOwner**: To all clients except the owner
- **SimulatedOnly**: Only to simulated proxies (not autonomous)

### Replication Update

Each network tick:

1. Server collects dirty properties for each relevant object
2. Properties are serialized to a per-client update buffer
3. Buffer is sent as an unreliable or reliable datagram
4. Client receives and deserializes updates
5. Client interpolates between received states for smooth visual updates

## Remote Procedure Calls (RPC)

RPCs allow clients and server to invoke functions on remote objects:

```cpp
enum class RPCDirection {
    Server,     // client → server (validated)
    Client,     // server → client (reliable or unreliable)
    Multicast,  // server → all clients
};

struct RPCCall {
    NetworkId          targetObject;
    uint32_t           functionId;
    std::vector<uint8_t> payload;     // serialized parameters
    RPCReliability     reliability;
};
```

Server RPCs are validated:

- The calling client must own the object (or have explicit permission)
- Parameter ranges are validated on the server
- Rate limiting prevents RPC spam

## Prediction And Reconciliation

### Client Prediction

Clients predict the result of their own input before receiving server
confirmation:

1. Client sends input to server
2. Client immediately applies input to its local predicted state
3. Server processes input and sends authoritative state
4. Client compares predicted state with authoritative state
5. If they differ, the client reconciles (rewinds and replays)

```cpp
struct PredictionState {
    uint32_t            lastAcknowledgedInputId;
    std::deque<InputSnapshot> pendingInputs;
    std::deque<StateSnapshot> stateHistory;
};
```

### Reconciliation

When the server state arrives:

- Discard acknowledged inputs from the pending queue
- Apply remaining unacknowledged inputs to the server state
- Interpolate visual state toward the corrected position
- Log prediction errors for debugging

## Interest Management

Not all objects are relevant to all clients:

```cpp
struct InterestSettings {
    float    relevanceRadius;       // spatial distance
    uint32_t maxRelevantObjects;
    ReplicationGroupMask groupMask;
};

struct ReplicationGroup {
    ReplicationGroupId   id;
    BoundingBox          volume;      // spatial volume
    float                priority;
};
```

Interest management is spatial (by distance) and explicit (by replication
group). The server maintains a per-client relevant set, updated each network
tick.

## Dedicated Server

Dedicated servers run the engine in headless mode:

- No window, no rendering, no audio
- Full physics and gameplay simulation
- Network transport for client connections
- Command-line configuration
- Observability and logging for server operations

```bash
horo-engine server --project /path/to/MyGame --map MainLevel --port 7777
```

Server features:

- Player slot management (max players, reserved slots)
- Connection queue and authentication
- Server travel (seamless map changes)
- Server-side plugins for admin tools
- Matchmaking integration hooks

## Network Transport

The network layer abstracts the transport:

```cpp
class IReplicationProtocol {
public:
    virtual Result<void> SendReplicationMessage(ClientId target, const ReplicationMessage& message) = 0;
    virtual Result<void> BroadcastReplicationMessage(const ReplicationMessage& message) = 0;
    virtual Result<void> PollReplicationMessages(std::span<ReplicationMessage> outMessages) = 0;
};
```

Transport policy follows
[ADR-097](../../adr/097-default-real-time-transport-backend.md) and the
[Networking Architecture](./networking-architecture.md):

- **GameNetworkingSockets**: The production direct-IP baseline, implemented only
  by private `NetworkTransportGNS` and projected through Horo-owned transport
  capabilities and events.
- **Null**: The deterministic/offline backend used for contract tests, CI and
  products that intentionally run the network runtime without native I/O.
- **Optional providers**: ICE/P2P, relay, WebRTC, Steam and console integrations
  are future product-specific compositions. They are not mandatory 1.0 targets
  and cannot be selected as silent fallback behavior.

Replication does not depend on a native transport or provider identity. Session
authentication, protocol negotiation, authority and bandwidth policy remain in
`NetworkRuntime`; GNS packet encryption does not replace them. All transports must
honor the same bounded queues, cancellation, malformed-input and shutdown contract.
Before ADR-098 activation, replication messages are rejected and never buffered;
provider identity or a claimed client role cannot grant replication authority.

## Bandwidth Management

Replication bandwidth is managed:

- Per-client bandwidth budget per tick
- Priority-based scheduling (closer/more-important objects get bandwidth first)
- Delta compression (only changed bytes are sent)
- Object休眠 (dormancy) for distant or irrelevant objects
- LOD for replicated properties (fewer updates for distant objects)

## Feature Tiers

| Feature              | `es3`      | `dx11` / `dx12_vulkan` | `high_end` |
| -------------------- | ----------- | ------------- | ------------ |
| Max players (server) | 4           | 32            | 128          |
| Replicated objects   | 512         | 4K            | 16K          |
| Client prediction    | Basic       | Full          | Full         |
| Interest management  | Distance    | Spatial+Group | Spatial+Group|
| Dedicated server     | Yes         | Yes           | Yes          |
| Bandwidth budget     | 64 KB/s     | 256 KB/s      | 512 KB/s     |

## Related Documents

- [Networking Architecture](./networking-architecture.md): transport layer and connection model
- [ADR-097: Default Real-Time Transport Backend](../../adr/097-default-real-time-transport-backend.md): production baseline and optional provider policy
- [ADR-098: Protocol, Session and Trust Policy](../../adr/098-protocol-session-and-trust-policy.md): pre-gameplay admission, peer trust and active-session gate
- [Scene Runtime](./scene-runtime.md): object lifecycle and network identity
- [Physics Architecture](./physics-architecture.md): server-side physics and prediction
- [Save Game And Persistence](./save-game-and-persistence.md): server-side save state
- [Security Application](../security/application-security.md): server validation and anti-cheat
- [Observability Performance](../observability/observability-performance.md): network metrics
