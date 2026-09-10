# ADR-176: Runtime UI Element and Control Taxonomy

- **Status**: Accepted
- **Date**: 2026-09-07
- **Supersedes**: None
- **Scope**: Runtime UI element kinds, orthogonal capabilities, control semantics, descriptor ownership, validation, compatibility, migration, and failure policy
- **Issue**: [RUI-001.5](https://github.com/HoroCore/horo-engine/issues/701)
- **Jira**: [HORO-701](https://horo-engine.atlassian.net/browse/HORO-701)
- **Parent**: [RUI-001](https://github.com/HoroCore/horo-engine/issues/700)
- **Related**: [ADR-073](073-runtime-ui-ownership-scope-and-update-order.md), [ADR-074](074-runtime-ui-layout-units-and-measure-arrange.md), [ADR-076](076-runtime-ui-style-asset-token-and-inheritance.md), [ADR-078](078-runtime-ui-input-context-and-player-routing.md), [ADR-079](079-runtime-ui-binding-provider-schema-identity-and-lifetime.md), [ADR-082](082-runtime-ui-accessibility-capability-and-ownership.md), [ADR-083](083-ui-template-identity-schema-and-expansion.md)
- **Normative documents**: [Game UI and HUD](../architecture/runtime/game-ui-and-hud.md)

## Context

Runtime UI needs a small, stable vocabulary for authored and cooked element
descriptors. Without one authority, documents, templates, editor palettes,
serialization, layout, input, accessibility, and rendering can each invent a
different widget hierarchy. A single inheritance-heavy widget class can also
make visual appearance, layout, input, value state, and application commands
implicit side effects of the element kind.

The taxonomy must remain Horo-owned and backend neutral. Native views, ImGui
widgets, renderer objects, editor selection state, and gameplay-specific widgets
are projections or compositions, not Runtime UI element kinds. The taxonomy is
metadata interpreted by the owning Runtime UI generation; it does not create
services, register callbacks, or mutate ambient state.

## Decision

### 1. One closed element-kind vocabulary owns structural identity

`UiElementKind` is a versioned, closed Horo enum. Its values are grouped by the
single structural or semantic role that determines an element's core descriptor:

- structural/container: `Canvas`, `Screen`, `Panel`, `Frame`, `LayoutGroup`;
- visual/text: `Text`, `Image`;
- action/value/text-entry: `Button`, `ProgressBar`, `Slider`, `Toggle`,
  `TextInput`;
- scrolling/collection: `ScrollView`, `CollectionView`.

These names are schema identities, not C++ subclasses, renderer draw types, or
native controls. Each persisted value has an explicit numeric identity and one
schema version. Reordering source declarations, renaming an editor label, or
changing backend placement cannot change its encoded identity.

Core does not add gameplay concepts such as inventory, quest tracker, minimap,
dialogue system, health bar, or ability bar. Those are finite compositions of
core elements through ordinary documents and ADR-083 templates. A package may
introduce a namespaced registered element descriptor only through a future
explicit extension contract; it cannot reinterpret or shadow a core kind.

### 2. Kind is separate from orthogonal capabilities

An element descriptor contains exactly one kind and independently declares only
capabilities admitted for that kind and schema version. The closed capability
dimensions include:

- child containment and collection-item projection;
- visual content and clipping;
- text content or text editing;
- focus, action, scalar value, boolean value, selection, and scrolling;
- accessibility semantics supplied under ADR-082.

Capabilities describe what typed data may be present. They do not imply that an
element is visible, enabled, focusable, writable, selected, or currently owns
capture. Those are explicit authored or runtime states. Duplicate, conflicting,
unknown required, or kind-incompatible capability records reject the candidate.

This separation prevents a `Panel` from becoming a different kind when it clips,
a `Text` element from becoming a different kind when localized, or a `Button`
from encoding its style, layout algorithm, input device, or application command
inside its kind tag.

### 3. Control semantics use typed values and commands

Interactive controls expose typed semantic state rather than callbacks:

- actions produce a stable `UiActionId` plus bounded typed command arguments;
- scalar controls use a declared numeric value type, finite range, step, and
  explicit clamp/reject policy;
- toggles use a declared boolean or closed-state value;
- text input declares bounded text, editing, validation, and submission policy;
- collections use stable item identities and an explicit selection policy;
- scrolling uses typed axis, extent, offset, and overscroll policy.

The owning Runtime UI update validates state and emits an owner-addressed command
at the ADR-073 safe point. Descriptors contain no function pointer, closure,
script object, service key, mutable ECS reference, or renderer/native handle.
Reading a binding never grants write authority; ADR-079 remains authoritative for
binding schema and lifetime.

Cancellation, owner retirement, document replacement, focus/capture loss, and
shutdown close command admission. Already prepared commands are either committed
once under their expected generation and revision or rejected as stale; an
element destructor never executes gameplay behavior.

### 4. Layout, style, input, and accessibility remain separate authorities

Element kind does not select an implicit layout algorithm or visual skin.
ADR-074 owns box constraints and measure/arrange. ADR-076 owns style tokens,
inheritance, and resolved presentation. ADR-078 owns player/viewport routing,
focus, capture, and input priority. ADR-082 owns semantic role, label, value,
state, action, traversal, and assistive projection.

The taxonomy may constrain which records are meaningful, but it cannot duplicate
those schemas. For example, a focus capability admits participation in ADR-078;
it does not store a native focus pointer. Accessibility role is explicit and
validated against the kind/capabilities; it is not inferred from renderer output.
Rendering consumes immutable projected primitives and cannot author, upgrade, or
replace an element kind.

### 5. Ownership and lifecycle follow the Runtime UI generation

Authored and cooked descriptors are immutable values owned by their document or
package generation. Mutable control state belongs to exactly one ADR-073 runtime
instance generation. Layout, interaction, accessibility, and render data are
immutable projections with their own revisions and leases; none owns the source
descriptor or stores a mutable pointer back to it.

Preparation validates the complete tree and all capability records privately.
Activation publishes one coherent generation. A failed descriptor, migration,
resource resolution, or projection preserves the prior last-good generation.
Retirement closes input and command admission before projections and resource
leases drain. Teardown is idempotent and cannot extend an element beyond its
game, player, scene, or viewport owner.

The contract is synchronization-neutral. It defines immutable values and
owner-thread publication but does not claim that descriptor mutation or runtime
control state is concurrently writable.

### 6. Validation is finite and fails closed

Every document declares a taxonomy schema version and finite limits for elements,
depth, children, capability records, text bytes, command argument bytes,
collection items, and extension payloads. Validation checks before allocation:

- known schema version and kind;
- unique stable element identity, exactly one root, valid parents, and an acyclic
  connected tree;
- kind/capability compatibility and required descriptor payloads;
- typed value, command, layout, style, binding, input, and accessibility records;
- all declared byte/count/depth bounds.

Unknown kinds, unknown required capabilities, malformed payloads, duplicates,
and limit overflow reject cook, load, activation, or replacement. Runtime never
guesses the nearest known kind, silently converts an unknown control to a panel,
or truncates required state. Tooling may preserve explicitly length-delimited
unknown records for round-trip editing, but such preservation is non-executable
and cannot make the document runtime-valid.

### 7. Compatibility and migration are explicit

The taxonomy schema uses major/minor compatibility. Adding an optional capability
or optional descriptor field with a deterministic default may be minor-compatible.
Changing a kind's numeric identity, required capability, value meaning, command
meaning, ownership, or lifecycle is major-breaking.

Cooked artifacts pin the exact taxonomy schema and canonical descriptor digest.
Source migrations are ordered, deterministic, bounded transforms performed before
validation. A migration produces a new candidate and diagnostics; it never edits
the active generation in place. Missing migration steps, ambiguous conversion,
loss of required data, or an unsupported future major version fail closed and
leave the source plus prior last-good runtime generation intact.

Editor palette labels, icons, categories, selection, expansion state, preview
handles, undo cursors, and native widget state are non-semantic projections and
never participate in compatibility identity.

### 8. ADR-176 is the sole taxonomy authority

Architecture documents, public descriptors, serializers, editors, templates,
tests, and package contracts reference this decision. They may project the
taxonomy for their own boundary, but they do not copy or redefine its kind list,
capability rules, numeric identities, or compatibility policy. A taxonomy change
therefore starts by superseding or revising this decision through a reviewed ADR,
then migrates all consumers in one deliberate change.

## Consequences

- Runtime, editor, renderer, tooling, and packages share one stable typed
  vocabulary without sharing implementation classes.
- Common controls compose from explicit capabilities, values, layout, style,
  input, and accessibility records rather than hidden widget behavior.
- Unknown or version-skewed content is diagnosable and cannot execute through a
  permissive fallback.
- Domain-specific UI remains cheap to author through documents and templates but
  does not expand the engine taxonomy accidentally.
- Future implementation tickets must provide serialization-width, malformed and
  unknown-kind, capability compatibility, migration, generation-retirement, and
  public-consumer regression coverage against this decision.

## Rejected Alternatives

### One virtual widget class hierarchy

Rejected because inheritance couples semantics, state, rendering, and lifecycle;
it also encourages native/backend types and callback ownership in the public
boundary.

### Toolkit or backend control names as the schema

Rejected because ImGui, Cocoa, Win32, web, and renderer primitives are projections
with different lifetimes and capabilities. None is a stable cross-platform
Runtime UI authority.

### String roles and property bags

Rejected because misspellings, version skew, and unsupported combinations become
late runtime behavior. Closed IDs and typed payloads make compatibility and
failure deterministic.

### One configurable mega-control

Rejected because arbitrary flag combinations create invalid states and conceal
which capabilities, value model, and input/accessibility semantics are required.

### Templates or gameplay packages define new core kinds

Rejected because templates are composition and packages are separately
namespaced extensions. Treating either as core creates competing registries and
unstable serialized identities.

### Renderer-owned element taxonomy

Rejected because the renderer owns projected primitives and backend execution,
not UI semantics, commands, focus, accessibility, or document compatibility.
