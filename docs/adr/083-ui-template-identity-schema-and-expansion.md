# ADR-083: UI Template Identity, Schema and Expansion

- **Status**: Proposed
- **Date**: 2026-09-02
- **Supersedes**: None
- **Scope**: Runtime UI template assets, stable/local/instance identity, typed parameters and slots, nested references, insertion and linked instancing, deterministic expansion, update/rebase, detachment, cook/runtime projection, package ownership, failures, compatibility, limits, unload, and shutdown
- **Issue**: [RUI-012.1](https://github.com/abdullahbodur/horo-engine/issues/810)
- **Jira**: [HORO-810](https://horo-engine.atlassian.net/browse/HORO-810)
- **Parent**: [RUI-012](https://github.com/abdullahbodur/horo-engine/issues/781)
- **Related**: [ADR-054](054-extension-and-package-authority-boundary.md), [ADR-073](073-runtime-ui-ownership-scope-and-update-order.md), [ADR-074](074-runtime-ui-layout-units-and-measure-arrange.md), [ADR-076](076-runtime-ui-style-asset-token-and-inheritance.md), [ADR-079](079-runtime-ui-binding-provider-schema-identity-and-lifetime.md), [ADR-081](081-runtime-ui-and-localization-ownership-boundary.md)
- **Normative documents**: [Game UI and HUD](../architecture/runtime/game-ui-and-hud.md), [Asset Pipeline](../architecture/runtime/asset-pipeline.md), [Prefab Architecture](../architecture/runtime/prefab-architecture.md), [Horo Package System](../architecture/packages/package-system.md)

## Context

Runtime UI templates need to reuse a typed element hierarchy without becoming new
runtime widget kinds. A template can be copied once into a document or retained as
a linked instance whose declared parameters vary per use. Those workflows require
different identity and update semantics.

Blind deep overrides make base updates ambiguous. Automatic live propagation can
silently change open documents, packaged behavior, focus/accessibility or bindings.
Conversely, expanding every use into unrelated copies loses provenance and makes
safe reusable HUD/menu composition impossible. Paths, vector indexes and display
names are not stable enough to reconcile changes.

Scene prefabs solve a related but separate ECS/entity problem. UI templates use UI
element/property/style/localization/binding schemas and expand into Runtime UI core
primitives. They must not reuse `EntityId`, prefab component payloads or gameplay
lifecycle callbacks.

This decision establishes a deliberately narrow baseline: typed parameters and
named content slots are the only linked customizations; base updates are explicit
reviewed rebase transactions; detach materializes an independent document subtree.

## Decision

### 1. Templates are first-class Runtime UI assets, not runtime classes

Assets owns stable `AssetId`, source/sidecar identity, generic import/cook/cache/
package scheduling, dependency publication and immutable runtime bytes. The Runtime
UI Template domain owns template schema, element/property validation, parameters,
slots, expansion, semantic fingerprints and compatibility.

A `UiTemplateAsset` contains a finite rooted fragment of core or registered Runtime
UI element descriptors plus its public parameter/slot interface. Expansion produces
ordinary UI elements. It cannot define a C++ class, virtual widget, native view,
ImGui callback, renderer object, script closure or ambient service lookup.

Template authoring and insertion are available to Editor, CLI and MCP through one
application capability. HoroEditor owns preview/undo UI, not template semantics.

### 2. Asset, local, instance and expanded identities are distinct

The model has these non-interchangeable identities:

- `UiTemplateAssetId`: the underlying stable Assets identity;
- `UiTemplateLocalElementId`: stable authored element slot within one template;
- `UiTemplateParameterId`: stable public parameter identity;
- `UiTemplateSlotId`: stable named content-slot identity;
- `UiTemplateInstanceId`: stable instance identity within the owning UI document;
- `UiElementId`: stable element identity in an owning standalone/expanded document;
- `UiTemplateRevision`: schema version plus canonical semantic content digest.

Local element, parameter and slot IDs are never vector indexes, display names,
paths or hashes of mutable labels. Surviving IDs do not change when declarations
reorder; deleted IDs are not reused within the compatibility window. Renaming
display labels leaves identity unchanged.

Each linked instance stores its template asset ID, accepted semantic revision,
instance ID, typed argument values, slot fragments and required compatibility
range. Nested instances extend an immutable lineage of stable
`UiTemplateInstanceId` values. Expansion derives
candidate `UiElementId` values with a fixed versioned hash over owning document,
instance-ID lineage and local element ID, then collision-checks the complete target
document. A collision rejects publication; hashing never authorizes overwrite.
Moving or renaming an instance without changing its stable ID leaves expanded IDs
unchanged; reparenting changes only lineage membership when it changes the semantic
instance owner.

### 3. The template document schema is finite and self-describing

A template document declares:

```cpp
struct UiTemplateDocument {
    UiTemplateSchemaVersion schema;
    UiTemplateInterfaceVersion interfaceVersion;
    UiTemplateLocalElementId root;
    std::span<const UiTemplateElement> elements;
    std::span<const UiTemplateParameter> parameters;
    std::span<const UiTemplateSlot> slots;
    std::span<const UiTemplateNestedInstance> nestedInstances;
};
```

Elements use the same versioned typed descriptors as ordinary `UiDocument` data.
Exactly one root, unique local IDs, valid parents, connected acyclic hierarchy,
known element/property schemas and valid style/font/localization/binding/resource
references are required. Unknown required semantics reject; approved opaque
authoring preservation does not make them runtime-expandable.

Source documents are UTF-8, deterministically encoded and project-version migrated
before validation. Editor selection, tree expansion, preview resolution, undo IDs,
ImGui state, absolute paths and runtime handles are never semantic template data.

### 4. Parameters expose exact typed targets

Each public parameter declares stable ID, display/localization metadata, closed
value type, required/default policy, constraints and one or more exact targets:

```cpp
struct UiTemplateElementPropertyTarget {
    std::span<const UiTemplateInstanceId> instanceLineage;
    UiTemplateLocalElementId element;
    UiStylePropertyId property;
    UiTemplateParameterTransform transform;
};

struct UiTemplateNestedParameterTarget {
    std::span<const UiTemplateInstanceId> instanceLineage;
    UiTemplateParameterId parameter;
    UiTemplateParameterTransform transform;
};

using UiTemplateParameterTarget =
    std::variant<UiTemplateElementPropertyTarget, UiTemplateNestedParameterTarget>;
```

The value set includes bounded scalar/integer/boolean/enum, text content or
`LocalizedMessageRef`, typed color/style/font/asset references, layout values,
declared binding descriptors and other registered Horo value schemas. Arbitrary
string property paths, variant maps, C++ offsets, pointers and callbacks are
forbidden.

Target property type must exactly match the parameter or an explicitly registered
deterministic transform. Parameters cannot target identity, parentage, element kind,
owner scope, package trust, native/renderer handles or hidden executable state.
Duplicate/conflicting target writes, invalid defaults and cyclic parameter-derived
expressions reject the template.

Nested-parameter targets forward a parent argument through an exact stable instance
lineage before the child expands. The child parameter must exist and accept the
source type or a registered deterministic transform. Forwarding cannot target a
descendant outside that lineage, write identity, or form a parameter dependency
cycle; the complete forwarding graph is validated before any expansion output.

Parameter removal/type change is breaking. Adding an optional parameter with a
default is compatible. Constraints may tighten only under a new incompatible
interface version unless every previously admitted value remains valid.

### 5. Slots admit bounded typed child fragments

A slot declares stable ID, owning local element, minimum/maximum child count,
allowed root element kinds/interfaces, ordering policy and required/default
fragment. Instance slot content is ordinary authored UI data owned by the host
document, not mutable storage borrowed by the template.

Slot fragments receive stable document-owned IDs and may contain nested template
instances within global depth/count/cycle limits. They cannot escape their slot,
replace template-owned ancestors, override undeclared properties or retain pointers
to the template source. Removing or narrowing a populated slot is a breaking
interface change.

Accessibility/focus/layout validation runs after slot composition so the expanded
tree, not each fragment in isolation, is authoritative.

### 6. Insert and linked instance are explicit different operations

`InsertTemplate` resolves one exact template revision, arguments and slots, expands
the fragment, assigns fresh persisted document-owned `UiElementId` values and
commits it as ordinary UI content. The result stores optional diagnostic provenance
only. It has no live template relationship, update/revert/apply-to-base behavior or
runtime dependency on the source template.

`CreateTemplateInstance` persists the linked instance record. Its generated subtree
is a derived authoring/preview/cook projection, not a second serialized source of
truth. Only declared parameter values and slot content may differ from the accepted
template revision. Arbitrary deep/property overrides are outside the baseline.

Both operations are revision-checked document transactions with previewable
diagnostics and complete undo. Failure publishes nothing and preserves the prior
document.

### 7. Expansion is deterministic, bounded and side-effect free

Expansion resolves the complete nested template DAG, validates exact compatible
asset/interface revisions, substitutes typed parameters, composes slots, assigns
scoped candidate IDs and emits one ordinary immutable UI fragment. Stable traversal
is parent-before-child and local-ID ordered; declaration/vector/file order cannot
change output.

Expansion performs no asset discovery by path, network/source I/O, service lookup,
module activation, widget construction, binding-provider call, gameplay command,
renderer allocation or lifecycle callback. All dependencies are resolved and
pinned before expansion.

Cycles are detected by `AssetId` alone across the active lineage; selecting another
revision of an already active asset does not make recursion valid. Limits apply to
source and fully expanded depth, elements, nested instances,
parameters, slots, argument bytes, payload bytes and dependency count. Incremental
checks stop before exceeding budgets; truncation is never valid output.

### 8. Linked updates require explicit atomic rebase

A linked instance pins an accepted `UiTemplateRevision`. Publishing a newer source
marks the instance `UpdateAvailable`; it does not mutate open documents, preview,
saved content or running UI. Runtime/package cook uses the accepted dependency and
fails with `TemplateRevisionUnavailable` if exact reproducible input cannot be
resolved.

`RebaseTemplateInstance` explicitly selects a target revision and prepares a full
candidate. Reconciliation matches stable local/parameter/slot IDs, applies schema
migrations, retains valid argument/slot values, reports removed/type-changed/
constraint-invalid declarations and compares the complete expanded semantic diff.

Because baseline linked customization is limited to public parameters/slots, there
is no hidden deep-override merge. Any unresolved required parameter, populated
removed slot, identity collision, incompatible nested dependency, binding/style/
accessibility/focus/layout failure or limit violation blocks commit. The editor may
offer explicit user choices, but cannot guess or drop data.

Commit updates accepted revision and complete instance state atomically under the
owning document revision with undo. Active preview/runtime replacement follows
ADR-073 prepare/activate/last-good retirement; source save never patches a running
tree in place.

### 9. Detach materializes one independent subtree

`DetachTemplateInstance` expands the exact accepted revision, allocates and persists
fresh document-owned `UiElementId` values for all template-owned elements, preserves
slot-owned content and resolved values, removes the linked instance record and
commits one ordinary subtree transaction.

After commit there is no update/revert/apply-to-template relationship. Optional
provenance is diagnostic metadata only and cannot reactivate linking. Nested linked
instances are detached recursively by default; tooling may expose an explicit mode
to preserve valid nested links, with the choice recorded in the command/result.

Failure leaves the original linked instance intact. Detach never edits the template
asset, package or other instances and never uses runtime/transient IDs as persisted
document identity.

### 10. Cook expands linked instances; runtime has no template authority

UI document cook resolves accepted template revisions from the locked asset/package
graph and emits a flattened `CookedUiDocument` of ordinary elements plus bounded
diagnostic provenance. Cook dependencies include every nested template, style,
font, localized asset/message catalog, binding schema and slot resource revision.

Cook cache identity includes the host document, template semantic/interface
revisions, canonical arguments/slots, expansion algorithm version, property/element
schemas and dependency digests. Output is byte-deterministic for identical inputs.

Packaged Runtime UI loads and instantiates the cooked ordinary tree. It does not
resolve source templates, propagate updates, detach instances or expose template
authoring handles. A future explicitly dynamic runtime-template capability would
require a separate decision and cannot silently reuse this authoring contract.

### 11. Packages contribute assets without becoming template authority

Template packages use ADR-054 package identity, version, manifest, lock, trust and
file verification. Package descriptors list template assets/catalog metadata as
finite inert contributions. Runtime UI validates the actual template schema; the
package manager does not interpret elements/parameters/slots or expand instances.

Serialized references use stable Asset/package identities and compatibility
ranges, never install paths, marketplace IDs or module pointers. Package update/
disable/uninstall cannot remove an accepted template revision while documents,
cooks or snapshots lease it. Deactivation closes admission and drains leases first.

Templates cannot grant code execution, binding write permission, asset access or
native capability beyond the verified package/host contract. A template using a
package element/property/provider schema records and validates that exact dependency.

### 12. Compatibility and migration are explicit

Template source schema, public interface version and semantic revision are separate.
Source schema migration preserves meaning and IDs. Public interface uses major/minor
compatibility: optional defaulted additions are minor; removal/type/requiredness/
target semantic changes are major. Semantic content revision changes for any
meaningful element/property/default/nested dependency update even when interface is
compatible.

Cooked artifacts declare schema, required feature bits, expansion algorithm,
endianness/scalar widths, limits and integrity digests. Unknown required semantics,
newer major interfaces and expired migrations request editor update/recook rather
than best-effort expansion.

Legacy copied template content imports as ordinary detached UI. A legacy linked
format is converted only when stable source/local/instance identities and all
customizations can be represented losslessly; otherwise migration reports an
explicit user-assisted detach and does not discard data.

### 13. Failures, lifecycle and limits preserve last-good state

Results follow ADR-008 with stable codes for missing/revoked asset/revision,
unsupported schema/interface/feature, malformed hierarchy, duplicate/invalid ID,
parameter/target/type/default/constraint failure, slot/cardinality/type failure,
nested cycle/depth, identity collision, dependency/package incompatibility, stale
document, expansion/cook budget, cancellation and shutdown.

Diagnostics carry bounded template/instance/local/parameter/slot/property/revision
evidence without full user text, secret binding values, native handles or raw asset
payloads. Required failure rejects insert/instance/rebase/detach/cook activation and
retains the previous document/runtime generation.

Preparation captures immutable asset/package/schema/document revisions and
cancellation. Late completion cannot publish into a newer document or reused owner
scope. Shutdown closes template commands/cook admission, cancels and joins owned
preparation, retires UI/document/asset/package leases, then releases registries and
Assets. Partial activation and repeated shutdown are idempotent.

### 14. Verification is part of the contract

Required coverage includes:

- stable asset/local/parameter/slot/instance IDs across rename/reorder/insert/delete,
  repeated/nested instances, deterministic candidate IDs and injected collisions;
- valid/invalid roots, parents, cycles, element/property schemas, unknown required
  data, source migration and canonical round-trip;
- every parameter value kind, defaults/requiredness, exact target type, transforms,
  duplicate/conflicting writes, constraint and interface compatibility;
- slot cardinality/type/order/defaults, stable host content IDs, nested instances,
  focus/accessibility/layout validation and data-preserving failure;
- Insert produces independent persisted content; linked instance stores only public
  arguments/slots and exact accepted revision;
- source update creates UpdateAvailable without document/runtime mutation; rebase
  preview/commit/undo, removed/type-changed parameters/slots and last-good runtime;
- detach identity allocation, slot preservation, recursive/preserved nested modes,
  rollback and absence of later propagation;
- nested DAG expansion order, cycles, fully expanded count/depth/byte/dependency
  limits, cancellation and no side effects or ambient discovery;
- byte-identical cook/cache keys, complete dependency closure, missing accepted
  revision, locked package version, packaged ordinary-tree runtime and unload leases;
- equivalent Editor/CLI/MCP commands, editor preview, headless validation and no
  template/native/ImGui/editor pointers in runtime/public snapshots;
- schema/interface/algorithm/version skew, malformed cooked artifacts, redacted
  diagnostics and shutdown after every partial lifecycle state.

## Consequences

Templates provide reusable typed UI without adding runtime widget classes or a
second element source of truth. Inserted content is predictably independent;
linked instances remain updateable because customization is constrained to a
declared interface. Updates are reviewable/reproducible and detach is lossless and
final.

The baseline intentionally omits arbitrary deep overrides, live propagation,
apply-instance-to-base and runtime source-template instantiation. More expressive
variant inheritance would need a separate conflict/provenance model.

## Rejected Alternatives

### Treat templates as editor-only copy/paste blobs

Rejected because identity, parameter schema, package dependencies, deterministic
CLI/MCP use and linked instances would have no authoritative contract.

### Allow arbitrary deep property overrides on linked instances

Rejected because base deletions/type changes require ambiguous merges and can
silently lose data. Declared parameters/slots form the safe baseline interface.

### Propagate source saves live to every instance

Rejected because open/saved/running documents could change without review, undo,
reproducible dependency identity or complete validation.

### Serialize the expanded linked subtree beside the instance record

Rejected because the projection and template reference would compete as sources of
truth and drift after edits or migration.

### Keep a detached subtree linked through provenance metadata

Rejected because detach would not be a reliable ownership boundary. Re-linking
requires an explicit future operation with full reconciliation, not hidden state.

### Reuse scene prefab entities and component schemas

Rejected because Runtime UI elements, properties, owner scopes, focus/layout and
render extraction are not ECS entities or gameplay component lifecycle.
