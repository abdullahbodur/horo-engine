# ADR-114: Canonical Runtime World Persistence Boundary

- **Status**: Proposed
- **Date**: 2026-09-02
- **Supersedes**: None
- **Scope**: Authoring defaults versus saved overrides, durable/derived/transient state classification, subsystem-owned canonical adapters, participant descriptors, capture and restore authority across PIE, packaged, server and client worlds
- **Issue**: [SAV-004.1](https://github.com/abdullahbodur/horo-engine/issues/1438)
- **Jira**: [HORO-1438](https://horo-engine.atlassian.net/browse/HORO-1438)
- **Related**: [ADR-012](012-world-streaming-partition-authority-and-subsystem-boundaries.md), [ADR-024](024-perception-ownership-sense-policy-and-budget.md), [ADR-068](068-music-transport-and-cross-system-ownership.md), [ADR-092](092-character-controller-determinism-and-state-composition.md), [ADR-112](112-save-archive-container-and-compatibility-policy.md), [ADR-113](113-local-storage-user-profile-and-slot-ownership.md)
- **Normative documents**: [Save Game And Persistence](../architecture/runtime/save-game-and-persistence.md), [Scene Runtime](../architecture/runtime/scene-runtime.md), [Gameplay Module Boundary](../architecture/extensions/gameplay-module-boundary.md), [World Streaming](../architecture/runtime/world-streaming-architecture.md)

## Context

Runtime Save owns one coherent capture/restore transaction, but the current language
still names an `EcsSaveSnapshot`, asks components to declare serialization and uses a
generic gameplay-state provider. That makes it easy to treat every component field or
service object as independently serializable, duplicate subsystem state in ECS and a
service chunk, or persist native/transient execution state because it is reachable.

Scene documents and cooked assets define authored defaults. A runtime world contains
mutated semantic state, derived caches, in-flight work, presentation objects and
external account state with different owners and lifetimes. A save must reconstruct a
valid gameplay world, not reproduce an object graph or memory image.

Subsystems already have focused rules: Character saves a complete Horo checkpoint
paired with Physics, World Streaming owns dormant deltas, Audio permits semantic music
state but not voices/devices, and AI distinguishes stable execution meaning from jobs
and cooked indices. These rules need one common opt-in adapter contract and an explicit
composition model.

## Decision

### 1. State is classified before persistence is admitted

| Category | Authority | Save behavior |
|---|---|---|
| Authoring definition/default | SceneDocument, source asset and cook pipeline | Referenced as the immutable base; never copied back from runtime save state |
| Runtime canonical state | Owning Scene/gameplay subsystem | Opt-in through exactly one canonical state adapter |
| Derived/rebuildable state | Owning runtime subsystem/cache | Excluded; rebuilt from base assets and restored canonical state |
| Transient execution state | Scheduler, operation, transport or subsystem runtime | Excluded unless a schema explicitly promotes semantic state into the canonical category |
| Presentation state | Renderer, Audio device/voice, Runtime UI presentation | Excluded; semantic intent may be saved only by its gameplay/domain owner |
| Asset/cooked content | Asset Registry and package/cook authorities | Stable identities/revisions are dependencies; payloads are not embedded as runtime state |
| Account/profile state | Profile/account authority | Separate store and transaction; slot restore cannot rewind it |
| External/platform/network state | Platform service or network authority | Excluded; reconnect/revalidate through that authority after restore |

Reachability, trivially-copyable layout, reflection metadata or presence in ECS does
not change category. The default for runtime data is not durable. Persistence is an
explicit schema decision owned by the subsystem that owns the semantic invariant.

### 2. Authoring base and saved runtime overrides never become peers

Restore first resolves and validates the exact compatible cooked scene/world base.
It then composes canonical saved state by stable identity:

- an authored entity absent from saved overrides uses current compatible defaults;
- a persisted field override replaces only the schema-declared authored default;
- deletion of an authored entity is an explicit tombstone;
- a runtime-spawned durable entity records a stable `PersistentEntityId`, declared
  archetype/prefab/definition identity and canonical owned values; and
- references resolve by stable persistent identity inside the detached candidate.

Runtime `EntityRef`, slot/generation indices, pointers, hierarchy addresses and
component storage order never persist. A base revision change requires a declared
base/delta migration or typed incompatibility; readers do not guess that same paths or
names mean same authored objects.

Saved overrides never mutate SceneDocument, source assets, cooked packages or current
authoring defaults. Editor Apply/Keep Changes is a separate reviewed authoring command
that may translate selected runtime facts into document edits; loading/saving a slot
does not invoke it.

### 3. Each durable semantic field has exactly one adapter owner

Composition seals one registry of inert descriptors and adapter bindings:

```cpp
enum class CanonicalStateScope : uint8_t {
    World,
    Session,
    SlotPlayer,
    ServerWorld
};

struct CanonicalStateParticipantDescriptor {
    StableTypeId participant;
    ParticipantSchemaVersion schema;
    CanonicalStateScope scope;
    RequiredParticipantPolicy required;
    StableTypeId ownerSubsystem;
    std::vector<StableTypeId> dependencies;
    CanonicalStateLimits limits;
};

class ICanonicalStateAdapter {
public:
    virtual Result<OwnedCanonicalSnapshot> Capture(
        const CanonicalCaptureContext&) = 0;
    virtual Result<PreparedCanonicalState> PrepareRestore(
        CanonicalStateReader&, const CanonicalRestoreContext&) = 0;
    virtual void PublishPrepared(PreparedCanonicalState&&) noexcept = 0;
};
```

`OwnedCanonicalSnapshot` owns an immutable, concurrently readable byte/root view;
after `Capture` returns, background serialization may read it while the owner resumes
mutation. Large owners use versioned immutable roots or copy-on-write pages so the
safe-point capture is bounded; returning a borrowed mutable view or requiring the
owner thread to remain blocked is forbidden.

`CanonicalRestoreContext` exposes a read-only lookup of already prepared dependency
candidates keyed by `StableTypeId`. The host populates it strictly in the validated
dependency DAG order. A participant cannot inspect later candidates, publish through
the lookup, or retain a candidate beyond aggregate commit/rollback.

Descriptors are inert metadata. Creating/validating them performs no registration,
service lookup, capture or lifecycle callback. Host composition binds adapters,
validates unique participant/field ownership, schema/migration paths, required/
optional policy, dependency DAG, scopes, affinity, resource limits and no-fail
publication before a session can save.

Sealing builds an immutable `CanonicalFieldId -> participant/ownerSubsystem` map.
Duplicate claims reject the complete composition before session activation; runtime
capture never resolves field ownership by registration order or last writer.

A component descriptor may declare stable fields belonging to an adapter, but it does
not receive an independent generic `Serialize(Component&)` escape hatch. A gameplay
service, ECS component and world ledger cannot each emit authoritative copies of the
same health, quest, inventory, transform or spawn state. Duplicate ownership rejects
composition.

### 4. The core Scene adapter owns identity and structural world state only

The Horo Scene canonical adapter owns:

- compatible base scene/world identity and structural revision evidence;
- persistent authored/spawned entity identity, existence and tombstones;
- canonical hierarchy/ownership relationships and stable cross-entity remaps; and
- Horo core component fields explicitly assigned to Scene ownership.

It does not walk arbitrary component memory, serialize gameplay-module fields, own
Physics/Character/AI state or capture native handles. Gameplay modules and built-in
subsystems contribute separate participants for their owned canonical semantics.
Their stable entity references join the core identity map during aggregate prepare.

Slot-player state is a distinct participant for slot-scoped player facts not already
owned by another gameplay participant. Account-global preferences, achievements and
statistics remain outside. Persistent World owns dormant cell deltas/tombstones; the
core Scene adapter does not duplicate them from unloaded cells.

### 5. Adapters define semantic inclusion and exclusion

An adapter schema lists durable stable IDs/values, canonical defaults, scope,
dependencies, maximum counts/bytes and migration behavior. It also documents derived
rebuild rules and explicitly excluded state. Unknown fields or missing required data
follow ADR-112 compatibility policy.

Always excluded unless converted by the semantic owner into a different canonical
value are:

- pointers, native/library objects, allocator/container capacity and compiler layout;
- runtime handles/generations, registry indices and provider objects;
- jobs, futures, cancellation tokens, queues, callbacks and pending command/event
  buffers;
- renderer/GPU resources, extraction buffers, fences, views and frame history;
- Audio voices/devices/decoders/DSP/ring buffers and sample-device epochs;
- Physics solver bodies/manifolds/broadphase/proxies/query caches and Jolt state;
- Navigation query handles, open lists, Detour runtime pointers and rebuild scratch;
- network sockets/connections/keys, replication queues, prediction buffers and remote
  authority state on clients;
- UI focus/hover/animation/layout caches, modal/panel state and localized text; and
- wall-clock time as simulation progression or restore causality.

RNG streams, timers, AI execution frames, musical position, physics motion or similar
facts are durable only when their owner defines stable semantic representation and
restore behavior. Persisting them once does not authorize copying adjacent transient
implementation state.

### 6. Adjacent subsystems map to explicit participants

| Owner | Canonical participant responsibility | Rebuilt/excluded examples |
|---|---|---|
| Scene/ECS core | Persistent entity identity, existence, hierarchy, core owned values, spawns/tombstones | ECS slot/generation, storage order, system queues |
| Gameplay module/service | Its declared component/service semantics and stable references | Behavior objects, callbacks, module pointers, hot-reload machinery |
| Character + Physics | ADR-092 complete Horo Character state and paired canonical Physics/world checkpoint | Jolt bodies, proxy/manifold/query-cache state |
| World Streaming | ADR-012 persistent active/inactive cell deltas, relocations and tombstones | Residency attempts, PartitionEpoch, loader jobs and spill paths |
| Navigation | Session-persistent semantic obstacle/link intent when product policy requires it | NavMesh artifacts, Detour queries, local avoidance scratch and tile candidates |
| Gameplay AI | Stable plan/schema identity, approved blackboard/perception and bounded semantic execution state | Jobs, path/query handles, cooked indices and scheduler buckets |
| Audio/domain gameplay | Semantic music/narrative cue and admitted musical position under ADR-068 | Voice/device/provider handles, decoder/DSP state and output samples |
| Runtime UI/game flow | Gameplay-owned route/model state only when explicitly declared | Widget tree, focus, hover, animation and render state |
| Networked world | Authoritative server-owned canonical gameplay/world participants | Client copies of server truth, connections, keys and packet queues |
| Profile/account | No slot participant for global settings/progression | Separate profile-store transaction |

The table assigns boundaries, not one mandatory save policy for every product. A
product may omit optional semantic state and rebuild/reset it, but cannot give another
subsystem ownership merely to avoid writing the proper adapter.

### 7. Capture is one aggregate canonical cut

RuntimeSaveService coordinates but does not interpret subsystem state. At the owner
lifecycle safe point it pins one scene/session incarnation, fixed tick, structural
revision, origin, participant-registry revision and dependency revisions, then asks
all required adapters for owned immutable snapshots under their admitted capture
contracts. Large state uses bounded copy-on-write/versioned roots; workers receive no
live mutable object.

Every required participant belongs to the same `CanonicalCaptureEpoch`. An adapter
that cannot provide a coherent snapshot defers/fails the whole capture; the service
does not combine prior-tick state or omit a required participant. Optional omission
must be descriptor-approved and represented in the manifest. RuntimeSaveService
orders dependencies, enforces aggregate budgets and builds ADR-112 canonical state,
but never reflects over SceneDocument or arbitrary ECS storage.

### 8. Restore prepares all owners and publishes once

Restore validates the archive/base/dependencies, migrates detached participant data,
builds the core stable entity map and calls each adapter's fallible `PrepareRestore`
in dependency order. Adapters resolve assets/current implementation resources and
create private candidate roots; they do not mutate active state.

Required candidates plus Scene, slot-player and Persistent World roots join the one
aggregate bundle. The owner revalidates session/base/registry generations at
`CommitDeferredLifecycleChanges` and invokes only prevalidated noexcept ownership
transfers. No observer can see a Scene/entity structure without its required gameplay,
Physics/Character/world state. Derived caches and asynchronous work restart after
publication from the new authoritative roots.

Failure/cancellation before commit retires candidates and leaves the active runtime
unchanged. Post-publication failure is an owner-contract violation, not permission to
partially roll back. Profile/account/platform side effects are never part of this
bundle.

### 9. Host/authority policy changes admitted participants, not ownership

- PIE uses the same descriptor/adapter contracts in its isolated ADR-113 namespace.
  Play restart/hot reload transfer is a separate transient preservation operation and
  cannot silently become durable save schema.
- Packaged standalone saves the authoritative local world plus product-approved
  slot-player/session participants.
- Dedicated/headless hosts admit server-world participants and omit presentation/
  client-only state without linking renderer, Audio device or UI implementations.
- A multiplayer server may capture authoritative world state under host quiescence.
  A client may save only locally authoritative/product-approved state and references;
  it never archives replicated server state as restorable authority.

The manifest records required participant IDs/scopes. Loading under a host that lacks
required authority/capability rejects before preparation; it does not reinterpret a
server participant as client-local data or silently drop it.

Server restore validates every manifest participant scope against the authenticated
host/session authority plan before reading participant payloads. A client-supplied or
untrusted archive containing `ServerWorld` scope is rejected as
`AuthorityScopeMismatch`; archive metadata cannot grant server authority or select a
server-only adapter.

### 10. Qualification proves classification and single ownership

Required evidence includes:

- descriptor composition rejects duplicate participant/field ownership, dependency
  cycles, missing required adapters, invalid scopes and unbounded declarations;
- authoring base plus override/tombstone/spawn composition, base migration and proof
  that save/load never edits SceneDocument or cooked assets;
- one-tick aggregate capture across Scene/gameplay/Physics/Character/AI/world ledger,
  including inactive cells and optional omission policy;
- failure of every required prepare stage with active runtime unchanged, followed by
  one observable no-fail aggregate publication;
- reflection/trivial-copy/native-memory attempts rejected and golden canonical
  fixtures for each participant schema;
- rebuilt caches/resources/jobs after restore and absence of pointers, native handles,
  queues, presentation state and wall-clock progression in archives; and
- PIE/packaged/headless/server/client composition matrices with forbidden replicated
  client authority and no presentation dependency in headless saves.

## Consequences

### Positive

- Authoring data, canonical runtime state and derived/transient state have explicit
  non-overlapping owners.
- Subsystems preserve semantic invariants instead of exposing object memory.
- Aggregate capture/restore remains coherent across component, service and world data.
- Adjacent subsystem contracts map to one extensible participant model.

### Costs

- Every durable subsystem needs a descriptor, canonical codec, migration and adapter.
- Composition must validate field ownership and dependency order.
- Restore must rebuild implementation/native state instead of loading memory images.

## Rejected Alternatives

### Serialize every reflected or trivially copyable component

Rejected because component storage does not own every semantic invariant and native
layout is neither portable nor sufficient for service/world state.

### Store the full runtime object graph

Rejected because pointers, handles, jobs, caches and native resources have invalid
lifetimes and hide dependency-aware reconstruction.

### Save only SceneDocument plus runtime transforms

Rejected because authoring defaults are not mutated gameplay truth and omit durable
service, spawn/tombstone, inactive-world and subsystem state.

### Let RuntimeSaveService decide fields centrally

Rejected because it cannot own subsystem semantics, schema evolution or safe native
reconstruction; it coordinates adapters and the aggregate transaction only.
