# ADR-102: Runtime Network Modes and Authority Exposure

- **Status**: Proposed
- **Date**: 2026-09-02
- **Supersedes**: None
- **Scope**: Standalone, client, listen-server and dedicated-server host composition; package support versus runtime selection; world-scoped role and authority exposure; startup, travel, disconnect and shutdown lifecycle
- **Issue**: [NET-007.1](https://github.com/abdullahbodur/horo-engine/issues/1161)
- **Jira**: [HORO-1161](https://horo-engine.atlassian.net/browse/HORO-1161)
- **Related**: [ADR-097](097-default-real-time-transport-backend.md), [ADR-098](098-protocol-session-and-trust-policy.md), [ADR-099](099-replication-ownership-authority-and-compatibility.md), [ADR-100](100-prediction-capability-tiers-and-determinism-policy.md), [ADR-101](101-interest-priority-and-network-budget-model.md)
- **Normative documents**: [Networking Architecture](../architecture/runtime/networking-architecture.md), [Multiplayer Replication Architecture](../architecture/runtime/multiplayer-replication-architecture.md), [Runtime Lifecycle](../architecture/runtime/runtime-lifecycle.md), [System Design](../architecture/foundation/system-design.md)

## Context

Horo defines transport, admission, replication, prediction and scheduling contracts,
but does not yet freeze how a product becomes a standalone runtime, client, listen
server or dedicated server. Without a composition decision, code may infer server
authority from a process flag, headless state, a local player, a listener, a loopback
connection or the fact that a server and client share one process. Those heuristics
conflict with ADR-099's world-scoped roles and can expose mutually incompatible
capabilities to one gameplay world.

Compile/package capability and runtime behavior are also different concerns. A
package may contain both client and server support while one invocation selects
only one mode. Conversely, a client-only package must not manufacture server
authority because a launch argument or project value asks it to listen. The build
profile and project-configuration ownership that produce the support declaration
belong to NET-009.1; this decision defines the runtime seam they must satisfy.

Listen servers make process-global answers especially unsafe. Their authority
server world and local client world share a process but not canonical state,
session role, authority epoch or mutation rights. Dedicated servers share the same
server contract without local-player or presentation capabilities. Standalone
worlds have normal local gameplay ownership but no network authority grant.

This ADR defines the finite mode plan, legal capability matrix, scoped exposure
contract and lifecycle. It does not define project settings storage, build-profile
schema, matchmaking, server orchestration, distributed authority or lockstep.

## Decision

### 1. Package support and runtime mode are separate typed facts

Every runnable artifact exposes an immutable product capability declaration before
application services are constructed:

```cpp
enum class RuntimeNetworkMode : std::uint8_t {
    Standalone,
    Client,
    ListenServer,
    DedicatedServer
};

struct NetworkProductModeSupport {
    ProductBuildId productBuild;
    NetworkProductModeSupportRevision revision;
    EnumSet<RuntimeNetworkMode> supportedModes;
};

struct RuntimeNetworkModeRequest {
    RuntimeNetworkMode requestedMode;
    NetworkModeProfileId profile;
    NetworkTrustPolicyId trustPolicy;
};
```

The package declaration says only which modes the artifact can realize. It does
not select the current mode or grant authority. The host resolves command-line,
application and trusted configuration intent into one request, checks it against
the declaration and produces one immutable `RuntimeNetworkPlan` for the host
generation. An unsupported mode fails startup with a typed error before a world,
listener, outbound connection or local player is published.

Mode selection cannot add a missing target/backend, load an undeclared module,
weaken a trust policy or reinterpret headless/render/audio support. A package may
support both `Client` and `DedicatedServer`; one process invocation still activates
exactly one plan. The source, precedence and cook rules for the product declaration,
profile and request remain NET-009.1.

### 2. The host validates one finite composition plan

The resolved plan describes roles and owned services rather than scattered
booleans:

```cpp
enum class NetworkWorldKind : std::uint8_t {
    Standalone,
    AuthorityServer,
    Client
};

struct NetworkWorldPlan {
    NetworkWorldSlotId slot;
    NetworkWorldKind kind;
    ReplicationExecutionRole replicationRole;
    LocalPlayerPolicy localPlayer;
};

struct RuntimeNetworkPlan {
    RuntimeNetworkPlanId id;
    HostGeneration hostGeneration;
    RuntimeNetworkMode mode;
    NetworkRuntimePolicy networkRuntime;
    ListenerPolicy listener;
    OutboundSessionPolicy outboundSession;
    std::span<const NetworkWorldPlan> worlds;
};
```

Composition validation rejects duplicate world slots, a missing required world,
more than one authority world, an authority role in client/standalone mode, a
listener without an authority world, an outbound gameplay session in dedicated
mode, a local player in dedicated mode and any world role/capability combination
outside the matrix below. The plan is frozen before Scene/Application activation;
descriptor validation has no side effects.

| Mode | Worlds | Local player owner | Listener/admission owner | Outbound gameplay session | Canonical authority |
|---|---|---|---|---|---|
| `Standalone` | One standalone world | Application owns an optional local-player service scoped to that world | None | None | Normal local Scene/Gameplay ownership; no network authority epoch/grant |
| `Client` | One client world | Application owns local-player service scoped to the client world | None | `NetworkRuntime` owns one or more bounded client session attempts | Remote server; local world receives autonomous/simulated grants only after admission |
| `ListenServer` | One authority-server world and one distinct local client world | Application owns local player only in the client world | Server-side `NetworkRuntime` owns listener, admission and active server sessions | Local client uses an explicit admitted loopback session | Server world only |
| `DedicatedServer` | One authority-server world | None | `NetworkRuntime` owns listener, admission and active server sessions | None | Server world only |

An editor preview or test harness must instantiate one of these same plans. It may
use Null/in-memory transport and synthetic principals, but cannot introduce an
`Editor`, `Preview`, `Host` or `Local` authority role.

### 3. World role and service ownership are distinct

The process host owns the immutable plan and lifecycle. `NetworkRuntime` owns
transport injection, listeners, admission, sessions, authority epochs,
replication/scheduling generations and their teardown. Scene/Gameplay own
canonical world values and safe points. Application/Input own local-player and
device assignment. A local player is not a session, and neither is authority.

In listen-server mode the server and client worlds have distinct runtime scene
IDs/generations, replication roles, session views, command queues and mutation
safe points. The loopback path crosses the same bounded ADR-098 admission and
typed submission/apply boundaries as a remote client. An implementation may
optimize protected message movement after validation, but it cannot share mutable
world storage, bypass schemas or apply the client world directly to the server.

Dedicated and listen servers use the same server role, admission, replication,
interest and budget contracts. Headless operation only omits window, renderer,
audio, input and local-player capabilities. It does not create a different
authority model or renderer-derived network profile.

Standalone composition omits `NetworkRuntime` and concrete transports by default.
A product that deliberately includes an offline Null facade may expose only the
standalone role; it creates no listener, session principal, authority epoch,
replication capture or transport worker.

### 4. Gameplay receives a scoped, generation-checked role view

Gameplay code receives a read-only view from its current world execution context:

```cpp
struct GameplayNetworkRoleView {
    RuntimeNetworkPlanId plan;
    HostGeneration hostGeneration;
    RuntimeSceneId scene;
    RuntimeSceneGeneration sceneGeneration;
    ReplicationExecutionRole role;
    std::optional<NetworkSessionGeneration> sessionGeneration;
    std::optional<ReplicationAuthorityEpoch> authorityEpoch;
    EnumSet<GameplayNetworkCapability> capabilities;
};
```

The capability set is derived only from the validated mode/world plan plus active
ADR-098 session and ADR-099 object grants. It is not configurable independently.
The finite capabilities distinguish local canonical mutation, authoritative
capture/publication, bounded client input submission, autonomous prediction and
authoritative snapshot application. Validation rejects incompatible pairs such as
authoritative publication with client snapshot apply, or server authority with
autonomous-client submission in one world view.

`authorityEpoch` exists only for an authority-server world. A client view gains a
session generation only after the session is `Active`; object submission still
requires the server-issued object grant. Standalone exposes local gameplay
ownership without inventing an epoch or session generation.

The view may be borrowed only during the owner execution scope or copied as an
immutable value. Every privileged command revalidates plan, host, scene, session,
authority and object generations at the owning safe point. A stale or foreign view
returns a typed rejection and performs no mutation.

### 5. Ambient and process-derived authority are prohibited

Gameplay and feature modules must not discover authority from:

- a process-global `IsServer`, `IsClient`, `NetMode` or mutable singleton;
- launch arguments, executable name, build configuration or package target;
- headless state, absent renderer/audio/input, thread identity or frame phase;
- listener/connection presence, transport backend, IP range or loopback locality;
- local-player count, device assignment, possession, ownership display labels or
  editor play state; or
- service-locator availability, same-process pointers or direct world storage.

A scoped convenience query may derive an answer from
`GameplayNetworkRoleView`, but the boolean is presentation/control-flow evidence,
not a transferable authority token. Mutations require the typed operation/grant
accepted by the actual owner. Debug, console, admin and cheat permission remain
separate capability checks; dedicated/listen-server mode does not grant them.

### 6. Startup publishes no partial role

Startup order for a network-capable runtime is:

1. load and verify product mode support;
2. resolve the trusted runtime mode request and referenced policy IDs;
3. validate the complete plan and target/backend availability without side effects;
4. construct the selected NetworkRuntime/transport and local-player services;
5. create unpublished world candidates with their exact world roles;
6. start required listener or outbound attempt under the immutable trust policy;
7. publish worlds and scoped role views only after their local construction succeeds;
8. publish a client session view to gameplay only after ADR-098 activation.

A listener becoming bound does not publish authority into a client world. A
transport becoming connected does not publish a gameplay session. Failure unwinds
only constructed owners in reverse order and invalidates the host/plan generation,
so a late bind, connect or verifier completion cannot resurrect the runtime.

### 7. Travel, reconnect and disconnect preserve mode boundaries

Runtime network mode is immutable for one host generation. Changing among
standalone, client, listen-server and dedicated-server requires bounded host
recomposition/restart; a live world cannot silently acquire or lose server
authority.

Scene travel preserves the plan but prepares new world generations. Server travel
allocates a new authority epoch and republishes admitted sessions/object grants
only after the new authority world commits. Client travel applies only a
server-admitted transition and replaces its client world generation. Listen-server
travel commits compatible server and client candidates as an aggregate lifecycle
operation; neither world observes a half-replaced pair.

Reconnect allocates a new session generation and repeats negotiation,
authentication and activation. Old principals, object grants, prediction history,
replication baselines and role views remain stale. Client disconnect removes the
active session view before further gameplay dispatch and enters the explicit
application disconnect route; it never promotes the client world to standalone.
A listen server's local-client disconnect does not stop or transfer authority from
the server world. Loss of the server listener stops new admission but does not
silently change the existing authority role; host policy chooses bounded recovery
or shutdown.

### 8. Shutdown revokes exposure before destroying owners

Shutdown closes external admission and gameplay submission first. The host then:

1. marks the plan stopping and rejects new listener/connect/travel requests;
2. unpublishes gameplay role/session views and invalidates session/object grants,
   authority epochs, prediction histories and replication work generations;
3. stops local-player/input producers and drains accepted owner commands;
4. unloads client and authority worlds through their Scene safe points;
5. closes sessions/listeners, cancels admission and bounded network work, then
   shuts down and destroys the transport under ADR-098/ADR-097; and
6. retires the plan and host generation after all leases close.

Repeated shutdown is idempotent. Late transport, verifier, travel, capture or
apply completions can observe only stale generations and cannot republish a role,
session or world. A partial-startup unwind uses the same owner order for the
resources that were actually constructed.

### 9. Tests qualify the full mode and lifecycle matrix

Focused automated coverage must include:

- each valid mode plan and every invalid world/listener/outbound/local-player role
  combination, including unsupported package modes;
- proof that launch flags, headless state, local players, listener presence,
  loopback transport and same-process pointers cannot synthesize authority;
- standalone startup with NetworkRuntime omitted and with an explicitly composed
  inert Null facade;
- client admission success/failure, pre-Active invisibility, reconnect generation
  replacement, disconnect without standalone promotion and stale grant rejection;
- listen-server isolation, admitted loopback flow, separate world storage/queues,
  local-client disconnect and aggregate travel without authority transfer;
- dedicated startup without window/renderer/audio/input/local-player services,
  while preserving server admission, simulation and replication;
- travel/reload allocating new scene/authority generations and rejecting late
  packets, verifier work, prediction history and apply/capture commands;
- shutdown and partial-startup failure at every construction stage, repeated stop,
  active/pending sessions and no callback or role publication after destruction;
  and
- compile/link checks that client-only/standalone artifacts cannot activate
  undeclared server targets or concrete backends.

## Consequences

### Positive

- Runtime topology, service ownership and gameplay authority are explicit and
  testable across standalone, client, listen and dedicated products.
- One artifact may safely support multiple runtime selections without treating a
  package capability as current authority.
- Listen servers preserve the same client/server trust, schema and mutation
  boundaries as remote compositions.
- Generation-scoped views make travel, reconnect, disconnect and teardown reject
  stale work instead of leaking ambient role state.
- Dedicated servers stay headless without making renderer/audio absence a gameplay
  authority signal.

### Costs

- Hosts must validate and retain a mode/world plan and pass scoped role views into
  gameplay execution.
- Listen-server tests require two world lifecycles and explicit loopback admission
  rather than a shared-world shortcut.
- Mode changes require host recomposition instead of convenient live global-flag
  mutation.

## Rejected Alternatives

### A process-global network mode or `IsServer()` singleton

Rejected because a listen server contains both server and client worlds, and a
process-global answer cannot carry scene/session/authority generations.

### Infer authority from listener, transport or locality

Rejected because connectivity and locality are transport facts. They do not prove
session activation, principal admission, world role or object authority.

### Treat standalone as a one-peer authority server

Rejected because standalone has normal local gameplay ownership and should not pay
for or accidentally expose sessions, replication, authority epochs or transport
work.

### Share one mutable world in listen-server mode

Rejected because direct client access would bypass admission, schemas, prediction
correction, ownership safe points and the server's canonical authority boundary.

### Let runtime flags enable modes absent from the package

Rejected because runtime configuration cannot safely create missing targets,
backends, trust material or qualification evidence.

### Change network mode during scene travel

Rejected because travel changes world generations, not host composition. Combining
them would expose half-transitioned services and make authority revocation
ambiguous.
