# Multiplayer Replication Architecture

## Purpose

This document defines Horo Engine's server-authoritative replication ownership,
schema and `FieldId` identity, runtime roles, capture/transmit/apply flow, object
lifecycle, compatibility, RPC/input boundary, prediction seam, interest management,
dedicated-server composition and transport integration.

## Core Decisions

- The authority server is the only network role that publishes canonical gameplay
  state. Clients submit typed inputs/commands; they do not write server state.
- Gameplay/component owners declare stable schemas, codecs and capture/apply
  adapters. Scene/Gameplay retain canonical values and mutation safe points.
- `NetworkRuntime` owns registry snapshots, network object identity, capture
  orchestration, baselines, interest, wire encoding, routing and validated apply
  commands—not gameplay state.
- Wire fields use stable numeric `FieldId` values scoped by stable schema IDs and
  explicit versions. Property paths, member names, offsets and native layouts are
  presentation/implementation details only.
- Standalone, authority-server, autonomous-client and simulated-client roles are
  explicit world/session capabilities. Process locality, possession and input
  devices never grant authority.
- Only ADR-098 `Active` sessions reach replication/RPC dispatch.
- Automatic ECS-memory and generic event-bus replication are prohibited.

## Ownership Boundary

| Responsibility | Owner |
|---|---|
| Field meaning, schema/field IDs, codec, bounds and capture/apply adapters | Declaring gameplay/component module |
| Canonical values, entity/component lifetime and mutation safe points | Scene/Gameplay owner |
| Registry generation, network IDs, snapshots, baselines, interest, encoding, routing and receive validation | `NetworkRuntime` |
| Protected message movement and connection lifecycle | `INetworkTransport` |
| Role/trust/session composition | Host and ADR-098 admission policy |

Declarations are inert metadata until host composition validates and publishes one
immutable registry generation. Descriptors cannot register globally, inspect live
objects, find a service locator or mutate state during construction.

## Execution Roles and Authority

```cpp
enum class ReplicationExecutionRole : std::uint8_t {
    Standalone,
    AuthorityServer,
    AutonomousClient,
    SimulatedClient
};

struct ReplicationAuthorityGrant {
    RuntimeSceneId scene;
    RuntimeSceneGeneration sceneGeneration;
    ReplicationAuthorityEpoch epoch;
    ReplicationExecutionRole role;
    NetworkSessionGeneration sessionGeneration;
};
```

The host assigns a role to a specific runtime world. The authority server issues
object-level ownership/submission grants under its epoch. Every operation validates
world, epoch, object generation, session generation and role.

| Role | Canonical state | Submission | Received state |
|---|---|---|---|
| Standalone | Normal local Scene/Gameplay ownership; no network grant | None | None |
| Authority server | Captures/publishes canonical declared state at safe points | Validates typed client inputs/commands | Rejects client-authored state snapshots |
| Autonomous client | No server-state authority; may hold separately declared prediction/presentation state | Sends only commands permitted by the server grant | Applies authoritative snapshot/correction through owner adapter |
| Simulated client | No authority or local prediction grant | No object-authoritative submission | Applies authoritative replica state through owner adapter |

A listen-server process composes distinct server and client worlds/roles. The fact
that both live in one process, share a player, or use loopback transport does not
grant the client world authority. Autonomous means permitted input submission and
possibly a separately qualified prediction capability; it never means canonical
server write permission.

## Replication Schema and Field Identity

```cpp
struct ReplicationSchemaVersion {
    std::uint16_t major;
    std::uint16_t minor;
};

struct ReplicationFieldDescriptor {
    FieldId id;
    ReplicationValueTypeId valueType;
    ReplicationCodecId codec;
    ReplicationCondition condition;
    ReplicationFieldRequirement requirement;
    ReplicationWritePolicy writePolicy;
    ReplicationFieldLimits limits;
};

struct ReplicationSchemaDescriptor {
    ReplicationSchemaId id;
    ReplicationSchemaVersion version;
    ModuleId owner;
    std::span<const ReplicationFieldDescriptor> fields;
    ReplicationCaptureBinding capture;
    ReplicationApplyBinding apply;
};
```

`ReplicationSchemaId` is a globally registered stable semantic ID. `FieldId` is a
non-zero stable 32-bit ID scoped by its schema. Both survive rename, reorder and
C++ refactor. Published IDs are never reused; removed field IDs remain tombstoned.

A field declares one semantic value type, canonical bounded codec, condition,
requiredness, write policy and limits. Editor/display names, `PropertyPath`, C++
member names, component order, reflection indices, pointer values and byte offsets
never enter the wire contract.

Replication conditions are descriptor policy, not authority:

- `Always`: considered for every eligible snapshot;
- `InitialOnly`: required in the authoritative spawn record;
- `OwnerOnly`: routed only to the granted autonomous client;
- `SkipOwner`: omitted for that autonomous client;
- `SimulatedOnly`: routed only to simulated-client views.

An owner may use an explicit revision counter or
`MarkReplicationDirty(NetworkObjectId, FieldId)` as a scheduling hint. The hint
does not contain the value and grants no write/capture authority.

## Registry Composition and Lifecycle

Modules contribute schema descriptors before network world/session activation.
Validation rejects:

- zero, duplicate, tombstoned or foreign-owned schema/field IDs;
- overflowed/empty schemas, duplicate/unsorted fields and invalid version graphs;
- missing value types/codecs, unbounded limits or contradictory condition/write
  policy;
- missing role-required capture/apply adapter and incompatible adapter ownership;
- ambiguous/cyclic compatibility edges or invalid translators.

Fields are canonicalized by numeric `FieldId`. The registry computes a domain-
separated schema-set fingerprint used by ADR-098 session negotiation. Failure
publishes nothing; active sessions pin the complete immutable generation.

Hot reload builds a candidate off-thread and publishes it at an owner safe point.
Existing sessions retain their pinned generation or explicitly close/re-negotiate.
Module unload waits until sessions, snapshots, baselines, queues and adapter calls
release the old generation. Shutdown cancels work, closes dispatch and drains pins
before module/runtime destruction.

## Network Object Lifecycle

The authority server allocates `NetworkObjectId` plus generation under a
`ReplicationAuthorityEpoch` and maps it to one scene-qualified `EntityRef`.
Network IDs are not ECS indices, addresses, names, hierarchy paths or authored
scene IDs.

Lifecycle records are ordered:

1. `Spawn`: object ID/generation, schema/version and required initial fields.
2. `Update`: authoritative tick, baseline identity and changed field records.
3. `Despawn`: terminal tick/reason; later updates for that generation fail.

Reuse increments object generation. Scene replacement increments the authority
epoch. Late packets can therefore never mutate a reused entity slot or replacement
world.

## Capture and Snapshot Ownership

During the fixed network capture safe point, `NetworkRuntime` selects authoritative
relevant objects and invokes the registered schema capture adapter with:

- a validated read-only owner-thread state view;
- the pinned schema generation;
- object/authority identity;
- a preallocated bounded typed writer.

The adapter emits canonical values by `FieldId`. It cannot mutate gameplay state,
retain component views, register fields, call the transport or allocate/block
without bound. After return, NetworkRuntime owns the immutable captured snapshot
and derived baselines—not the source values.

NetworkRuntime never scans ECS/component memory, reflects arbitrary fields,
serializes `sizeof(T)`, compares padding or derives replication from editor
metadata. Explicit capture is the only state source.

## Wire Records and Compatibility

Every replication state record contains:

- authority epoch, object ID/generation and authoritative simulation tick;
- schema ID and negotiated schema version;
- spawn/update/despawn kind and baseline ID where applicable;
- bounded field count with canonical ascending `FieldId` entries;
- each entry's wire type/codec tag, bounded byte length and canonical bytes.

The receiver validates the session is `Active`, server-to-client authority
direction, epoch, object lifecycle, schema generation/version, counts, ordering,
duplicate IDs, field types, lengths, codec limits and total work before decode or
allocation. Native memory layout, padding, RTTI, vtables and pointers never cross
the wire.

Compatibility rules are explicit:

- major versions are incompatible unless the declaring owner registers a bounded
  deterministic translator;
- a compatible minor version may add optional fields with canonical defaults;
- removed IDs are tombstoned;
- semantic type/meaning/requiredness changes allocate a new field ID or follow an
  explicit major-version translation;
- unknown/missing required fields, incompatible codec/type or unsupported
  translator fail;
- unknown optional length-delimited fields may be skipped only when the negotiated
  compatibility rule permits it.

ADR-098 negotiates one compatible schema-set fingerprint/projection. The sender
encodes only that projection. There is no best-effort member/path matching, implicit
downgrade or layout-derived compatibility.

## Validated Apply Path

NetworkRuntime decodes a complete record into a bounded typed apply command. It
validates session, role, authority, object lifecycle and schema before queueing the
command to the target world.

At the owner-thread safe point, Scene revalidates scene/entity generation and calls
the declaring owner's apply adapter. The adapter validates semantic ranges and
commits all accepted fields as one owned mutation, or rejects without partial
visibility. It cannot retain decoded views, bypass component invariants, perform
structural mutation outside Scene commands or write authority-server state from a
client snapshot.

Malformed records are atomic failures. Repeated hostile records follow ADR-098's
abuse/close policy.

## Input, Commands, RPCs and Events

Client input and RPCs are not replicated state writes. They use separately
registered stable command/RPC schemas with direction, reliability, rate, permission,
parameter and payload limits. The server validates the active principal and object
grant, then gameplay decides whether canonical state changes.

Gameplay may publish local events that cause owned state mutation or mark declared
fields dirty. NetworkRuntime does not subscribe to the generic event bus and mirror
arbitrary event payloads. A network event/RPC requires its own typed schema; local
event publication alone produces no network traffic.

## Prediction and Reconciliation Boundary

Prediction is not implied by client role. The non-predicted path applies server
snapshots through the same adapter without mandatory history/replay storage.

An autonomous client may use prediction only when a later capability policy and the
declaring gameplay owner provide canonical input/state/capture/restore hooks. Such
state remains a local candidate; authoritative server snapshots and corrections
win. Simulated clients never gain prediction or input-submission authority.

## Interest Management

Not all objects are relevant to all clients:

```cpp
struct InterestSettings {
    float relevanceRadius;
    std::uint32_t maxRelevantObjects;
    ReplicationGroupMask groupMask;
};

struct ReplicationGroup {
    ReplicationGroupId id;
    BoundingBox volume;
    float priority;
};
```

Interest is a read-only routing input. It cannot grant authority, mutate Scene or
change schema compatibility. The authority server maintains a per-session relevant
set at the network tick under explicit work and bandwidth budgets.

## Dedicated Server

Dedicated servers run headless with full gameplay/physics simulation, authority-
server role, client admission, replication capture and observability. They compose
no renderer/audio dependency merely to define network limits.

```bash
horo-engine server --project /path/to/MyGame --map MainLevel --port 7777
```

Listen and dedicated servers share the same authority, schema and admission
contracts. A listen server's co-located client uses a separate client role/world.

## Network Transport

Replication submits bounded messages through the backend-neutral network runtime;
it never depends on native transport identity:

```cpp
class IReplicationProtocol {
public:
    virtual Result<void> SendReplicationMessage(
        ClientId target,
        const ReplicationMessage& message) = 0;
    virtual Result<void> BroadcastReplicationMessage(
        const ReplicationMessage& message) = 0;
    virtual Result<void> PollReplicationMessages(
        std::span<ReplicationMessage> outMessages) = 0;
};
```

ADR-097 selects private `NetworkTransportGNS` for production direct IP and Null
for deterministic/offline tests. Optional providers remain explicit product
compositions. Transport encryption/provider identity does not grant replication
authority; ADR-098 active-session admission remains mandatory.

## Bandwidth Management

Replication scheduling observes explicit per-session/tick limits:

- per-client byte/message/work budgets;
- priority scheduling of already-relevant objects;
- baseline/delta encoding with bounded retained history;
- replaceable unreliable snapshot policy;
- explicit reliable-queue overflow failure.

Interest, priority and budgets affect routing/scheduling only. They do not change
field identity, authority or compatibility.

## Feature Tiers

| Feature              | `es3`      | `dx11` / `dx12_vulkan` | `high_end` |
| -------------------- | ----------- | ----------------------- | ---------- |
| Max players (server) | 4           | 32                      | 128        |
| Replicated objects   | 512         | 4K                      | 16K        |
| Client prediction    | Basic       | Full                    | Full       |
| Interest management  | Distance    | Spatial+Group           | Spatial+Group |
| Dedicated server     | Yes         | Yes                     | Yes        |
| Bandwidth budget     | 64 KB/s     | 256 KB/s                | 512 KB/s   |

## Testing and Qualification

Required automated coverage includes:

- valid schema registration, canonical fingerprint/order and rejection of zero,
  duplicate, tombstoned, foreign-owned and overflowed declarations;
- missing codec/type, contradictory policy, unbounded limits and malformed/cyclic
  compatibility translators;
- standalone/server/autonomous/simulated capability matrices and co-located
  listen-server worlds without locality-derived authority;
- rename/reorder/layout changes preserving `FieldId`, with explicit rejection of
  `PropertyPath`, offset, pointer and raw-memory fixtures;
- exact, additive-minor and explicit-major translation plus missing/unknown/
  duplicate/reordered/wrong-type/oversized fields;
- spawn/update/despawn, stale object/scene/session/authority generations,
  disconnect, scene reload and module reload/unload pinning;
- capture/apply owner safe points, dirty hints, atomic apply rejection, queue/work
  overflow, cancellation and shutdown with queued work;
- proof that arbitrary component mutations and generic event publication do not
  replicate without a registered schema and explicit capture/dirty path.

Descriptor, compatibility, record framing and codec parsers receive bounded fuzz/
property coverage. Diagnostics identify stable IDs/versions and safe reasons but
exclude field payloads by default.

## Related Documents

- [ADR-097: Default Real-Time Transport Backend](../../adr/097-default-real-time-transport-backend.md)
- [ADR-098: Protocol, Session and Trust Policy](../../adr/098-protocol-session-and-trust-policy.md)
- [ADR-099: Replication Ownership, Authority and Compatibility](../../adr/099-replication-ownership-authority-and-compatibility.md)
- [Networking Architecture](./networking-architecture.md)
- [Scene Runtime](./scene-runtime.md)
- [Gameplay Behavior Authoring](../extensions/gameplay-behavior-authoring.md)
- [Physics Architecture](./physics-architecture.md)
- [Save Game And Persistence](./save-game-and-persistence.md)
- [Application Security Architecture](../security/application-security.md)
- [Observability Performance](../observability/observability-performance.md)
