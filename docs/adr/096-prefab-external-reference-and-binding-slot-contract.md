# ADR-096: Prefab External Reference and Binding Slot Contract

- **Status**: Proposed
- **Date**: 2026-09-02
- **Supersedes**: None
- **Scope**: Prefab-local and asset reference classes, prohibited scene-external capture, stable typed binding-slot declarations and instance bindings, create-from-selection boundary reporting, nested re-exposure, cook/spawn validation, lifetime, diagnostics and transactions
- **Issue**: [PFB-001.11](https://github.com/abdullahbodur/horo-engine/issues/1086)
- **Jira**: [HORO-1086](https://horo-engine.atlassian.net/browse/HORO-1086)
- **Related**: [ADR-017](017-prefab-role-ownership-and-capability-tiers.md), [ADR-093](093-prefab-override-property-identity-and-delta-operations.md), [ADR-094](094-prefab-nested-composition-and-variant-inheritance.md), [ADR-095](095-prefab-cook-boundary-and-artifact-model.md)
- **Normative documents**: [Prefab Architecture](../architecture/runtime/prefab-architecture.md), [Scene Runtime](../architecture/runtime/scene-runtime.md), [Gameplay Behavior Authoring](../architecture/extensions/gameplay-behavior-authoring.md), [Editor Document Model](../architecture/editor/editor-document-model.md)

## Context

A portable prefab can safely refer to objects inside its own effective hierarchy and
to registered assets. It cannot preserve a scene-local object pointer, `EntityRef`
or component address because those identities belong to one authoring document or
runtime scene generation. Copying such a value into `.prefab` would make the asset
path-dependent, scene-dependent or silently stale after duplication and spawn.

Some reusable templates legitimately need an external relationship: a door needs
the room controller, a trigger needs a scene-owned objective, or a behavior needs a
specific component on a caller-selected entity. Silently nulling these references
during create-from-selection loses intent; copying the external object changes
ownership; retaining it leaks the scene boundary. An explicit typed binding slot is
the portable interface for these cases.

Binding slots must not become a second override algebra or an untyped property bag.
ADR-093 owns authored value deltas. Spawn initialization parameters are copied
values that configure creation. A binding slot instead establishes a generation-
checked relationship to an external object/component chosen by the containing
instance or spawn caller. This ADR fixes those distinctions and their transactional
failure behavior.

## Decision

### 1. Persisted references have four closed classes

| Class | Serialized in prefab source | Meaning |
|---|---:|---|
| `PrefabLocalReferenceV1` | Yes | Stable object/component inside the effective prefab scope |
| `AssetReferenceV1` | Yes | Stable `AssetId`, expected `AssetTypeId` and optional registered subresource ID |
| Direct scene-external reference | No | Scene object/component, runtime `EntityRef`, pointer, handle, path or query outside the prefab |
| `PrefabBindingSlotReferenceV1` | Yes | Reference to a declared typed external-binding interface slot |

Unknown reference kinds fail authoring validation and cook. Display names, hierarchy
indices, component array positions, filesystem paths, pointer values and runtime
handles never become durable reference identity.

`PrefabLocalReferenceV1` uses ADR-093/094 identity: bounded nested placement scope,
target `LocalObjectId`, and—when targeting a component—registered
`ComponentTypeId` plus persisted `ComponentInstanceId`. It may target only the
resolved effective hierarchy. Cook remaps it to dense artifact slots; staging maps
those slots to generation-checked `EntityRef`/component access after the entire
hierarchy is reserved. No partial target is observable.

`AssetReferenceV1` uses `AssetId` and expected type. It participates in the normal
dependency/cook/package closure. It cannot identify a scene entity merely because
an entity originated from that asset.

Direct serialized `SceneObjectId`, cross-document object ID, `EntityRef`, component
address, ECS index/generation, native pointer or name/path query is rejected in a
prefab source, variant, nested-placement override and `CookedPrefab`.

### 2. Binding slots are stable typed interface declarations

The enabled V1 binding-slot tier supports external scene object and component
relationships:

```cpp
enum class PrefabBindingTargetKind : std::uint8_t {
    SceneObject,
    Component
};

enum class PrefabBindingRequirement : std::uint8_t {
    Required,
    Optional
};

struct PrefabBindingSlotDeclarationV1 {
    PrefabBindingSlotId id;
    PrefabBindingTargetKind targetKind;
    std::optional<ComponentTypeId> componentType;
    std::optional<ComponentInstanceId> componentInstance;
    PrefabBindingRequirement requirement;
    RegisteredStringId displayName;
    RegisteredStringId purpose;
};

struct PrefabBindingSlotReferenceV1 {
    PrefabBindingSlotId slot;
};
```

`PrefabBindingSlotId` is a persisted random 128-bit ID allocated once in the
concrete prefab. It survives rename, reorder and display localization. Deleted IDs
are not reused while documents, variants, instances or migration history may refer
to them. `displayName` and `purpose` are bounded presentation; they are not lookup
keys.

A `SceneObject` slot requires only a compatible external object. A `Component` slot
requires an object containing exactly the declared registered component type; an
optional `ComponentInstanceId` constraint may be added only when the schema permits
multiple occurrences and the declaration persists that identity. V1 has no
interface inheritance, structural duck typing, tag/name queries, implicit nearest-
object search or service-locator binding.

Every slot/reference must be declared, unique and used with the same target schema.
A concrete prefab owns the declarations. Variants inherit them unchanged; changing
kind, requiredness or component schema is a versioned prefab-interface migration,
not an ADR-093 override. V1 variants cannot add an incompatible shadow declaration.

### 3. Bindings, overrides and initialization parameters are different data

An ADR-093 override selects a durable property/component address and changes an
authored value relative to an immediate source layer. A `PrefabBindingSetV1` maps a
declared slot ID to one external target for one placement/spawn. It has no expected-
value digest, default comparison, operation ordering, apply-to-prefab or revert
semantics.

A runtime initialization parameter is a bounded typed value copied into the spawn
candidate before behavior construction. It cannot contain an `EntityRef` or satisfy
a binding slot. Binding targets remain external identity relationships and are
validated against the target scene generation.

Inspector surfaces present Overrides, Initialization and External Bindings as
separate sections. Serializers and migration code do not translate between them.

### 4. Instance bindings are explicit at every boundary

Static scene placement stores `PrefabBindingSetV1` outside the portable prefab. A
binding target is a stable scene-authoring object ID and, for component slots, the
declared component type/instance identity. Scene conversion resolves it against one
immutable `SceneDocument` snapshot after prefab expansion and before
`RuntimeSceneDefinition` publication.

A nested placement inside another prefab cannot store a scene target. Its binding
map may only:

- bind an inner slot to an object/component in the containing effective prefab via
  `PrefabLocalReferenceV1`; or
- re-expose the inner slot as one declared outer `PrefabBindingSlotId` with exactly
  compatible kind, component schema and requiredness.

Re-exposure chains are bounded by ADR-094 nesting depth and are flattened with
provenance during resolution. Serialized order never chooses a target.

Dynamic spawn extends the owned request with a bounded binding set:

```cpp
struct PrefabRuntimeBindingV1 {
    PrefabBindingSlotId slot;
    EntityRef target;
    std::optional<ComponentInstanceId> componentInstance;
};

struct PrefabSpawnRequest {
    SceneRuntimeId targetScene;
    Assets::AssetId prefabAssetId;
    Transform spawnTransform;
    std::optional<EntityRef> parentEntity;
    BoundedVector<PrefabRuntimeBindingV1> externalBindings;
    PrefabInitializationValuesV1 initialization;
};
```

Admission copies the binding values and pins the target scene. Owner-thread staging
revalidates scene ID, entity generation/membership, component schema/occurrence,
slot uniqueness and required coverage immediately before structural commit. A
cross-scene or stale target is never rebound by index or name.

### 5. Required and optional semantics are deterministic

A required slot must have exactly one compatible binding at the applicable instance
boundary. Missing, duplicate, stale or incompatible required binding rejects scene
conversion, scene cook or spawn before publication. Standalone `CookedPrefab` cook
validates the declaration/use graph but intentionally retains required slot
requirements for the future spawn caller; it does not invent an external value.

An optional slot may be omitted. Omission resolves to the typed `Unbound` state and
is encoded identically on every host. If supplied, it must pass the same validation
as a required slot; invalid optional input is an error, not equivalent to omission.
There is no fallback search, first-match behavior or automatic nulling after a bad
binding.

Binding validation proves availability at commit, not ownership. It does not keep
the target entity alive. Runtime storage holds a scene-qualified generation-checked
reference. Later target destruction makes access return typed `BindingUnavailable`
and emits a bounded diagnostic once per policy window; it never retargets a reused
slot. Gameplay code must treat external lifetime loss as a normal reference-loss
path. A required slot does not authorize cascading target ownership or destruction.

### 6. Create-from-selection is a boundary audit transaction

Create-from-selection scans every known typed reference field in the complete
selected candidate before writing a prefab or replacing scene objects. For each
reference it records owning object/component/property identity, target identity and
classification:

- target inside the selection: rewrite to `PrefabLocalReferenceV1` using newly
  allocated stable local IDs;
- registered asset: preserve as `AssetReferenceV1` and dependency;
- target outside the selection: report a boundary crossing;
- unknown/opaque field whose reference semantics cannot be proven: report blocked
  validation without interpreting bytes.

Every boundary crossing requires one explicit user command choice: include the
target in the selection when ownership permits, expose a typed required/optional
binding slot, or cancel. Optional is never chosen merely because the old reference
was null or unresolved. The workflow never silently nulls, copies, reparents,
captures or converts an external scene object to a path/name query.

The Editor builds source, replacement instance, local-ID remap, binding declarations,
instance binding set, history and selection reconciliation as one transaction. Any
unresolved crossing, opaque uncertainty, validation failure, cancellation, source-
control/publication failure or stale document revision leaves the source scene,
prefab file, history, dirty state and selection unchanged.

### 7. Cook and runtime preserve one validation authority

ADR-094 resolution validates local targets and nested re-exposure chains. ADR-095
Scene Cook resolves static bindings into the expanded scene candidate; Prefab Cook
encodes slot declarations, internal consumer sites and compatibility schema in
`CookedPrefab`, never external scene targets. Binding declarations/usages,
component schemas, instance mappings and their revisions/digests participate in the
complete dependency-aware cache key.

Runtime staging creates a private binding table only after all internal entities and
components are reserved. Behavior instances receive a read-only typed binding view
through `BehaviorContext` after structural commit. Constructors, `OnCreate` and
`OnStart` cannot discover targets by name/path or bypass declared component access.
No behavior hook runs when required binding validation fails.

Unknown slot schema/version, undeclared use, duplicate map entry, target type
mismatch, missing required binding, invalid optional binding, cross-scene target,
stale generation or bounds failure is typed and transactional. Diagnostics include
prefab/source revision, placement scope, slot ID, expected schema and failure class;
localized labels remain presentation only.

### 8. Bounds and qualification are explicit

V1 permits at most 64 slot declarations per concrete prefab, 256 slot-reference
uses in one effective candidate and 64 external binding entries per placement or
spawn request. Counts include inherited/nested re-exposure after flattening and are
checked before unbounded allocation.

| Category | Scenario | Expected outcome |
|---|---|---|
| Valid | Internal object/component reference survives reorder | Stable local/component IDs resolve the same target |
| Valid | Asset reference moves on disk | `AssetId` and expected type remain authoritative |
| Valid | Required scene slot is explicitly bound | Conversion/cook resolves it into the complete scene candidate |
| Valid | Dynamic spawn supplies required component slot | Owner-thread validation succeeds; behavior sees typed binding after commit |
| Valid | Nested prefab binds inner slot locally or re-exposes it | Compatible map flattens with stable provenance |
| Boundary | Exactly 64 declarations/bindings and 256 uses | Accepted without unbounded allocation |
| Boundary | Next declaration/use/binding exceeds limit | Typed limit failure; no document/artifact/entities publish |
| Malformed | Prefab source contains direct scene ID, `EntityRef`, path or pointer | Authoring/cook rejection; value is not rewritten |
| Malformed | Duplicate slot ID, undeclared use or incompatible re-exposure | Candidate rejected with slot/schema diagnostic |
| Malformed | Missing required or invalid supplied optional binding | Conversion/spawn fails; optional invalid is not treated as absent |
| Lifecycle | Create-from-selection finds external target | Include/expose/cancel report; no silent null/copy/capture |
| Lifecycle | Target is destroyed after successful spawn | Access becomes `BindingUnavailable`; no retarget or ownership cascade |
| Lifecycle | Target scene reloads or entity slot is reused before commit | Generation check fails; staged hierarchy and hooks remain unpublished |
| Lifecycle | Transaction/cook/spawn is cancelled after partial preparation | Prior document/artifact/scene remains unchanged |
| Separation | Override or initialization value uses a slot-like key | Rejected by its own schema; no cross-model coercion |

## Consequences

- Prefab sources stay portable and scene-independent while supporting explicit
  external relationships where required.
- Create-from-selection becomes lossless and reviewable instead of guessing how to
  repair boundary crossings.
- Stable typed slots add interface/versioning work, but avoid stringly service
  discovery and a second override language.
- Required bindings protect activation invariants; optional absence is deterministic
  and invalid optional input still fails.
- External target lifetime remains with Scene Runtime, so gameplay must handle
  post-commit reference loss explicitly.

## Alternatives Considered

### Serialize scene object IDs or paths in prefab source

Rejected because the identity has no portable meaning across scenes, duplication,
spawn or runtime generations.

### Silently null external references during create-from-selection

Rejected because it loses authored intent and produces a prefab that appears valid
until behavior fails later.

### Copy every referenced external object into the prefab

Rejected because reference does not imply ownership and copying may duplicate large
graphs, cycles or scene-authoritative objects.

### Encode bindings as ADR-093 overrides

Rejected because binding has interface coverage, target compatibility and lifetime
semantics rather than inherited value/default/revert semantics.

### Resolve bindings by tag, name or nearest component

Rejected because results depend on mutable scene order/content and cannot be
validated or reproduced deterministically.
