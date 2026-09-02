# ADR-093: Prefab Override Property Identity and Delta Operations

- **Status**: Proposed
- **Date**: 2026-09-02
- **Supersedes**: None
- **Scope**: Stable prefab object/component/property/collection identity, typed override operation algebra, canonical ordering/equality/default comparison, source-revision rebase, conflicts/orphans, document/application transactions, opaque preservation, migration, limits and qualification
- **Issue**: [PFB-003.1](https://github.com/abdullahbodur/horo-engine/issues/1027)
- **Jira**: [HORO-1027](https://horo-engine.atlassian.net/browse/HORO-1027)
- **Parent**: [PFB-003](https://github.com/abdullahbodur/horo-engine/issues/1002)
- **Related**: [ADR-008](008-error-model-exception-boundary-and-registry.md), [ADR-017](017-prefab-role-ownership-and-capability-tiers.md), [ADR-054](054-extension-and-package-authority-boundary.md), [ADR-057](057-package-manifest-v1-typed-model.md), [ADR-094](094-prefab-nested-composition-and-variant-inheritance.md)
- **Normative documents**: [Prefab Architecture](../architecture/runtime/prefab-architecture.md), [Editor Document Model](../architecture/editor/editor-document-model.md), [Project Versioning and Migration](../architecture/foundation/project-versioning-and-migration.md), [Scene Runtime](../architecture/runtime/scene-runtime.md)

## Context

Prefab Architecture promises placed-instance overrides, later granular property
tracking, variants, revert-to-prefab and apply-to-prefab. It has stable
`LocalObjectId` hierarchy slots but no durable component-instance or property
identity. A JSON key, localized Inspector label, C++ member offset, reflection
ordinal or vector index would change after rename, localization, compiler/layout,
schema reorder or collection insertion.

Built-in components and project/package-owned components need the same semantics.
Unknown project components must round-trip opaquely, while known schemas must type-
check values and migrate them. Multiple components of one type and nested prefab
instances also mean component type alone cannot identify a target.

Source synchronization adds a three-way problem. An instance override was authored
against source revision A, then source revision B may remove/rename/retype the
target or independently change its value. Silently dropping the record loses user
work; applying it by a guessed path can corrupt another property. Last-write-wins
ordering hides duplicate records and makes serialization order semantic.

Finally, override/revert/apply-to-source are authoring mutations. Inspector widgets,
file watchers and serializer helpers cannot edit live document or prefab files
directly. They must use the same transactional command/history and atomic-save
boundaries as other Editor changes.

This ADR chooses the v1 property/component identity model and delta algebra. It
does not define the complete hierarchy-object override algebra, variant DAG user
experience or collaborative source-control protocol; those must build on these
identities and transaction rules.

## Decision

### 1. Overrides are typed deltas over an exact source revision

A placed instance or variant stores one `PrefabOverrideSetV1`:

```cpp
struct PrefabOverrideSetV1 {
    PrefabOverrideSchemaVersion schema;
    Assets::AssetId sourcePrefab;
    PrefabSourceRevision authoredAgainst;
    ComponentSchemaRegistryRevision registryRevision;
    BoundedVector<PrefabOverrideRecordV1> records;
    BoundedVector<PrefabOverrideConflictV1> conflicts;
    BoundedVector<OpaquePrefabOverrideV1> orphans;
};
```

`PrefabSourceRevision` is the canonical source-document content digest plus
`ProjectVersion`, not path, file timestamp or editor revision counter. Records are
semantic intent relative to that exact source. Expansion resolves a complete
candidate against a pinned source/registry snapshot; it never mutates the source or
override set while reading it.

Conflict and orphan collections are authoritative preserved authoring data, not
temporary UI warnings. A set with unresolved required records may remain editable
and saveable but cannot cook/expand into a runtime definition unless the owning
policy explicitly excludes that instance from runtime output.

### 2. Every component schema owns stable registered identities

The component schema registry defines:

```cpp
struct ComponentTypeId { StableId128 value; };
struct ComponentInstanceId { StableId64 value; };
struct PropertyId { StableId64 value; };
struct CollectionElementId { StableId64 value; };
```

Built-in Horo components receive fixed checked-in `ComponentTypeId` and `PropertyId`
values. Project/package components declare their IDs in versioned component schema
metadata owned by their stable package/project namespace. Tooling may generate a
cryptographically random nonzero ID once, but the persisted value is authority; it
is never recomputed from a display/C++ type name.

Renaming a C++ type/member, namespace, source file, serialized key, Inspector label
or localization key preserves the ID. Removed IDs are tombstoned and cannot be
reused for another semantic field. A compatible rename migration maps old ID to the
same retained ID or a one-way declared replacement; runtime/editor code never
guesses by similar spelling.

Every component occurrence on a prefab object has a persisted nonzero
`ComponentInstanceId`, including singleton built-ins. `ComponentTypeId` defines its
schema; `ComponentInstanceId` distinguishes multiple occurrences and survives
component display reorder. Neither ECS pool index nor serialization-array position
is identity.

### 3. The complete property address is stable and scope aware

```cpp
struct PrefabPropertyAddressV1 {
    PrefabObjectAddress object;
    ComponentTypeId componentType;
    ComponentInstanceId componentInstance;
    PropertyPathV1 property;
};

struct PrefabObjectAddress {
    BoundedVector<LocalObjectId> nestedInstanceScope;
    LocalObjectId sourceObject;
};

struct PropertyPathV1 {
    BoundedVector<PropertyPathSegmentV1> segments;
};
```

`nestedInstanceScope` is the stable sequence of owning `LocalObjectId` slots from
the outer source to the nested instance. `sourceObject` is the target's local ID in
the final scope. Asset identity comes from the override-set source and pinned nested
source manifest. Repeated nested instances therefore remain distinct without a
generated display path.

Property path segments are typed:

- `StructField(PropertyId)` selects a registered field;
- `OptionalValue(PropertyId)` selects the registered optional field value;
- `VariantAlternative(PropertyId, AlternativeId)` selects a schema-declared
  alternative;
- `KeyedElement(PropertyId, CollectionElementId)` selects a persisted element;
- `MapValue(PropertyId, CanonicalMapKey)` selects a schema-admitted bounded key.

Localized/display/JSON key strings, C++ offsets, reflection ordinals, object names,
component order and container indexes are forbidden in durable addresses.

### 4. Collection identity is explicit or the whole value is replaced

A collection schema declares exactly one override policy:

- `Atomic`: only whole-property `AssignValue` is legal;
- `StableElements`: every element carries a persisted nonzero
  `CollectionElementId`; element operations are legal;
- `CanonicalMap`: a bounded typed canonical key is identity; map operations are
  legal.

An unkeyed sequence index is never durable identity. Arrays/vectors without stable
element IDs use `Atomic`, so inserting source element zero cannot redirect an old
override from element three to another value. A schema cannot infer element identity
from value equality or a display field such as `name`.

Stable element IDs are scoped to the owning component/property and are not reused
while a source/override/conflict/orphan may reference them. Move order uses stable
anchors, never numeric indexes.

### 5. V1 has a closed typed operation algebra

`PrefabOverrideOperationV1` admits:

| Operation | Target | Meaning |
|---|---|---|
| `AssignValue` | Existing property/atomic collection | Replace the effective source value with one schema-typed canonical value |
| `InsertElement` | Stable-element collection | Insert a new stable element and complete typed value before/after a stable anchor or at start/end |
| `RemoveElement` | Existing stable/map element | Tombstone the identified source/override element |
| `MoveElement` | Existing stable element | Reorder relative to a stable anchor without changing element identity/value |
| `AssignElementValue` | Existing stable/map element | Replace the identified element's complete typed value |
| `AddComponent` | Existing prefab object | Add one new component instance with type/schema and complete canonical payload |
| `RemoveComponent` | Existing source component instance | Tombstone the complete component occurrence |

`ClearOverride`/revert is not a persisted operation. The document command removes
the selected record(s), allowing the effective source value/component to flow
through. `AddComponent` contains a complete payload so later schema-default changes
cannot reinterpret an omitted field. Properties inside an added component may be
overridden only by a later inheritance layer; the same layer edits the add payload
transactionally instead of creating self-patches.

Hierarchy operations (`AddObject`, `RemoveObject`, `ReparentObject`) require the
PFB-003 hierarchy decision. V1 rejects them as unknown operations rather than
encoding object paths or child indexes ad hoc.

### 6. Records carry explicit preconditions and provenance

Every record contains operation ID/version, property/component address, the exact
source schema version, `expectedSourceValueDigest` or
`expectedSourceComponentDigest`, new typed value/payload where applicable, authoring
source revision and bounded provenance needed for diagnostics/history.

The expected digest is over the schema codec's canonical effective source bytes at
the layer immediately below this override. It is not a file substring hash or raw
JSON spelling. Add operations carry an expected-absence marker; remove operations
carry the expected complete target digest.

These preconditions support validation and three-way rebase. They do not implement
optimistic locking against a live document: document revision/state preconditions
remain owned by the Editor transaction.

### 7. Canonical equality and default comparison are schema owned

Each registered property/component schema supplies a bounded canonical codec,
validator and equality operation. Equality is equality of valid canonical semantic
bytes after declared normalization:

- integer, enum, stable ID, string and presence values compare exactly;
- strings compare exact Unicode scalar/normalization policy declared by the schema,
  never localized/case-folded display text;
- finite floating values normalize signed zero and any schema-declared quaternion/
  unit-vector representation, then compare exact canonical bits;
- NaN/non-finite values are rejected unless a specific schema defines a closed
  canonical representation;
- maps/sets use canonical key order; stable-element sequence order remains semantic.

There is no broad epsilon/default comparison. Approximate Inspector display values
cannot decide whether an override exists.

For a source component, the "default" is its effective value in the exact immediate
source layer, after base/variant deltas—not the current component registry default.
An `AssignValue` equal to that value is redundant and is removed by canonicalization.
If a source update later becomes exactly equal to the override, rebase also removes
the redundant record after preserving history in the transaction delta.

Registry defaults are used only when an authoring command creates a new component.
`AddComponent` then persists the complete validated payload, so changing a future
registry default does not mutate existing instance intent.

### 8. Canonical ordering is independent of authoring/UI order

Within one override set records sort lexicographically by:

1. nested instance scope length and each `LocalObjectId` value;
2. target `LocalObjectId`;
3. `ComponentTypeId`, then `ComponentInstanceId`;
4. operation target class (`Component`, `Property`, `CollectionElement`);
5. property path segments by kind, `PropertyId`, alternative/element/map-key bytes;
6. closed operation rank;
7. new element ID for inserts.

Canonical encoding writes this order with explicit lengths and versions. Source
document order, Inspector order, hash-map iteration and transaction arrival order do
not affect bytes/digest.

Two operations in one layer that claim the same semantic target are not resolved by
last-write-wins. Duplicate `AssignValue`, add/remove of the same component, remove
plus property patch, duplicate element insert or mutually incompatible move/remove
is `DuplicateOrConflictingOverride`. The document command must coalesce/replace the
record deliberately before commit.

Inheritance layers apply base to leaf: base prefab, each variant in DAG order, then
placed-instance set. Each layer is canonical internally. A higher layer may override
an existing lower-layer property or remove a lower-layer component, but validation
must preserve provenance and reject a patch into an already removed target.

### 9. Resolution produces a closed candidate without source mutation

Resolution pins the complete source/nested dependency graph, component schema
registry and override-set revisions. It then:

1. validates bounds, canonical encoding/order and duplicate identities;
2. builds stable object/component/property lookup tables;
3. validates every typed record and expected-source precondition;
4. applies layers into detached typed component/object payloads;
5. collects conflicts/orphans without deleting their original canonical bytes;
6. validates the fully effective prefab hierarchy/components/capabilities;
7. publishes one immutable resolved candidate/digest only when policy permits.

Collectors, serializers, Inspector widgets and file watchers never mutate source or
active document storage. Unknown but well-formed opaque records remain byte-for-byte
preserved and are not applied until their owning schema is available and validated.

### 10. Source revision changes use deterministic three-way rebase

Rebase compares the record's expected lower-layer digest, the new effective source
value/component and the override value/operation:

| Old source vs new source | Override vs new source | Result |
|---|---|---|
| Equal | Different | Apply record unchanged; update authored-against/precondition |
| Different | Equal | Drop redundant record transactionally; inherit new source |
| Different | Different, compatible and schema merge rule proves disjoint | Apply deterministic schema merge and record provenance |
| Different | Different with no admitted merge | Preserve as explicit conflict; do not choose source or override |
| Target missing/unresolvable | N/A | Preserve as orphan with exact reason and bytes |

V1 defines no generic field-wise merge inside an `AssignValue`. A schema may expose
an exact versioned merge only when it can prove disjoint stable subproperties or
collection elements. Otherwise simultaneous change conflicts.

Renamed/moved identity resolves only through registered migration mappings. Removed
property/component, missing nested scope, incompatible type/cardinality, deleted
collection element/anchor or unavailable schema creates a typed orphan/conflict. It
is never retargeted by nearest name, offset, type-only match or collection position.

Rebase builds a new override-set candidate. Failure/cancellation leaves source,
instance, conflicts, history and saved files unchanged.

### 11. Conflicts and orphans are lossless authoring states

`PrefabOverrideConflictV1` stores stable target, operation/provenance, expected old
digest, new-source digest/value reference, override bytes, conflict kind and source/
schema revisions. `OpaquePrefabOverrideV1` stores the original bounded canonical
record bytes plus known envelope identity and orphan reason.

Resolution choices are explicit document commands:

- `KeepSource` removes/archive-resolves the override;
- `KeepOverride` revalidates/rebases it against the new source;
- `EditMergedValue` writes one new typed operation;
- `RetargetThroughDeclaredMigration` applies a registered identity/value migration;
- `DeleteOrphan` intentionally discards preserved data.

Opening/saving an authoring document does not auto-select a choice. Opaque unknown
records round-trip exactly. Cook/runtime expansion fails while required conflicts or
orphans remain because shipping silently different content is not acceptable.

### 12. All mutations use document/application transactions

Inspector actions submit typed intents to the Editor command executor. Commands
validate document session/revision, pinned prefab/source/schema revisions and the
complete delta before staging. One committed transaction atomically updates the
override set, conflict/orphan state, document revision/dirty state, selection
reconciliation and one history entry.

Revert removes records through the same command path. Undo restores the exact prior
override/conflict/orphan set; redo reapplies the validated semantic transaction.
Preview gestures may use a reversible overlay but do not persist override records or
dirty state until commit.

Apply-to-prefab is a multi-document application use case. It prepares source-prefab
edits, affected instance rebase candidates, source-control/write permissions and
atomic saves before committing document states. It never edits a `.prefab` file from
an Inspector callback or removes instance overrides before source publication is
known successful. Partial external publication reports an explicit outcome and
reconciliation record; it cannot claim atomic rollback of an already published file.

File watcher/external reload creates a new source revision and queues an explicit
rebase/conflict workflow. It is not inserted as a hidden undo command and does not
overwrite dirty instance intent.

### 13. Migration is ID- and schema-driven

ProjectVersion migration reads the old override envelope under its recorded
component/property schema versions. Registered migration stages may:

- retain an ID across a representation rename;
- map one tombstoned ID to one declared replacement ID;
- split/combine values only with an explicit total typed conversion;
- transform canonical value bytes between supported schema versions;
- convert old atomic collections to stable elements only with a deterministic
  persisted ID assignment recorded by the migration.

Ambiguous one-to-many mappings, failed value conversion, missing package/schema or
unkeyed index-based legacy paths become preserved conflicts/orphans. Migration never
uses localized label/C++ offset/current array position to guess. Successful migration
emits a new canonical set/source precondition and records the stage chain; source
files are replaced only through the project/document migration transaction.

Cooked prefab artifacts contain only fully resolved effective components plus
provenance/digests required by the cook contract. Runtime never migrates or applies
authoring override records; source tools migrate and recook.

### 14. Limits and lifecycle are explicit

Profiles bound override records, conflicts, orphans, nested-scope/path depth,
canonical value/opaque bytes, component additions, collection operations and total
resolved candidate bytes. Counts and checked lengths are validated before reserve,
decode or recursion. CanonicalV1 starts with:

| Item | Maximum |
|---|---:|
| override records per instance/variant layer | `16,384` |
| conflict plus orphan records per set | `16,384` |
| nested instance scope depth | `16` |
| property path segments | `32` |
| canonical value or opaque record bytes | `1 MiB` |
| aggregate override-set bytes | `16 MiB` |

Projects may lower these. Raising them requires format/tool/editor/cook performance
evidence and checked arithmetic.

Document close/project switch closes new commands/rebases, cancels and joins
preparation, drains immutable snapshots and then releases schema/source leases. Late
worker results carrying old document/source/registry revisions are rejected. Package
unload may make records opaque/orphaned but cannot delete them. Reinstall/reload
re-resolves through a new explicit candidate; it never mutates behind history.

### 15. Errors and observability preserve provenance

ADR-008 results distinguish invalid/duplicate component/property/element identity,
unknown schema/version/operation, type/range/value error, illegal collection policy,
missing target/scope/anchor, precondition/source/registry conflict, orphan/conflict,
noncanonical order/encoding, cycle/depth/count/byte budget, stale document/revision,
transaction conflict, cancellation, operation in progress, unload and shutdown.

Diagnostics carry bounded asset/source/document revisions, nested/local/component/
property/element IDs, operation kind, layer, expected/actual digests, migration stage
and source location. Localized labels may be presentation only. Logs never dump
opaque/value bytes or unbounded component payloads.

Metrics include record/operation/conflict/orphan counts, resolution/rebase/migration
result and duration, candidate/opaque bytes, canonicalization removals, limit high
water and stale completion rejection. Stable result/operation/schema enums may be
dimensions; asset/object/property IDs may not.

### 16. Qualification covers valid, boundary, malformed and lifecycle cases

Required regression evidence includes:

- built-in and project/package component/property IDs survive type/member/JSON key/
  label/localization reorder/rename without retargeting;
- multiple same-type component instances and repeated nested prefab scopes resolve
  independently;
- struct/optional/variant, atomic sequence, stable-element and canonical-map paths;
- insert/remove/move/assign element using stable IDs/anchors under source reorder;
- unkeyed collection supports whole-value assignment only and rejects numeric path;
- every operation, valid layer composition, duplicate/conflicting operations and
  patch-after-remove failure;
- canonical order/bytes/digest independent of UI/hash-map/document order;
- semantic equality, signed zero/quaternion normalization, non-finite rejection,
  effective-source default comparison and redundant-record removal;
- three-way source unchanged/override equal/disjoint schema merge/conflict/orphan
  truth table, including deleted/retyped target and missing anchor/schema;
- unknown component/operation opaque round trip and cook failure until resolved;
- every declared ProjectVersion/ID/value migration plus ambiguous/failed preservation;
- apply/revert/undo/redo symmetry, cancelled/failed rebase, external reload and
  apply-to-source partial-publication reconciliation;
- malformed/truncated/duplicate/noncanonical/oversized encodings and fuzzed checked
  lengths/depth/counts;
- maximum records/path/scope/value/aggregate bytes, no partial candidate publication,
  package unload/reload, document close/project switch and repeated shutdown.

## Consequences

Prefab customization gains stable typed identity across code, label, schema order,
hierarchy reorder and keyed collection edits. Override bytes/order/equality and
default comparison are deterministic. Source changes preserve conflicting or
orphaned intent instead of guessing or dropping it, and all authoring mutation stays
inside reviewable undoable transactions.

The cost is persistent component/property/element IDs, schema codecs/migrations,
complete preconditions/provenance, explicit conflict UI and stricter rejection of
unkeyed granular edits. Tier-2 hierarchy operations still require a follow-up
decision rather than being smuggled into property paths.

## Rejected Alternatives

### Use serialized property names or Inspector labels

Rejected because rename/localization changes identity and project components may
reuse labels. Strings may be presentation aliases, never durable addresses.

### Use C++ offsets, reflection ordinals or component/container indexes

Rejected because compiler/layout/schema reorder and insertion retarget existing
overrides. Persistent component/element IDs and property IDs are required.

### Match collection elements by value or display name

Rejected because duplicates and edits make the match ambiguous. Unkeyed collections
are atomic; granular collections declare stable element identity.

### Apply records in serialized order with last-write-wins

Rejected because file/UI/hash-map order would become behavior and duplicate intent
would be hidden. Canonical order and conflict rejection are explicit.

### Compare overrides to the current registry default

Rejected because the effective immediate source layer—not today's component default—
is what the instance inherits. Added components persist complete payloads.

### Drop unknown, missing or conflicting records on source update

Rejected because that silently loses project-owned authoring data. Records remain
opaque/conflicted/orphaned until an explicit transaction resolves or deletes them.

### Let Inspector widgets edit prefab files and instance maps directly

Rejected because failure, undo/redo, dirty state, external conflict and multi-
document apply-to-source would become partial and unreviewable.

### Add hierarchy operations to property V1 opportunistically

Rejected because object ownership, nesting, parent/transform preservation and source
propagation need their own closed algebra. Unknown hierarchy ops fail until accepted.
