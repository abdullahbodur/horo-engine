# ADR-099: Replication Ownership, Authority and Compatibility

- **Status**: Proposed
- **Date**: 2026-09-02
- **Supersedes**: None
- **Scope**: Replicated-state declaration, capture, transport and apply ownership; standalone/server/autonomous/simulated roles; stable schema and FieldId identity; compatibility, registration, lifecycle, malformed input and safe-point behavior
- **Issue**: [NET-003.1](https://github.com/abdullahbodur/horo-engine/issues/1120)
- **Jira**: [HORO-1120](https://horo-engine.atlassian.net/browse/HORO-1120)
- **Related**: [ADR-092](092-character-controller-determinism-and-state-composition.md), [ADR-097](097-default-real-time-transport-backend.md), [ADR-098](098-protocol-session-and-trust-policy.md)
- **Normative documents**: [Multiplayer Replication Architecture](../architecture/runtime/multiplayer-replication-architecture.md), [Networking Architecture](../architecture/runtime/networking-architecture.md), [Scene Runtime](../architecture/runtime/scene-runtime.md), [Gameplay Behavior Authoring](../architecture/extensions/gameplay-behavior-authoring.md)

## Context

The replication architecture described server authority but represented fields by
string `PropertyPath`, implied automatic dirty-property collection and did not say
which subsystem owns declaration, snapshot capture or application. That ambiguity
invites scanning component memory, serializing C++ layout, treating a local client
as authoritative, or mirroring arbitrary event-bus traffic. Each would create an
unstable wire format and a second mutation authority beside Scene and Gameplay.

Replication crosses four different concerns. A gameplay/component owner knows the
semantic state and valid mutations. Scene owns entity/component lifetime and safe
points. NetworkRuntime knows sessions, interest, baselines and wire scheduling. The
transport only moves protected messages. The contract must preserve each owner
instead of centralizing mutable gameplay state in networking.

Server, autonomous client and simulated client may exist in one process (for
example a listen-server/editor preview) and may use separate world generations.
Process locality, player index, input device, object visibility or possession is
not authority. Roles and grants must be explicit, world-scoped and generation-
checked.

## Decision

### 1. Declaration, state, capture, transmission and apply have distinct owners

| Responsibility | Owner |
|---|---|
| Semantic field meaning, stable schema/field IDs, codecs, validation and capture/apply adapters | Declaring gameplay/component module |
| Canonical runtime values, entity/component lifetime and mutation safe points | Owning Scene/Gameplay system |
| Schema registry snapshot, network object identity, dirty hints, capture orchestration, baselines, interest, serialization, transport submission and receive validation | `NetworkRuntime` |
| Protected message delivery and connection lifecycle | Injected `INetworkTransport` |
| Product role/trust/session composition | Host plus ADR-098 admission policy |

The declaring module does not own sockets, peer/session routing or baselines.
`NetworkRuntime` does not own canonical gameplay fields and cannot write component
memory directly. Scene does not interpret wire packets. Transport does not know
replication schemas or authority.

A declaration is inert metadata until the host composes and validates a registry
snapshot. Descriptor construction cannot inspect a service locator, register
globally, capture live objects or mutate state.

### 2. Execution roles are explicit capabilities, not local heuristics

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

The host creates a role for a specific runtime world; the server creates object-
level ownership/submission grants under its authority epoch. Every capture, input
submission, snapshot apply and RPC check validates scene, epoch, object generation,
session generation and role. A listen server uses distinct server/client world
roles even when both live in one process.

Role capabilities are:

| Role | Canonical write/capture | Network submission | Received-state apply |
|---|---|---|---|
| Standalone | Local gameplay systems own normal canonical mutation; no network grant exists | None | None |
| Authority server | May capture canonical declared state at the owner safe point and publish authoritative spawn/update/despawn | Validates typed client submissions; sends authoritative state | Applies only explicit server-owned administrative/import commands, not client snapshots |
| Autonomous client | Cannot author server state; may maintain separately declared local prediction/presentation state | May submit bounded typed input/commands permitted by its server grant | Applies authoritative corrections/snapshots through the schema adapter |
| Simulated client | No server-state or prediction authority | No object-authoritative submission | Applies authoritative snapshots through the schema adapter for presentation/simulation |

Autonomous does not mean authoritative. Prediction is an opt-in capability owned by
a later policy; it cannot change the server's canonical state or schema rules.
Possession, local input, local player ID, same process and successful transport
connection never synthesize a grant.

### 3. Schemas and FieldId values are durable semantic identity

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
non-zero stable 32-bit numeric ID scoped by that schema. IDs are allocated once,
persist across rename/reorder/refactor and are never reused after publication.
Removed IDs remain tombstoned. Display names, C++ member names, editor labels,
JSON paths, component order, reflection indices, addresses and byte offsets are
not identity and never appear in the wire contract.

Each field has one registered semantic value type, canonical bounded codec,
condition, requiredness, write policy and limits. A type/meaning change that cannot
preserve compatibility allocates a new `FieldId` or a new major schema with an
explicit translator. Quantization/compression is codec policy; it does not change
the gameplay owner's canonical field or permit lossy values to become restore/hash
authority.

RPCs and client input use separate stable schema/RPC/command IDs. A generic event
type or event-bus subscription is not a replication declaration.

### 4. Registration freezes one immutable schema-set generation

Modules contribute descriptors during host composition before network world/session
activation. Registry validation rejects:

- duplicate/zero schema or field IDs, reuse of tombstoned IDs and foreign owners;
- empty/overflowed schemas, unsorted or duplicate fields and invalid version rules;
- missing/unregistered codecs/types, unbounded limits or unsupported conditions;
- capture without apply support for a receive role, write-policy contradictions
  and adapter ownership/lifecycle mismatches;
- ambiguous compatibility edges, cycles or translators that do not produce the
  declared target schema.

Validation canonicalizes fields by numeric `FieldId` and computes a domain-
separated schema-set fingerprint consumed by ADR-098 negotiation. A failure
publishes nothing. Runtime sessions pin the immutable registry generation.

Hot reload builds and validates a replacement generation off-thread, then swaps it
at the owner safe point for new worlds/sessions. Existing sessions keep the old
generation or close/re-negotiate under explicit policy. A module cannot unload
while a registry, queued snapshot, baseline, adapter call or active session pins
its generation. Retirement waits for bounded drain; shutdown cancels work and
releases pins in owner order.

### 5. Network object identity never aliases ECS storage

The authority server allocates `NetworkObjectId` plus generation and maps it to a
scene-qualified generation-checked `EntityRef`. IDs are unique within the server
authority epoch and are not ECS indices, pointers, authored scene IDs, names or
paths. Reuse increments generation; stale spawn/update/despawn messages fail.

Spawn publishes object ID/generation, schema ID/version and the required initial
field set before any delta. Despawn retires the mapping at a defined tick. Scene
destruction/reload invalidates the authority epoch so late messages cannot target a
replacement world or reused entity slot.

### 6. Capture is explicit, immutable and owner-safe

At the fixed network capture safe point, `NetworkRuntime` selects authoritative,
relevant objects and invokes their registered capture adapters with a validated
read-only owner-thread view, schema generation and preallocated bounded writer.
The adapter emits canonical field values by `FieldId`.

Capture cannot retain component pointers/views, mutate state, call transport,
register fields or perform unbounded allocation/blocking I/O. NetworkRuntime may
use explicit owner-provided revision counters or `MarkReplicationDirty(object,
FieldId)` as scheduling hints. A dirty hint is neither authority nor a value; the
capture adapter remains the value source.

NetworkRuntime never scans ECS/component memory, reflects arbitrary members,
serializes `sizeof(T)`, diffs padding or infers fields from editor metadata. It owns
immutable captured snapshots and baselines after capture returns, not the source
state.

### 7. Wire records are schema-versioned and bounded

Every state record carries at least:

- authority epoch, network object ID/generation and authoritative simulation tick;
- schema ID and negotiated schema version;
- spawn/update/despawn kind plus baseline identity where applicable;
- bounded field count and canonical ascending `FieldId` entries;
- each entry's wire type/codec tag, bounded byte length and canonical value bytes.

The receiver validates session `Active`, authority direction, epoch, object
lifecycle, schema generation/version, record/field counts, ordering, duplicate IDs,
types, lengths, codec bounds and total work before allocating or decoding. Native
struct layout, endianness, padding, RTTI and pointer values never cross the wire.

Malformed records fail atomically. No partial object mutation is visible and no
unknown bytes are copied into component storage. Repeated hostile records follow
the ADR-098 abuse/close policy.

### 8. Compatibility is explicit per schema version

Major versions are incompatible unless the declaring owner registers a bounded
deterministic translator. Within a compatible major, a minor version may add an
optional field with a canonical default. Removing a field tombstones its ID;
changing requiredness, semantic type or meaning requires a version rule and often
a new ID/major.

Negotiation chooses one explicit compatible schema-set projection. The sender
encodes only that projection. Unknown required schemas/fields, missing required
fields, incompatible type/codec, unsupported translator or fingerprint mismatch
fails admission or the affected session according to policy. Unknown optional
length-delimited fields may be skipped only when the negotiated compatibility rule
authorizes it.

Translators operate on bounded typed field records, not raw component memory. They
are pure with respect to world/gameplay state, cannot invent authority and are
covered by canonical fixtures. There is no best-effort name/path match or implicit
version downgrade.

### 9. Apply validates then commits through the state owner

`NetworkRuntime` decodes an entire record into a bounded typed apply command and
validates role, session, authority, object and schema before queueing it for the
target world. At the declared owner-thread safe point, Scene revalidates scene/
entity generation and invokes the schema owner's apply adapter.

The adapter validates semantic ranges and commits all accepted fields as one owned
mutation or rejects the command without partial visibility. It may update
replica/presentation or declared prediction-correction state according to role. It
cannot write an authority-server object from a client snapshot, bypass component
invariants, perform structural mutation outside the Scene command boundary or
retain decoded views after return.

Client input/commands are not replicated state writes. They use typed schemas,
rate/permission checks and server-owned gameplay handlers; the server decides
whether and how canonical state changes.

### 10. Events and diagnostics are projections, not wire authority

Gameplay events may mark a declared field dirty or cause gameplay to mutate owned
state. NetworkRuntime then captures that declared state. It does not subscribe to
the generic event bus and serialize arbitrary event payloads. Network RPC/event
schemas are separately registered, direction-scoped and bounded.

Diagnostics use schema/field/object IDs, versions, role, safe failure reason,
counts and timing. They omit secret/session proofs and field payloads by default.
Display paths/names may be resolved locally for tools but never affect decode,
authority or compatibility.

### 11. Qualification covers registration through retirement

Focused automated coverage proves:

- valid descriptor registration, canonical ordering/fingerprint and duplicate/
  zero/tombstoned/foreign ID rejection;
- missing codec, invalid type/limits/condition/write policy, overflowed schemas and
  malformed compatibility graph/translator rejection;
- standalone/server/autonomous/simulated capability matrices, including same-
  process listen-server worlds without locality-derived authority;
- stable field identity across rename/reorder/layout changes and rejection of
  `PropertyPath`, offset, pointer and raw-memory fixtures;
- exact/additive-minor/major-translated compatibility plus missing required,
  unknown, duplicate, reordered, wrong-type and oversized field records;
- spawn/update/despawn ordering, stale object/scene/session/authority generations,
  disconnect, world reload and module hot-reload/unload pinning;
- capture/apply safe-point ownership, atomic apply failure, dirty-hint behavior,
  cancellation, queue/budget overflow and shutdown with queued snapshots/applies;
- explicit proof that generic event publication and arbitrary ECS component
  mutation produce no network traffic without a registered schema/dirty command.

Fuzz/property coverage targets descriptor parsing, compatibility negotiation,
record framing and field codecs under configured memory/work limits.

## Consequences

### Positive

- Gameplay and Scene retain canonical state and mutation ownership.
- Stable schema/`FieldId` values survive C++ refactors and editor renames.
- Server authority is explicit per world/session/object generation, including in a
  listen-server process.
- Compatibility and malformed input are decided before component mutation.
- NetworkRuntime can optimize snapshots/baselines without learning native layouts.
- Standalone products do not pay mandatory transport, history or replication work.

### Negative

- Gameplay/component owners must define and maintain schemas, codecs and adapters.
- Schema evolution requires ID discipline, tombstones, fixtures and explicit
  translators.
- Capture/apply indirection adds registry and safe-point integration work.
- Automatic reflection-based replication is intentionally unavailable.

## Rejected Alternatives

### String `PropertyPath` as wire identity

Names and paths change during refactors, localization and hierarchy edits and can
be ambiguous. Stable numeric `FieldId` scoped by a stable schema is normative.

### Serialize component/ECS memory automatically

C++ layout, padding, pointers, endianness and transient fields are not semantic or
portable. Raw scanning also bypasses codecs, bounds and state-owner invariants.

### Replicate the generic event bus

Local events have different lifetime, audience and payload guarantees. Mirroring
them creates ambient network authority and an unbounded accidental protocol.
Network commands/RPCs require explicit typed schemas.

### Grant authority to the local player or local process

A listen server contains server and client roles together, and compromised clients
are still local to themselves. Only explicit world/session/object grants determine
capability.

### Let NetworkRuntime own canonical gameplay state

This creates a second source of truth and lets networking bypass Scene/Gameplay
safe points. NetworkRuntime owns captured snapshots and wire orchestration only.

### Apply decoded fields directly into components

Direct writes bypass generation checks, semantic validation, invariants and atomic
commit. Apply must go through the declaring owner's adapter at the Scene safe point.

### Best-effort compatibility by matching names or layouts

Implicit matching hides semantic drift and produces peer-dependent results.
Compatibility is versioned, fingerprinted and explicit.
