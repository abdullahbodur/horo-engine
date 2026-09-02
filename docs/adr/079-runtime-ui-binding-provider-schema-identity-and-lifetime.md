# ADR-079: Runtime UI Binding Provider Schema, Identity and Lifetime

- **Status**: Proposed
- **Date**: 2026-09-02
- **Supersedes**: None
- **Scope**: Runtime UI binding provider/property/schema/instance identity, typed values and read/write capability, descriptor registration, snapshot/read and command/write boundaries, game/player/scene/module lifetime, revocation, module unload, errors, security, compatibility, and shutdown
- **Issue**: [RUI-006.1](https://github.com/abdullahbodur/horo-engine/issues/747)
- **Jira**: [HORO-747](https://horo-engine.atlassian.net/browse/HORO-747)
- **Parent**: [RUI-006](https://github.com/abdullahbodur/horo-engine/issues/746)
- **Related**: [ADR-008](008-error-model-exception-boundary-and-registry.md), [ADR-018](018-command-registration-permissions-threading-and-packaged-build-policy.md), [ADR-054](054-extension-and-package-authority-boundary.md), [ADR-073](073-runtime-ui-ownership-scope-and-update-order.md)
- **Normative documents**: [Game UI and HUD](../architecture/runtime/game-ui-and-hud.md), [Runtime Lifecycle](../architecture/runtime/runtime-lifecycle.md), [Scene Runtime](../architecture/runtime/scene-runtime.md), [Internal Module Descriptor](../architecture/foundation/internal-module-descriptor.md)

## Context

Runtime UI needs health, inventory, settings, route, player, scene and module-owned
data without reading ECS/components or arbitrary object properties directly. A
binding must survive cook and identify the same semantic property after code moves,
while runtime instances must not outlive their game, player, scene or providing
module. Two-way UI also needs explicit write authority instead of exposing mutable
references or letting widget callbacks write gameplay state.

String property paths and reflection lookup are tempting but make type/access errors
late, add per-frame parsing/hash work and let a renamed C++ member silently change a
portable UI contract. A process-global provider registry or service locator would
let scene/player state leak across owners and block module unload. Calling provider
getters during layout/render would introduce hidden locks, allocations, side effects
and non-deterministic snapshots.

ADR-073 requires Runtime UI to read immutable committed binding snapshots during
VariableUpdate and emit typed owner commands for external mutation. It does not
define schema identity, provider registration/revocation, read/write capabilities or
scope resolution. This decision supplies that contract. RUI-006.2 defines authored
binding descriptors, RUI-006.3 dirty/batched update propagation, RUI-006.4 two-way
conflict policy, and RUI-006.5/.6 actions/asynchronous state on top of this owner and
lifetime boundary.

## Decision

### 1. Provider owners publish data; Runtime UI owns binding resolution

The responsibility split is:

| Responsibility | Owner |
|---|---|
| Stable provider/property schemas and snapshot/write adapter implementation | Providing gameplay/application/module domain |
| Contribution validation, registry generation, binding resolution, snapshot consumption and UI dependency lifetime | `RuntimeUiService` binding registry |
| Authoritative mutable state and write conflict/permission validation | Provider's game/player/scene/module owner |
| UI document/style binding descriptors and cook validation | Runtime UI model/cook using registered schema snapshots |
| Provider/module activation, trust and callback leases | Host composition/ExtensionHost |
| Layout/render/input | Consumers of resolved immutable UI state only |

Runtime UI does not discover providers through a service locator, inspect ECS/
script/C++ members, call arbitrary reflection, own gameplay state or invoke a
provider during render/layout. Providers cannot walk/mutate UI trees, return widget
pointers or register themselves from constructors/static initialization.

The application/host composition root supplies one validated provider-type registry
generation to each game runtime. Runtime provider instances enter through explicit
`RuntimeUiBindingCapability` commands and leases.

### 2. Type, property, instance and snapshot identities are distinct

The model uses:

- `UiBindingProviderTypeId`: stable namespace-qualified semantic provider type;
- `UiBindingPropertyId`: stable property identity unique within one provider type;
- `UiBindingSchemaVersion`: explicit major/minor plus canonical schema fingerprint;
- `UiBindingProviderInstanceId`: generation-checked runtime instance plus owner
  scope/generation;
- `UiBindingSnapshotId`: immutable provider instance/schema/value revision;
- `UiBindingLeaseId`: runtime binding dependency retained by one UI generation.

Serialized/cooked UI references provider type, property, minimum compatible schema,
typed owner selector and direction/required policy. It never stores a C++ member
name, memory offset, reflection path, component pointer, provider slot, service key,
editor object, native handle or runtime instance ID.

Display names/documentation are presentation metadata. IDs are normalized, bounded,
owned by a module/package namespace and never reused for a different semantic value
within a schema major version.

### 3. Provider descriptors are inert, finite metadata

A provider type contribution contains no live provider object:

```cpp
struct UiBindingProviderDescriptor {
    UiBindingProviderTypeId type;
    ModuleId ownerModule;
    UiBindingSchemaVersion schema;
    std::span<const UiBindingPropertyDescriptor> properties;
    UiBindingProviderScopeMask allowedScopes;
    UiBindingProviderFlags flags;
};
```

Creating, parsing or validating a descriptor performs no global registration,
service lookup, instance creation, callback, file/network access or ambient state
mutation. The host validates a complete bounded contribution batch before module/
game activation and atomically publishes one registry generation.

Duplicate provider/property IDs, foreign namespace ownership, unsorted/duplicate
schemas, incompatible types/access, invalid limits/privacy, empty scope masks or a
schema fingerprint mismatch reject the complete candidate. Registration/load order
never resolves a collision. Descriptors and property tables are immutable for one
registry generation.

Extension contributions follow ADR-054 install/trust/activation identity and retain
`ModuleCallbackLease`/registry leases. Descriptor visibility does not grant a
provider permission, activate an instance or keep a module loaded by itself.

### 4. Property schemas are closed typed contracts

Each property declares:

```cpp
struct UiBindingPropertyDescriptor {
    UiBindingPropertyId id;
    UiBindingValueType type;
    UiBindingAccess access;
    UiBindingUpdateKind update;
    UiBindingPrivacyClass privacy;
    UiBindingValueLimits limits;
    UiBindingPropertyFlags flags;
};
```

Version 1 value types include bounded UTF-8 text, localized-message reference,
boolean, signed/unsigned integer, checked fixed/logical scalar, enum/flags with a
registered schema, vector/color, stable Asset/Entity/domain ID, optional of an
admitted scalar/ID type and bounded immutable list/record with a registered schema.
There are no `void*`, `std::any`, arbitrary JSON/map, native object, script object,
renderer handle or mutable span values.

`UiBindingAccess` is `Read`, `WriteCommand`, or `ReadWriteCommand`. Write access
means the owner accepts a typed command; it never exposes a mutable reference/setter
callback to UI. Properties also declare snapshot/copy limits, required/optional
availability, nullability, thread/safe-point owner and whether a value can affect
layout, paint, accessibility or actions.

Privacy classes forbid credentials, authentication secrets, private native device
data and unrestricted user-sensitive content from ordinary UI binding. Restricted
properties require an explicit host capability/presentation policy and remain
redacted in diagnostics/observability.

### 5. Schema evolution is fingerprinted and directional

`UiBindingSchemaVersion` uses major/minor compatibility:

- a major change may remove/retype/reinterpret an ID and requires document recook/
  migration;
- a minor change may add optional properties or widen a declared non-semantic limit,
  but cannot change existing property type, access, scope, privacy or meaning;
- a provider is compatible when major matches, provider minor is at least the
  descriptor's minimum and every referenced property signature fingerprint matches.

The canonical schema fingerprint covers provider/property IDs, types/nested schema,
access, scopes, privacy, required flags, limits that affect semantics and owner safe-
point policy. Source declaration order and display text are excluded after canonical
sorting/normalization.

Cook records exact referenced property signature fingerprints and the admitted
provider schema range. Runtime validates them against the current registry before
preparing a binding. Unknown/newer required semantics or expired older versions
reject with migration/recook guidance; no reflection/name fallback is attempted.

### 6. Provider instances have one exact semantic owner scope

`UiBindingProviderScope` is a closed variant:

```cpp
enum class UiBindingProviderScopeKind : std::uint8_t {
    GameInstance,
    Player,
    Scene,
    Module,
};
```

- `GameInstance` names one game runtime generation and ends before it;
- `Player` names one exact local player/session generation and may survive scene
  replacement only with that player;
- `Scene` names one exact `SceneRuntimeId` and enters/leaves through the scene UI
  activation/unload barrier;
- `Module` names one exact module activation generation and may provide immutable/
  application-level values to admitted game runtimes through explicit injection.

One instance chooses one scope and cannot migrate. A Module-scoped provider is not a
process-global mutable singleton: each consuming game runtime receives an explicit
leased binding to the module activation generation, and module revocation ends it.
Viewport attachment does not change provider ownership.

Binding owner selectors are typed and relative: `OwningGame`, `OwningPlayer`,
`OwningScene`, explicit stable player/scene role, or exact admitted module/provider
identity. Resolution does not choose a nearest/latest/first registered provider.
Ambiguous/missing required selection fails.

### 7. Instance activation and binding resolution are transactional

Provider instances follow:

```text
Created -> Preparing -> Ready -> Active -> Revoking -> Draining -> Retired
               \-> Failed
```

Preparation validates registry/schema/scope/owner generations, capabilities and
resource limits, creates private adapter state and obtains the initial immutable
snapshot without publishing. Activation commits at the provider owner/ADR-073 safe
point and returns a generation lease.

A UI binding candidate resolves its typed selector to one active provider instance,
validates property schema/direction/privacy and acquires a `UiBindingLeaseId` before
the UI generation publishes. Required failure retains/rejects the UI candidate under
its activation policy. Optional absence uses only the descriptor's typed fallback
and remains observable.

Providers cannot appear/disappear midway through layout/extraction. Instance and UI
binding publication is all-or-nothing for one VariableUpdate generation.

### 8. Reads consume immutable committed snapshots

Each active provider publishes bounded immutable `UiBindingSnapshot` values from
its authoritative owner at its declared safe point. A snapshot contains exact
provider instance/schema/owner/value revisions and canonically ordered typed property
values or an immutable view with a lease. It contains no provider pointer/callback,
mutable ECS storage, locks, iterators or lazy getter.

At Runtime UI VariableUpdate start, the binding registry freezes one compatible
snapshot set. All bindings/layout/style/text/actions in that update read the same
property revisions. Renderer/extraction receives only already-resolved UI values;
it cannot request provider data.

Snapshot production may be dirty/batched under RUI-006.3, but baseline semantics are
a complete coherent revision. Provider work completing after the cutoff is applied
next update. Runtime UI never blocks waiting, calls a synchronous cross-owner getter
per element or retains a borrowed value past its lease.

### 9. Writes are typed owner commands with explicit authority

For a `WriteCommand` property, Runtime UI emits:

```cpp
struct UiBindingWriteCommand {
    UiBindingProviderInstanceId provider;
    UiBindingPropertyId property;
    UiBindingSchemaVersion schema;
    UiBindingValue value;
    UiBindingSnapshotId expectedSnapshot;
    UiActionOrigin origin;
    UiBindingWriteSequence sequence;
};
```

The provider owner validates type/range/access, scope/player authority, permission,
expected revision/conflict policy, current lifecycle and gameplay/application rules
at its safe point. It then accepts/rejects atomically and publishes a later snapshot/
typed completion. Runtime UI does not optimistically mutate authoritative provider
state unless the binding descriptor declares a separate UI-local pending projection.

Read access never implies write. A module/property must explicitly advertise and the
host must admit write capability. UI callback/action identity is evidence, not owner
authority. Commands cannot invoke arbitrary function names, pass native/script
objects or hold provider references. RUI-006.4 owns detailed conflict/rollback UX
within this boundary.

### 10. Revocation closes admission before lifetime ends

Revocation begins on game/player/scene teardown, explicit instance removal, provider
failure, schema/registry replacement, module unload/reload, trust/capability loss or
shutdown. The owner atomically marks `Revoking`, closes new binding/write admission,
publishes provider-unavailable/terminal evidence and cancels owned asynchronous
preparation/commands.

Runtime UI invalidates affected binding leases, applies required/optional policy,
publishes a candidate UI generation without stale provider values and releases old
snapshots only after layout/interaction/render/action leases retire. Pending writes
complete as cancelled/revoked against the old generation and cannot target a new
instance reusing the slot.

`Draining` waits boundedly for snapshot, callback and command leases. Module unload
cannot unmap code/data until every provider instance/adapter callback and UI lease
for that module generation is retired. Shutdown revokes scene/player/game providers
before Runtime UI, then module providers before ExtensionHost/module code disappears;
repeated revocation/shutdown is idempotent.

### 11. Editor preview and tooling use schemas, not live runtime pointers

The editor may browse immutable provider schemas, author bindings and validate
documents against a selected composition profile. Preview uses explicit stub/
fixture providers with their own preview game/player/scene/module generations and
the same snapshot/command contracts.

Editor selection, inspector objects, ImGui widgets and live play-session provider
pointers are never serialized or borrowed into authoring preview. Applying a write
from an editor tool requires the same explicit application capability and expected
runtime generation as any other adapter. Closing preview/play/project revokes its
providers without affecting another runtime.

CLI/MCP/schema tools receive redacted typed descriptor projections and validation
results; descriptor inspection never activates a provider or grants write access.

### 12. Errors, limits and compatibility are typed

Errors follow ADR-008 with stable reason codes for unknown/duplicate/foreign type or
property, schema/signature/version mismatch, invalid value/access/privacy/scope,
ambiguous/missing instance, stale owner/snapshot/registry, unavailable/revoking
provider, permission/conflict, snapshot/command/callback capacity, cancellation and
module unload/shutdown. Context includes bounded module/provider/property/schema/
scope/revision evidence and redacts values by privacy class.

Limits cover provider types/properties/nested schemas, descriptor batch bytes,
instances per scope, snapshot bytes/values/lists/text, active binding leases, dirty
updates, writes/completions, retained generations and diagnostics. Exhaustion returns
typed backpressure/failure; it never truncates required values, drops revocation or
reuses storage with outstanding leases.

No provider/domain/native type crosses the Horo public binding value/snapshot/
command contracts. Headless ModelOnly compositions may register fixture/real
providers without Renderer/Platform/editor. A required provider absent from the
composition fails explicitly; no hidden global default is created.

### 13. Verification is part of the contract

Required coverage includes:

- stable type/property IDs, namespace ownership, duplicate batches, canonical
  fingerprints and inert descriptor construction/validation;
- every scalar/optional/list/record value type, range/size/nullability, cross-type
  rejection, privacy/redaction and prohibited pointer/native/script values;
- major/minor evolution, added optional properties, illegal retype/access/scope/
  privacy change, newer/older/runtime recook behavior;
- GameInstance/Player/Scene/Module instances, ambiguous/missing selectors, scene
  replacement, player removal, multiple game runtimes and no scope migration;
- transactional instance/UI binding activation, required/optional fallback and
  initial snapshot failure;
- coherent per-update snapshots, dirty completion after cutoff, concurrent producer,
  no layout/render callback/getter/lock and lease expiry;
- read-only/write/read-write command capability, type/range/permission/expected-
  revision rejection, pending projection and stale/duplicate command sequence;
- game/player/scene teardown, schema registry replacement, module reload/unload,
  callback/snapshot/write draining, late completion and repeated shutdown;
- editor schema browsing, isolated fixture preview, play/project close and no live
  pointer/editor state serialization;
- descriptor/snapshot/command capacity pressure, malformed/version-skewed data,
  bounded/redacted diagnostics and deterministic headless fixtures.

Property/fuzz tests target descriptor/schema/value/cooked-binding validation and
generated provider lifecycle/write sequences. UI screenshots cannot replace schema,
snapshot, command and revocation contract tests.

## Consequences

Runtime UI can bind typed game/player/scene/module data without reflection, mutable
references or lifetime leaks. Every read is coherent for one UI update, every write
is an owner-validated command, and module/scene/player unload can prove callbacks and
snapshots have drained before code/state disappears.

The cost is explicit schema/type/property IDs, canonical fingerprints, immutable
snapshots, owner selectors, leases, write commands and revocation barriers. Providers
cannot expose arbitrary object graphs or synchronous getters for convenience.

## Rejected Alternatives

### Use string reflection paths into gameplay/ECS objects

Rejected because type/access/lifetime errors become late, refactors silently break
content and hot paths parse/hash strings. Bindings target versioned property IDs.

### Register providers from constructors or static initialization

Rejected because descriptor construction would mutate ambient state and ordering/
shutdown would become implicit. The composition root publishes validated batches.

### Keep one process-global provider registry and singleton instances

Rejected because game/player/scene/module lifetimes and multiple runtimes cannot be
isolated. Registries/instances are generation and scope bound.

### Let UI hold provider pointers or call getters during layout/render

Rejected because it introduces dangling references, locks/side effects and mixed
revisions. UI reads immutable committed snapshots only.

### Expose mutable references or direct setters for two-way binding

Rejected because UI would bypass owner authority, conflict policy and safe points.
Writes are typed expected-revision commands.

### Resolve missing providers by type name or nearest scope

Rejected because load order and ambient context are not semantics. Typed owner
selectors must resolve one exact instance or fail/fallback explicitly.

### Unload a module immediately after removing registry entries

Rejected because snapshots, callbacks, writes and old UI generations may still
reference its code/data. Revocation closes admission and drains leases first.

### Treat read support as implicit write permission

Rejected because presentation visibility does not grant mutation authority. Access,
host capability, owner validation and conflict policy are independent.
