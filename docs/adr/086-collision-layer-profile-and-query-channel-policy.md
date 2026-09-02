# ADR-086: Collision Layer, Profile and Query Channel Policy

- **Status**: Proposed
- **Date**: 2026-09-02
- **Supersedes**: None
- **Scope**: Stable project identities and response semantics for simulation layers, reusable collision profiles and query channels; project authority, serialization, runtime resolution, mutation, limits, lifecycle, errors, observability and qualification
- **Issue**: [PHY-004.1](https://github.com/abdullahbodur/horo-engine/issues/867)
- **Jira**: [HORO-867](https://horo-engine.atlassian.net/browse/HORO-867)
- **Parent**: [PHY-004](https://github.com/abdullahbodur/horo-engine/issues/831)
- **Related**: [ADR-005](005-submodule-compatibility.md), [ADR-008](008-error-model-exception-boundary-and-registry.md), [ADR-054](054-extension-and-package-authority-boundary.md), [ADR-084](084-canonical-physics-solver-units-and-tolerances.md), [ADR-085](085-physics-shape-authoring-cook-and-runtime-boundary.md)
- **Normative documents**: [Physics Architecture](../architecture/runtime/physics-architecture.md), [Project Model](../architecture/editor/project-model.md), [Scene Runtime](../architecture/runtime/scene-runtime.md), [Built-In Scene Primitives](../architecture/runtime/built-in-scene-primitives.md)

## Context

Physics Architecture currently says that collision layers are stable project
configuration, worlds resolve them into efficient masks and queries declare layer
filters. It does not define the identities, distinguish simulation filtering from
query intent or specify how reusable profiles produce one unambiguous response.

Exposing a solver object-layer number, a serialized bit position or a display name
would couple every scene and gameplay module to one project ordering and one Jolt
adapter layout. Reordering a settings panel could then change collision behavior.
Renaming `Enemy`, adding the sixty-fifth category or changing private broadphase
partitioning could invalidate save data or silently reinterpret a mask.

Simulation pairs and queries also answer different questions. A simulation layer
classifies a collider for body-to-body filtering. A query channel names an intent
such as visibility or camera obstruction. Treating both as one bit-mask makes each
new query intent consume simulation categories and allows asymmetric, hard-to-audit
per-object matrices.

Profiles must provide reuse without becoming mutable ambient state. A collider that
stores "profile 3" cannot explain which project definition it meant, and a profile
rename must not break scenes. Runtime worlds and query snapshots additionally need
immutable filter evidence while project settings can be edited and play sessions,
streaming cells and asynchronous queries overlap.

This ADR fixes the semantic and ownership baseline. PHY-004.2 owns the project
matrix editor and activation validation implementation, PHY-004.3 owns complete
typed query descriptors/hits/ordering, and later PHY-004 tickets own query
execution, snapshots and tick-event projection.

## Decision

### 1. Layers, profiles and channels have distinct typed identities

Horo defines three non-interchangeable opaque 128-bit IDs:

```cpp
using CollisionLayerId = StrongId<CollisionLayerTag, UInt128>;
using CollisionProfileId = StrongId<CollisionProfileTag, UInt128>;
using PhysicsQueryChannelId = StrongId<PhysicsQueryChannelTag, UInt128>;
```

The committed project collision configuration stores each ID as the RFC 9562 UUID
canonical textual form: exactly 36 lowercase ASCII characters in `8-4-4-4-12`
hexadecimal groups separated by hyphens, with no braces or `urn:uuid:` prefix. The
binary/runtime form is the same 128 bits. IDs are nonzero,
generated once by the project-authoring operation and stable across display-name
changes, list reordering, file moves, cook, package and save/load.

The type tag is part of validation even when two raw bit patterns happen to match.
No implicit conversion exists among the three IDs or to a solver object layer,
array index, bit position, string hash or asset ID. Display names and localized
labels are presentation metadata, never runtime identity.

### 2. One committed project document owns the collision schema

The Project domain owns one versioned `ProjectCollisionSchema` in committed
`.horo/collision.json` project configuration. It contains:

- schema identity/version and project identity;
- layer definitions keyed by `CollisionLayerId`;
- one symmetric simulation response matrix keyed by two layer IDs;
- query-channel definitions keyed by `PhysicsQueryChannelId`;
- complete collision profiles keyed by `CollisionProfileId`;
- the explicit default profile ID used by authoring operations;
- canonical limits, feature bits and a semantic fingerprint.

The configuration is the authoring source of truth. Physics validates and consumes
immutable normalized snapshots; it does not own a second editable registry. Scene
documents and collider assets reference exact typed IDs but do not copy project
definitions or masks.

Every project must select an existing default profile. A project template may seed
human-friendly layers, channels and profiles, but those entries remain ordinary
project-owned definitions. Runtime code never compares names such as `Default`,
`WorldStatic`, `Pawn`, `Trigger`, `Visibility` or `Camera` and never assumes a
reserved numeric slot.

### 3. A collision layer classifies simulation participation only

Each enabled collider resolves to exactly one `CollisionLayerId` through its
profile. The layer controls broadphase eligibility and body-to-body simulation
response. It does not describe shape kind, body motion mode, physical material,
query intent, gameplay team, render visibility or event subscriber.

Layer definitions contain stable ID, display metadata and explicit policy flags
needed to validate legal use, including admitted body modes and whether overlap-
only participation is allowed. They do not contain native Jolt object-layer or
broadphase-layer values. Physics derives those private values for each immutable
schema snapshot.

A collider never carries a serialized layer mask. A filter that needs several
classes names a bounded canonical set of layer IDs; the runtime resolves it against
the captured schema generation.

### 4. Simulation pair response is symmetric and project-owned

The project matrix stores one canonical response for every unordered pair of
enabled layers:

```cpp
enum class SimulationPairResponse : std::uint8_t {
    Ignore,
    Overlap,
    Block,
};
```

The diagonal is explicit. Canonical serialization orders a pair by the two raw
typed IDs, so `(A, B)` and `(B, A)` cannot disagree.

- `Ignore` rejects the pair before narrowphase whenever possible. It produces no
  contact constraint and no contact/overlap lifecycle result.
- `Overlap` performs the bounded detection needed for overlap lifecycle state but
  produces no solver contact impulse or positional response.
- `Block` admits contact generation and solver response according to shape,
  material, body-mode and contact policy. It is eligible for contact lifecycle
  results.

`Block` does not guarantee that shapes touch, that a contact survives validation or
that an event subscriber exists. `Overlap` is a collision-filter semantic and does
not change a collider into an editor `TriggerVolume`; trigger ownership and runtime
conversion remain separate typed contracts.

The symmetric matrix response is authoritative: `Ignore` overrides body-mode,
material and contact policy for that pair, and none may promote it to `Overlap` or
`Block`.

Profiles cannot override individual simulation pairs. This preserves one symmetric
source of truth. A different pair behavior requires a distinct project layer or an
explicit transient ignore relationship owned by a later body/contact contract, not
a hidden per-instance response table.

### 5. Query channels name query intent, not target object classes

A `PhysicsQueryChannelId` represents one project-defined query intent. A typed
ray, point, overlap or shape query may select one channel, and each collision
profile declares how its collider responds to that channel.

```cpp
enum class QueryResponse : std::uint8_t {
    Ignore,
    Overlap,
    Block,
};
```

- `Ignore` excludes the collider from results.
- `Overlap` admits a non-blocking hit and does not terminate traversal solely due
  to that response.
- `Block` admits a blocking hit. The query descriptor's collection, ordering and
  maximum-result contract determines when traversal may stop.

The two response enums deliberately use the same words but remain separate types.
A query response never creates a solver contact, and a simulation pair response
does not answer a query. Query descriptors may add explicit, typed inclusion rules
for triggers, bodies/entities, material or layer sets under PHY-004.3; those rules
compose with the selected channel and cannot reinterpret its identity.

Channel definitions contain stable ID, display metadata and documented intent.
They do not reserve solver bits. Removing or changing a channel is a project-schema
compatibility operation because profiles and authored gameplay queries may refer to
it.

### 6. A collision profile is a complete reusable value

Each `CollisionProfile` contains:

- stable `CollisionProfileId` and display metadata;
- exactly one `CollisionLayerId`;
- one explicit `QueryResponse` for every enabled query channel;
- bounded participation flags such as simulation enabled, query enabled and
  overlap/contact lifecycle eligibility;
- its own schema revision and semantic fingerprint contribution.

Profiles do not inherit from other profiles, contain wildcard-by-name rules or
store native masks. An authoring tool may clone a profile, but the result receives
a new ID and a complete deterministic value. This avoids order-dependent cascades
and lets validation prove that every channel has one response.

Authored colliders store one profile ID. They do not carry sparse response
overrides. A distinct reusable behavior is a distinct project profile. Runtime
enabled/disabled state is separate transient body state and never mutates the
profile snapshot.

The project default profile is used only when an authoring command creates a new
collider without an explicit choice. Import, scene migration and runtime loading do
not silently replace a missing/stale profile with the default.

### 7. Canonical normalization rejects ambiguous configuration

Project validation produces a canonical `NormalizedCollisionSchema` by sorting all
definitions by raw typed ID and expanding the complete symmetric layer matrix and
complete profile/channel table. It rejects:

- zero, duplicate or cross-type IDs;
- missing referenced layer, channel, profile or default profile;
- duplicate definitions or duplicate pair entries;
- a missing pair response, asymmetric legacy input or unknown response value;
- a profile missing or duplicating a channel response;
- illegal layer/body-mode or overlap/event combinations;
- unsupported required feature bits, schema versions or limit excess;
- non-canonical IDs and malformed or unsafe display metadata.

Unknown optional presentation fields may survive project round-trip under the
Project Model rules, but unknown semantic fields or responses block activation.
Validation never fills a missing matrix cell or profile response with a guessed
default.

The semantic fingerprint excludes display name, description, editor color, list
order and localization. It includes every typed ID, enabled/retired state, layer
policy, matrix response, channel intent feature and normalized profile field.

### 8. Packed masks and native filter tables are derived private state

At scene candidate preparation, Physics acquires one immutable normalized schema
snapshot and deterministically assigns dense runtime indices by canonical typed ID.
It may compile layer-pair tables, query-response bitsets, broadphase partitions and
Jolt object-layer/filter adapters optimized for the target.

Dense indices, bitsets, native filter objects and their ordering are scoped to one
`PhysicsFilterSchemaGeneration`. They are never serialized, sent through gameplay
or compared across worlds/generations. Debug and event projections translate them
back to typed IDs before leaving Physics.

A body record retains its stable profile/layer IDs plus captured schema generation,
not merely the packed index. Stale or cross-generation packed values fail before
native access. Native filter callbacks read only the world-owned immutable tables;
they do not access project files, ECS, Assets, strings or mutable registries.

### 9. Scenes, assets and saves preserve stable references

Scene/collider authoring stores `CollisionProfileId`. Gameplay query assets or
serialized commands store `PhysicsQueryChannelId` where durable query intent is
needed. A save records Horo IDs and semantic state required by its declared save
schema, never packed indices or native masks.

Scene conversion resolves all referenced profiles against the exact locked project
schema and emits typed runtime bindings with the expected schema fingerprint. A
missing reference is a blocking diagnostic. Renaming/reordering is compatible;
semantic edits require a new schema generation; deleting a referenced definition
is blocked until references are migrated or explicitly retired.

Legacy numeric/name-based input is migrated only by a versioned project migration
with an explicit old-to-new mapping. It generates/persists IDs once and reports
ambiguous or unknown names. Runtime loading never performs that migration.

### 10. Schema changes publish transactionally at Physics safe points

Project edit mode builds and validates a complete candidate collision schema away
from active worlds. Each affected play/preview world resolves every live body and
query dependency, constructs candidate private filter tables and reserves required
capacity before publication.

Publication occurs only at the Physics pre-step structural-change safe point. It
atomically installs the new schema generation, remaps bodies, invalidates affected
broadphase/contact/query-snapshot state and applies the explicit wake/event policy
owned by downstream PHY-004 contracts. Failure retains the old generation and
reports the complete bounded diagnostic set.

Body profile changes are owner-thread commands carrying world, body, old profile,
new profile and expected schema/body generations. They are deferred while stepping
and either commit completely at a safe point or leave the prior profile active.
Gameplay cannot edit project profile or matrix definitions from solver callbacks.

Changing display-only metadata does not require a Physics generation. Changing a
layer, matrix, channel or profile semantic does. Packaged builds normally treat the
schema as immutable for the application lifetime; loading a scene cannot mutate it.

### 11. Query and event consumers receive IDs and captured generation

Immediate queries execute against the owner world's active schema. Snapshot or
asynchronous queries retain a lease on the exact filter schema and broadphase
snapshot generation captured at submission. Results identify the selected query
channel, resolved profile/layer IDs and snapshot generation where the public hit
contract needs them; they never expose native filter values.

Collision/overlap tick events are derived from the symmetric simulation pair
response captured for that tick. A later schema publication cannot relabel an
already produced event. Event projection carries stable layer/profile IDs and the
schema generation needed to diagnose stale consumers.

Ordering, blocking-hit cutoff, overflow, staleness and contact summary details are
owned by PHY-004.3 and later tickets. This ADR fixes only the response meanings and
identity evidence those contracts must preserve.

### 12. Package and extension contributions materialize into project authority

Packages may distribute suggested collision definitions or migration helpers as
data, but enabling a package does not register ambient runtime layers/channels by
name or claim native bit ranges. Importing a preset materializes project-owned IDs
and records provenance under the Project Model transaction.

If a package requires exact semantic definitions, its manifest names stable IDs,
schema versions and fingerprints. Project compatibility validation either admits
the complete requirement or blocks it; it never merges conflicting entries by
display name. Package disable/update cannot remove definitions while scene, query,
world or schema leases still depend on them.

### 13. CanonicalV1 limits are explicit

The initial project schema admits at most:

| Definition | Maximum |
|---|---:|
| enabled collision layers | `64` |
| enabled query channels | `64` |
| collision profiles | `1024` |
| total retained/retired definitions of each ID type | `4096` |

These are validation and memory/work bounds, not serialized bit widths or promises
about Jolt object-layer capacity. A target may derive any private representation
that preserves semantics. Projects may choose lower policy limits. Raising a limit
requires a schema/profile qualification change with bounded memory, activation,
query and filter-callback evidence.

Runtime filter compilation uses checked arithmetic and bounded allocation during
candidate preparation, never inside the fixed step or native callback. A valid
project can still fail world admission when its compiled tables and overlap with an
active generation exceed the world/application Physics budget.

### 14. Compatibility versions semantic schema separately from representation

Project collision schema version, normalized semantic version, private compiled-
filter version, scene/save reference schema and native solver adapter version are
separate. A display-only project change preserves the semantic fingerprint. Any
filter semantic change creates a new immutable generation even if its private
packed layout happens to remain equal.

New required response semantics or identity kinds require an explicit version and
feature bit. Older runtimes reject them. Private compiled tables are rebuildable
cache/state and are never migrated as durable data. A solver upgrade may rebuild a
different native layout without changing project IDs or response meanings.

### 15. Errors and observability remain Horo-owned

Operations return ADR-008 typed errors for malformed/duplicate/unknown/stale IDs,
missing default/profile/channel/pair response, asymmetric matrix, illegal response
or body mode, limit/budget exhaustion, unsupported version/feature, generation
conflict, query snapshot mismatch, mutation during step, cancellation and shutdown.

Diagnostics carry bounded project/schema/world/body/profile/layer/channel IDs,
expected/actual generations, response kind and counts. Display names may be added
as sanitized presentation context but are not the diagnostic key. Native object-
layer numbers, masks, pointers and unbounded pair/result lists are not logged.

Metrics include schema generations/publication failures, enabled layer/channel/
profile counts, compiled-table bytes, per-response pair counts, body counts by
stable layer/profile, query response counts and stale-generation rejections. Stable
typed IDs may appear in bounded debug snapshots; display names and colors are
resolved outside the Physics hot path.

### 16. Ownership and shutdown ordering are explicit

The Project domain owns authored configuration and edit transactions. The Physics
application service owns normalized snapshot/cache generations. Each `PhysicsWorld`
owns a `PhysicsFilterSchemaLease`, derived native tables and body mappings. Query
snapshots, tick-event buffers and debugger snapshots retain bounded leases or copy
stable ID evidence.

Scene/world admission closes before unload. Physics cancels/joins candidate schema
work, drains steps and query submissions, retires bodies and event/debug/query
readers, destroys native filters/world state, then releases schema/project/package
leases. Project close and package removal wait for those leases. Repeated shutdown
and failure after every partial preparation stage are idempotent.

No callback, worker or query result retains references to mutable project storage.
No schema publication blocks while holding a solver lock or waits from a callback.

### 17. Qualification is part of the contract

Required coverage includes:

- stable typed IDs across rename, reorder, project move, cook, package and save;
- type separation and rejection of zero, duplicate, cross-type and native values;
- complete canonical symmetric matrix normalization for all three responses;
- separate simulation/query response truth tables and no cross-domain effects;
- complete profiles, no inheritance/order dependence and explicit default use only
  during authoring creation;
- unknown/missing/retired references and deterministic legacy migration;
- deterministic packed layout from canonical IDs and equivalence across different
  authoring list orders;
- private Jolt adapter parity with the Horo response matrix and no native leakage;
- profile/body changes deferred during step and atomic candidate publication/
  rollback across active worlds;
- immediate and leased snapshot queries across schema replacement;
- event identity bound to the tick's captured schema generation;
- maximum/over-limit counts, checked allocation, active/candidate overlap budget
  and no hot-path allocation;
- scene/cell unload, package removal, project close, pending query/debug/event
  leases and shutdown after each partial state;
- fuzzing project schema IDs, counts, pair/profile tables, versions and feature bits.

Golden fixtures compare stable Horo IDs and semantic responses, not dense index or
native mask equality. Tests run against the canonical solver profile and headless
composition because filtering is Renderer-independent.

## Consequences

Scenes, saves and gameplay queries retain meaningful project-stable references
while Physics can compile efficient solver-specific tables privately. Simulation
pair behavior is symmetric and auditable, query intent remains independent, and a
profile produces one complete reusable collider policy.

The cost is explicit project schema management, migration tooling, bounded table
compilation and overlapping immutable generations during safe publication. Projects
cannot patch arbitrary per-instance response maps or use convenient serialized
bitmasks; they create deliberate layers/profiles or use separately specified
transient body relationships.

## Rejected Alternatives

### Serialize solver object-layer numbers or 64-bit masks

Rejected because bit positions and native layer capacity are private representation
details. Reorder, platform or solver changes would silently reinterpret durable
data and expose Jolt coupling.

### Use display names or string hashes as identity

Rejected because rename/localization/case/normalization and hash collisions would
change or ambiguously resolve behavior. Names remain presentation metadata.

### Use one category system for simulation layers and query channels

Rejected because body-to-body class and query intent evolve independently. A
shared bit space couples unrelated limits and cannot express separate response
semantics clearly.

### Store asymmetric response maps on every profile or collider

Rejected because two colliders could disagree on the same simulation pair and the
resolution rule would become hidden order-dependent policy. One project-owned
symmetric matrix is authoritative.

### Allow profile inheritance and sparse runtime overrides

Rejected for CanonicalV1 because cascades introduce order/version dependencies and
make complete validation, fingerprinting and deterministic publication harder.
Profiles are complete values; authoring tools may clone them.

### Fall back to the default profile when a reference is missing

Rejected because it converts schema or package errors into silent collision
changes. The default is an authoring convenience, not a runtime recovery policy.

### Let packages register global layers/channels during startup

Rejected because activation order, package disable and project conflicts would
mutate ambient filter identity. Presets materialize transactionally into project
authority or exact requirements fail compatibility validation.
