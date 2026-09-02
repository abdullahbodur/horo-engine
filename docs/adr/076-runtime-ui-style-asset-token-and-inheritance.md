# ADR-076: Runtime UI Style Asset, Token and Inheritance

- **Status**: Proposed
- **Date**: 2026-09-02
- **Supersedes**: None
- **Scope**: Runtime UI style asset and class identity, typed visual/layout/typography/imagery tokens, token references, inheritance and element precedence, visual-state overrides, cycles, cook/runtime lifecycle, errors, accessibility, compatibility, and editor/renderer separation
- **Issue**: [RUI-004.1](https://github.com/abdullahbodur/horo-engine/issues/727)
- **Jira**: [HORO-727](https://horo-engine.atlassian.net/browse/HORO-727)
- **Parent**: [RUI-004](https://github.com/abdullahbodur/horo-engine/issues/726)
- **Related**: [ADR-015](015-accessibility-ownership-typed-transport-and-non-gating-policy.md), [ADR-054](054-extension-and-package-authority-boundary.md), [ADR-073](073-runtime-ui-ownership-scope-and-update-order.md), [ADR-074](074-runtime-ui-layout-units-and-measure-arrange.md), [ADR-075](075-runtime-ui-font-asset-family-and-fallback.md)
- **Normative documents**: [Game UI and HUD](../architecture/runtime/game-ui-and-hud.md), [Asset Pipeline](../architecture/runtime/asset-pipeline.md), [Rendering Architecture](../architecture/runtime/rendering-architecture.md), [Editor UI Design System](../architecture/editor/ui-design-system.md)

## Context

Runtime UI needs reusable colors, spacing, typography, imagery, borders and state
overrides across menus and HUDs. Those values must be themeable without copying
literals into every element, while inheritance and token aliases must not create
cycles or change meaning with file order. Hovered, focused, pressed, disabled,
checked, selected and validation states may overlap; an unspecified winner would
make editor preview and packaged runtime disagree.

HoroEditor already has a design-system `Theme`, tokens and ImGui presentation
layer. Runtime game UI is different content with different ownership, serialization,
accessibility and package lifetimes. Importing `ImGuiStyle`, borrowing an editor
theme pointer or persisting editor token names as runtime state would make packaged
games depend on the editor and create two mutable style authorities.

Styles also affect multiple downstream systems. Typography and spacing can change
ADR-074 measurement; images/fonts are asset dependencies; colors/borders/opacity
feed render extraction; visual states depend on ADR-073 input/focus snapshots. A
reload must therefore resolve and validate a complete candidate style generation
before layout/render publication and retain old computed styles while in-flight
interaction/render snapshots still reference them.

This decision defines the authored/cooked style model and normative precedence.
RUI-004.2 implements element style resolution, RUI-004.3 owns runtime theme
switching/hot reload, RUI-004.4 owns animation time domains, and RUI-004.5 owns
state-transition interpolation. Those capabilities consume this contract and do
not add a second token, inheritance or visual-state authority.

## Decision

### 1. RuntimeUiService owns runtime style semantics

The responsibility split is:

| Responsibility | Owner |
|---|---|
| Stable asset identity, source/sidecar access, generic cook/cache/package/publication and runtime bytes | Assets |
| Style/token/class schemas, inheritance, typed resolution, visual-state precedence and computed-style generations | Runtime UI Style domain |
| Font family/face resolution and metrics | ADR-075 Runtime UI Font domain |
| Accessibility/user presentation policy | Application/Accessibility capability |
| Immutable draw data and native realization | Runtime UI extraction/Renderer |
| Runtime-style authoring and isolated preview | HoroEditor adapter |

One `RuntimeUiService` owns a generation-scoped `RuntimeStyleRegistry` for each game
runtime. It resolves cooked style assets into immutable tables and computes
generation-correlated styles for runtime elements. Assets does not interpret token
meaning; Renderer does not inherit or select states; HoroEditor does not lend its
`Theme`, `DesignTokens`, ImGui style stack or widget pointers to the registry.

Runtime style contributions are inert versioned assets. Importing or validating a
descriptor cannot register global tokens, inspect a service locator, mutate the
active theme, load resources or invoke renderer/editor callbacks.

### 2. Style, class, token and computed identities are distinct

The model uses stable typed identities:

- `RuntimeStyleAssetId`: one authored/cooked style namespace and dependency root;
- `UiStyleClassId`: one reusable property/state rule in that namespace;
- `UiStyleTokenId`: one semantic typed value in that namespace;
- `UiStylePropertyId`: one registered core/package-owned property definition;
- `RuntimeStyleGeneration`: one fully resolved registry generation;
- `UiComputedStyleId`: one immutable deduplicated result within a runtime generation.

IDs are normalized, bounded and namespace-qualified. Serialized elements reference
asset/class/token IDs and optional typed inline properties, never source paths,
display labels, JSON object positions, hashes alone, editor theme keys, ImGui enums,
renderer pipeline handles or computed-style slots.

Token and class display names are presentation metadata. Renaming a display label
does not change identity; changing a token/property meaning requires a schema/
compatibility revision rather than silently reusing an ID.

### 3. Token values are closed typed variants

Version 1 tokens admit these Horo-owned categories:

| Category | Representative typed value |
|---|---|
| Color | finite unpremultiplied linear-sRGB `UiColor` plus declared semantic role |
| Spacing/size | non-negative ADR-074 `UiDip`; signed values only for properties explicitly allowing offsets |
| Typography | ADR-075 family/token, weight, stretch, style, variation, size, line-height and letter-spacing |
| Imagery | `AssetId`, expected type, fit/tile policy, nine-slice insets and optional tint role |
| Shape | radius, border widths/style, outline and bounded shadow descriptor |
| Scalar | finite opacity/progress-like ratio with property-declared range |
| Enum/flag | closed alignment, overflow, sampling and presentation policy values |
| Motion reference | stable duration/easing/transition token consumed under RUI-004.4/004.5 |

Colors do not contain native pixel formats, HDR output encodings or ImGui packed
integers. Baseline color components are finite and bounded by the declared property;
Renderer performs working/output conversion and premultiplication. Typography
references ADR-075 font identity, not a filename/native face. Imagery references an
asset and typed expected role, not a renderer texture/sampler handle.

Every property descriptor declares its accepted token/value category, default,
whether it affects measure/arrange, paint, hit testing or accessibility, whether it
inherits through the element tree, sign/range constraints and required dependency
kind. Cross-category assignment is an error; no numeric/string coercion is allowed.

### 4. Token aliases are same-type, finite and acyclic

A token declaration contains either one literal typed value or one reference to
another token of the exact same category. References are fully qualified or resolve
within the declaring asset namespace; bare display strings and fallback name search
are forbidden.

Cook/preparation builds a token-reference graph across the complete style dependency
set, validates ownership/types/limits, rejects self/cross-asset cycles and flattens
each token to one literal plus dependency provenance. Resolution order, JSON/map
iteration and registration order do not affect the result.

Missing or category-incompatible token references fail the containing required
style candidate. A property may declare an optional explicit literal fallback in
its schema; the resolver cannot guess a zero, transparent color, default font or
nearest token. Alias depth and total references are bounded.

### 5. Asset and class inheritance are single-parent chains

A `RuntimeStyleAsset` may name at most one base style asset. A `UiStyleClass` may
name at most one base class from its own or an admitted dependency namespace.
Single inheritance keeps origin/provenance and override order reviewable. Multiple
parents/mixins are represented as explicit element class lists under Section 7,
not hidden linearization.

Base asset/class chains resolve root to leaf. A derived declaration may override an
existing token/property only with the same identity and type; it may add new IDs;
it cannot remove a required base token/property or change its semantic category.
Sealed declarations explicitly reject override.

The combined asset, class and token-reference graph is validated before cook and
again against cooked dependencies at runtime. Any cycle, excessive depth, missing
base, duplicate ID, incompatible override or ownership violation rejects the whole
candidate. Runtime never breaks a cycle by dropping an edge or selecting the first
loaded asset.

### 6. Element-tree inheritance is property-declared

Class inheritance and element-tree inheritance are different. After class/inline/
state resolution, only properties whose registered descriptor says
`inheritsToChildren` flow from a parent element to a child lacking a local value.
Version 1 inheritable properties are typography family/attributes, text color,
text opacity, language/direction evidence and selected text-decoration semantics.

Layout sizes, margin, padding, position, anchors, background/image, border/radius,
clip/overflow, element opacity, transform, pointer/focus behavior and transition
state do not inherit unless a future schema explicitly revises the property.

Inherited values retain origin asset/class/token/generation provenance. A child
cannot mutate its parent's computed style or retain a pointer into mutable parent
state. Reparenting or parent style change invalidates the bounded affected subtree
for the next candidate generation.

### 7. Element style precedence is explicit

For each property, the low-to-high precedence is:

1. registered property default;
2. root-to-leaf runtime style asset/base defaults;
3. parent-element inherited value, only for an inheritable property;
4. element type/default class;
5. element-authored class list in stable declared order;
6. element typed inline value;
7. matching visual-state override blocks under Section 8;
8. host Accessibility/user presentation policy overlay.

Within one class list, a later declared class may override an earlier class only for
the same property type. The flattened cooked class IDs and stable element-authored
order are part of semantic data. Source file order, JSON object order, asset load
completion, hash iteration and editor edit history never create precedence.

Accessibility policy is not ordinary content inheritance. It may enforce contrast,
minimum text/control metrics, focus indicators, reduced motion or imagery policy at
the final typed overlay. Runtime content cannot opt out of a host-required policy;
the effective snapshot records both authored and policy revisions.

### 8. Visual states use a closed bitset and stable override ordering

Version 1 visual state evidence is a closed `UiVisualStateMask` containing
`Checked`, `Selected`, `Focused`, `Hovered`, `Pressed`, `Dragging`, `Disabled`,
`Invalid` and `Busy`. ADR-073 input/focus/control owners publish the state mask;
style data cannot set interaction truth merely to obtain a visual.

Each state override declares required bits, forbidden bits, a bounded property set
and an explicit `UiStateLayer`:

```text
Selection -> Focus -> Pointer -> Activation -> Validation -> Availability
```

Matching blocks apply from low to high layer. Inside one layer, fewer required bits
apply before more specific blocks; equal specificity follows stable authored block
order. A later-applied block wins only for properties it declares. Forbidden bits
filter matching and do not add specificity. This gives combined states such as
focused+hovered+pressed one deterministic result without a combinatorial class.

The built-in layer assignment is fixed: checked/selected use `Selection`, focused
uses `Focus`, hovered/dragging use `Pointer`, pressed uses `Activation`, invalid
uses `Validation`, and disabled/busy use the highest state layer, `Availability`.
This makes terminal availability styling override validation indicators for any
property it declares, reducing contradictory error/focus noise while an element
cannot be acted upon. Host Accessibility/user policy remains the final property
overlay above every state layer. A package may add state bits only through a
versioned registered property/state namespace and explicit layer; unknown required
bits reject compatibility.

State overrides are immediate resolved values in this ADR. RUI-004.5 may derive
transitions between old/new computed styles using RUI-004.4 clocks, but animation
cannot change precedence, interaction state, target value identity or layout phase.

### 9. Style assets declare every dependency and cook deterministically

The Runtime UI Style cooker emits a versioned `CookedRuntimeStyle` containing:

- asset/class/token/property IDs and schema/feature versions;
- flattened base chains, token literals and provenance;
- typed base/class/inline-compatible property tables;
- normalized state selectors/layers and stable authored order;
- dependency IDs/digests for fonts, imagery, materials and other style assets;
- property effect masks, limits, integrity digest and compatibility window.

AST owns stable asset identity, source/sidecar bytes, generic cook scheduling, cache
keys, dependency graph, staging and atomic publication. The Style domain owns the
semantic schema, normalization, flattening and payload validator. Source editor
layout, inspector expansion, selected token, preview state, `ImGuiStyle`, editor
theme, filesystem path and native/renderer handles are excluded from cook identity
and output.

The same validated inputs produce byte-identical cooked style bytes. Changing any
base/token/class/state/property/dependency/schema/provider version that can alter
the resolved result changes the semantic fingerprint. Runtime does not parse source
JSON/YAML, discover project files or lazily fetch a missing dependency.

### 10. Resolution publishes immutable computed-style snapshots

Runtime preparation resolves the complete cooked dependency graph, validates all
typed values/references/limits/capabilities and creates a private
`RuntimeStyleRegistry` candidate. For each active runtime tree, RUI-004.2 resolves
element styles against one document, style registry, font, locale, accessibility,
interaction and content revision.

The candidate produces immutable `UiComputedStyle` values and deduplicated
`UiComputedStyleId` handles. A value contains only Horo typed logical/layout/paint/
resource data plus provenance/revisions. It contains no mutable class maps, editor
widgets, native colors, renderer resource pointers or callback functions.

Publication occurs with ADR-073's VariableUpdate layout/interaction generation.
Measure-affecting changes create a new ADR-074 measure/arrange candidate; paint-only
changes still create a new render/interaction-correlated style snapshot but need
not invalidate unaffected logical boxes. Required failure publishes nothing and
retains the prior last-good style/UI generation according to activation policy.

Old computed styles, font/image/material dependencies, layout and render snapshots
remain leased until every frame/presentation reference retires. Reload, theme
switch, scope unload, viewport replacement, cancellation and shutdown cannot
publish late results into a new owner generation.

### 11. Editor and runtime styles are intentionally separate systems

HoroEditor's `Theme`, `DesignTokens`, component variants and ImGui styling remain
editor-process presentation owned by the Editor design system. A runtime style
asset cannot inherit an editor theme, reference an editor token ID, import an
`ImGuiStyle`, read the current dock/widget state or use editor accessibility/user
preferences as portable game defaults.

The editor Runtime UI style tool is an adapter over the same authored/cooked model:
it edits typed tokens/classes/states, validates through the Style domain and renders
an isolated preview runtime. Preview-only overrides are session state and apply to
the asset only through an explicit revision-checked authoring command. Play/preview
never mutates the editor's own theme, and editor theme switching never silently
changes game content.

A product may offer an explicit conversion/import tool between selected editor and
runtime semantic tokens. Conversion creates reviewed runtime literals/references
with provenance; it is not a live inheritance edge or compatibility fallback.

### 12. Renderer consumes resolved paint data only

`UiRenderSnapshot` extraction consumes immutable computed styles and emits bounded
draw/text/image/clip data. Renderer resolves Horo resource IDs and converts linear
colors to the rendering/output contract. It may batch/deduplicate equivalent paint
state, but cannot resolve tokens/classes/states, choose a font/image fallback,
change accessibility policy or write a computed style.

Backend replacement, resource eviction and pipeline changes preserve the same
semantic computed style. Missing required render realization returns a typed
resource/render failure; it does not substitute an editor color, native widget or
different token. Null/ModelOnly compositions validate style/layout/render extraction
without claiming native pixel output.

### 13. Errors, limits and compatibility are typed

Errors follow ADR-008 with stable reason codes for malformed/unknown schema,
duplicate/foreign ID, type/range mismatch, missing/sealed override, asset/class/
token cycle, excessive depth/count, invalid state selector/layer, missing dependency,
integrity/version mismatch, unsupported property/state, policy conflict, budget,
cancellation and stale generation. Context includes bounded asset/class/token/
property/element/revision paths and source locations without user text or secret
content.

Limits cover assets/dependencies, inheritance depth, tokens, aliases, classes,
properties, state blocks/bits, selector combinations, imagery/font dependencies,
computed styles, resolution work and diagnostics. Exhaustion rejects/backpressures
according to policy; it never drops a required rule or silently truncates a chain.

Cooked formats use explicit widths/endianness, schema/feature versions and integrity
digests. Unknown required token category/property/state/layer rejects the artifact.
Optional forward-compatible data can be ignored only when its schema declares that
behavior and its absence cannot change a required computed value. Older expired or
newer incompatible artifacts request recook rather than guessing editor/CSS defaults.

### 14. Verification is part of the contract

Required coverage includes:

- every typed token category, legal/illegal ranges, same/cross-type aliases and
  missing references;
- asset/class/token self and cross cycles, deep chains, duplicates, sealed and
  incompatible overrides, deterministic flattened provenance;
- property defaults, element inheritance, type/class-list/inline/state/
  accessibility precedence and non-inheritable property isolation;
- all visual state bits/layers, combined states, required/forbidden matching,
  specificity/authored-order ties, disabled+invalid and busy+invalid availability
  precedence, and unknown package state compatibility;
- font/image/material dependencies, semantic cook fingerprint changes,
  byte-identical cook and malformed/version-skewed payloads;
- measure-affecting versus paint-only invalidation, locale/font/accessibility/
  interaction changes and last-good publication;
- reload/theme switch with old layout/render leases, cancellation, scope unload,
  backend replacement and repeated shutdown;
- identical computed styles in editor preview, packaged, headless ModelOnly and all
  renderer peers;
- proof that editor theme/ImGui state, paths, pointers, native values and transient
  preview state cannot enter cooked/runtime snapshots;
- capacity/backpressure, bounded/redacted diagnostics and fuzz/property tests for
  source/cooked graphs and selector resolution.

Golden tests compare canonical cooked tables, flattened token/class provenance and
typed computed styles. Screenshot tests may qualify later renderer/theme results,
but cannot replace semantic resolution tests.

## Consequences

Runtime UI gains one deterministic style language whose token values, inheritance,
overrides and combined visual states behave identically in editor preview, packaged
games and headless validation. Assets, Accessibility, Font, Renderer and Editor
retain clear authorities, and reload can preserve last-good generations safely.

The cost is closed typed schemas, stable IDs/namespaces, single-parent chains,
explicit class-list order, graph validation, cooked flattening, provenance and
generation/lease tracking. Runtime styles cannot directly reuse arbitrary CSS,
ImGui or editor theme data.

## Rejected Alternatives

### Reuse HoroEditor Theme or ImGuiStyle for game UI

Rejected because packaged runtime content cannot depend on editor process state,
native ImGui types or user workspace preferences. The systems have separate assets
and lifetimes.

### Store tokens and properties as string-to-variant maps

Rejected because coercion, typos, package collisions and runtime parsing would make
type/ownership errors late and hot paths stringly typed. IDs and values are closed
typed data.

### Allow arbitrary multiple inheritance

Rejected because hidden linearization makes override provenance and cycles hard to
review. Assets/classes have one parent; explicit class lists provide composition.

### Break inheritance or token cycles by choosing the first loaded edge

Rejected because load order is not semantics and would hide invalid content. The
complete candidate is rejected with a cycle path.

### Use last file or editor edit order as precedence

Rejected because serialization/map order and editing history are unstable. The
normative property and visual-state orders are explicit.

### Let Renderer resolve tokens and visual states

Rejected because backend/resource state must not change semantic styles, layout or
accessibility. Renderer consumes immutable resolved paint data only.

### Copy every state into a separate complete style

Rejected because combined states become exponential and drift from base values.
Typed matching override blocks change only declared properties in stable layers.

### Silently default an unknown or missing token

Rejected because transparent/zero/default-font guesses can hide broken required UI.
Only a schema-declared typed fallback is allowed and remains observable.
