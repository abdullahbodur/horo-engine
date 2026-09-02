# ADR-082: Runtime UI Accessibility Capability and Ownership

- **Status**: Proposed
- **Date**: 2026-09-02
- **Supersedes**: None
- **Scope**: Runtime UI accessibility semantics, settings projection, navigation/focus, localized accessible text, immutable semantic snapshots, native platform bridge, capability reporting, editor validation, support matrix, failure, compatibility, qualification, unload, and shutdown
- **Issue**: [RUI-011.1](https://github.com/abdullahbodur/horo-engine/issues/802)
- **Jira**: [HORO-802](https://horo-engine.atlassian.net/browse/HORO-802)
- **Parent**: [RUI-011](https://github.com/abdullahbodur/horo-engine/issues/780)
- **Related**: [ADR-009](009-configuration-schema-precedence-and-secret-boundary.md), [ADR-015](015-accessibility-ownership-typed-transport-and-non-gating-policy.md), [ADR-073](073-runtime-ui-ownership-scope-and-update-order.md), [ADR-076](076-runtime-ui-style-asset-token-and-inheritance.md), [ADR-078](078-runtime-ui-input-context-and-player-routing.md), [ADR-081](081-runtime-ui-and-localization-ownership-boundary.md)
- **Normative documents**: [Accessibility Architecture](../architecture/runtime/accessibility-architecture.md), [Game UI and HUD](../architecture/runtime/game-ui-and-hud.md), [Runtime Lifecycle](../architecture/runtime/runtime-lifecycle.md), [Input Architecture](../architecture/runtime/input-architecture.md), [UI Design System](../architecture/editor/ui-design-system.md)

## Context

ADR-015 assigns accessibility authority per datum and behavior rather than to one
global manager. Runtime UI still needs a precise specialization: which subsystem
owns semantic nodes, how user settings override authored presentation, when native
assistive technology may observe a UI generation, what editor validation proves,
and how a product reports unsupported native integration without pretending that a
null adapter is usable support.

Visible UI, input geometry and accessibility semantics must describe the same
published generation. Exporting an authoring tree before successful presentation
can expose hidden or failed routes, while deriving semantics from renderer draw
commands loses role, state, relationships and actions. Native bridges also have OS
thread affinity and may be unavailable independently of the engine's semantic UI.

Support is multidimensional. Keyboard navigation, high contrast and reduced motion
can work while a native screen-reader adapter is unsupported. Headless tests can
qualify the semantic model but cannot prove interoperability with VoiceOver, UI
Automation or AT-SPI. This decision defines truthful states and evidence instead of
one broad `accessibilitySupported` flag.

## Decision

### 1. Runtime UI owns semantics; cooperating domains retain their authority

The responsibility split is:

| Responsibility | Owner |
|---|---|
| Stable UI element semantics, roles, state, relationships, accessible actions, semantic focus and immutable semantic snapshots | Runtime UI |
| Desired accessibility preferences and provenance | ADR-009 Configuration plus each declared setting domain |
| UI-scale, contrast, color-independent cues and reduced-motion projection | Runtime UI Style/Layout/Animation under ADR-076/077 |
| Physical input, mapping, player/audience routing, navigation and activation delivery | Input and ADR-078 Runtime UI input routing |
| Accessible names, descriptions, values and announcements resolved from message keys | ADR-081 Localization plus Runtime UI semantic projection |
| Native capability discovery, OS-node mapping, thread-affine dispatch and native lifetime | Platform accessibility adapter |
| Pixels, glyph/image resources and visual composition | Renderer |
| Authoring diagnostics, component rules, previews and qualification orchestration | HoroEditor and test tooling |
| Product support declaration and adapter composition | Application/host manifest |

Runtime UI exposes Horo-owned values and immutable snapshots. It never includes
NSAccessibility, UI Automation, AT-SPI, ImGui, DOM, renderer handles or native
window objects. Platform consumes the semantic transport and cannot query mutable
widgets. Renderer does not manufacture semantics from draw commands.

There is no process-global accessibility manager. The composition root supplies a
narrow configuration view, localization snapshots and an optional platform bridge
to each game/editor UI runtime.

### 2. The semantic model is typed and generation checked

Every eligible Runtime UI element has stable authored `UiElementId` and transient
generation-checked `UiAccessibilityNodeId`. A node contains a closed role, localized
name/description/value references, semantic state, allowed actions, relationships,
language/direction, live-region policy and logical bounds/visibility evidence.

Core roles include application, window/screen, dialog, alert, group, heading,
static text, button, toggle, checkbox, radio, slider, text field, link, image,
progress, list/list item, menu/menu item, tab/tab item, tree/tree item and table
roles. Extending the set requires a schema version and platform-mapping review;
custom strings do not become roles.

Role-valid state/action combinations are schema checked. Examples include checked
only on checkable roles, range metadata on sliders/progress, selected/expanded on
admitted containers/items and editable/selection metadata on text inputs. An
action names a typed Runtime UI command target, never a function pointer, script
closure, native callback or raw input event.

Parent/child, labelled-by, described-by, controls, owns and active-descendant
relations use node IDs from the same semantic generation. Cycles, dangling/cross-
runtime references, duplicate IDs, invalid role/state pairs and conflicting focus
reject the candidate.

### 3. Semantics publish with interaction and presentation evidence

VariableUpdate resolves semantics from the same immutable element, localization,
style, layout, route and input-context generations used for interaction. Extraction
produces one `UiAccessibilitySnapshot` per game runtime/audience/viewport as needed.
It contains owned bounded values, never tree pointers or lazy getters.

A node becomes native-visible only after the matching UI interaction revision has
been successfully presented. Covered, suppressed, suspended, exiting, clipped and
offscreen states remain distinct typed exposure policies; `Hidden` removes a node
from navigation/native exposure, while `Offscreen` may remain discoverable when the
platform contract supports it. Failed/skipped presentation cannot expose geometry
or focus that the player did not receive.

Pure semantic changes still create a new correlated semantic revision. Rendering
does not need to redraw unchanged pixels, but native dispatch and deterministic
recording observe the new revision. An active modal exports the appropriate modal
subtree and focus boundary under ADR-078/080; lower covered routes cannot remain
native-actionable.

### 4. Focus and actions remain Runtime UI/input authority

Semantic focus is the accessibility projection of the authoritative per-audience
Runtime UI focus state. A native request to focus, activate, increment, decrement,
set value, scroll or dismiss becomes a typed bounded platform-input command tagged
with runtime, audience, node, semantic and interaction revisions.

Runtime UI validates current ownership, visibility, enablement, action permission,
modal scope and expected generation at VariableUpdate. Platform never mutates a
node or invokes gameplay directly. Stale, cross-player, hidden, disabled or
unauthorized actions return typed rejection and may trigger bounded resync.

Keyboard/gamepad navigation works through ADR-078 whether or not a native bridge is
present. Native screen-reader focus and ordinary navigation focus may be correlated
but do not create a second mutable focus store.

### 5. Accessible text uses Localization snapshots

Accessible names, descriptions, values, hints and announcements store typed
`LocalizedMessageRef` values or explicitly classified user/content text. Runtime UI
resolves them using the exact ADR-081 snapshot pinned by the semantic/UI generation.
A native adapter receives owned resolved Unicode text and locale/direction evidence;
it never receives message keys, formatter callbacks or borrowed catalog strings.

Visible labels may provide names through an explicit labelled-by relation. Icon-
only actions require an independent accessible name. Placeholder text is not a
text-field name. Dynamic values have bounded update/coalescing policy so rapidly
changing meters do not flood assistive technology.

Live-region announcements declare Polite, Assertive or Off policy, stable event
identity and de-duplication window. They are admitted only from active semantic
owners. Localization/catalog replacement updates the semantic generation rather
than sending mixed-language node fields.

### 6. Preferences are immutable inputs, not duplicate UI settings

Configuration owns desired settings and provenance. Runtime UI captures one
compatible snapshot per VariableUpdate and derives behavior; it does not copy
preferences into another persistent store. UI scale, minimum readable sizes, high
contrast, reduced motion and other personal requirements apply after authored
style policy where ADR-076/077 define them and cannot be disabled by a theme,
route, renderer quality level or project branding.

An unsupported native bridge does not erase semantic metadata or desired settings.
The product may show capability-specific guidance, but must not silently turn the
preference off or rewrite configuration. Settings UI distinguishes preference,
engine semantic availability and current native capability.

### 7. Platform integration is optional, explicit and bounded

The Platform adapter advertises a revisioned capability descriptor with supported
semantic schema range, roles/actions/states, text/bounds/focus/live-region features,
native technology, thread affinity, queue byte/message limits and status:

- `Supported`: qualified adapter is composed and native service is usable;
- `Unavailable`: adapter is supported but temporarily cannot reach the service;
- `Unsupported`: this host/platform composition has no admitted native integration;
- `NotComposed`: product intentionally omitted an otherwise available adapter.

The first three preserve ADR-015 meanings; `NotComposed` is a host-composition fact,
not an invented native result. Capability is separate from user preference and may
change generation without changing UI semantics.

Submission is asynchronous, bounded and non-blocking. Platform copies call-duration
data into owned storage, preserves accepted generation/sequence order and performs
native work on the required OS thread/run loop. It never waits for native
acknowledgement in Runtime UI, input, render or audio phases.

Snapshot/delta overflow marks the native projection stale. Announcements are not
blindly replayed; node state performs a bounded full-snapshot resynchronization at
the next admitted boundary. Native callbacks carry an adapter/session generation so
late responses cannot target a reused runtime.

### 8. Null, recording and native adapters make different claims

`NullPlatformAccessibilityBridge` returns `Unsupported`, emits no native effects and
does not record messages. It supports omitted/headless compositions but is not
evidence that screen readers work.

`RecordingPlatformAccessibilityBridge` is test-only. It implements the same
capacity, ordering, resync, cancellation and action-validation transport under a
deterministic scheduler. It proves engine semantic transport, not native API or
assistive-technology interoperability.

Native macOS, Windows and Linux adapters may target VoiceOver/NSAccessibility, UI
Automation and AT-SPI respectively, but each remains `Unsupported` in product
claims until its implementation and platform qualification evidence exist. Other
platforms report their actual status; no backend or OS is inferred from Renderer.

### 9. Support is reported as a matrix with evidence

Products publish a versioned `AccessibilitySupportManifest` per host/platform/build
profile. It records each feature independently:

| Feature | Required semantic baseline | Qualification evidence |
|---|---|---|
| Accessible roles/names/state/actions | Runtime UI/ModelOnly | schema, golden snapshot and action tests |
| Keyboard/gamepad navigation and modal focus | Interactive Runtime UI | deterministic routing/focus tests |
| UI scale/high contrast/reduced motion | Rendered Runtime UI | layout/style/animation tests plus visual review |
| Captions/subtitles | Runtime UI, independent of Audio success | cue/localization/layout/overload tests |
| Native screen-reader bridge | Only where manifest says Supported | platform adapter, native API and assistive-technology interoperability tests |
| Headless semantic export | ModelOnly/test profiles that declare it | recording/golden snapshot tests |

`Implemented`, `Tested`, `Qualified` and `Supported` are separate evidence states.
Documentation-only architecture, a null bridge, compile success or a recording fake
cannot advance native support to `Supported`. A missing evidence artifact makes the
claim unqualified rather than silently passing.

Accessibility semantics are independent of graphics quality and product edition.
A product may truthfully omit a native adapter, but cannot label an inaccessible
quality tier as equivalent support.

### 10. Editor validation checks authored semantics without owning runtime state

HoroEditor validates UI documents/components using the same versioned semantic
schema. It checks required names, role/state/action compatibility, focus order,
modal containment, relation integrity, touch/target size policy, contrast pairs,
color-independent cues, localization coverage, text expansion, reduced motion and
caption safe areas.

Diagnostics name stable document/element/rule IDs and severity. They do not mutate
the runtime tree or auto-invent labels from variable names. Explicit safe quick
fixes produce normal revision-checked authoring commands. Preview runs an isolated
Runtime UI plus recording/native adapter selected by the developer and displays its
capability limits.

Cook/qualification policy decides which errors block a target. Core built-in
interactive controls missing required semantics are errors. Project-specific legal
or standards compliance remains a separate product assessment; passing engine
validation is not a certification claim.

### 11. Failure, privacy and limits are explicit

Results follow ADR-008 with codes for invalid schema/role/state/action/relation,
missing accessible name, stale revision, capability mismatch, queue full, oversized
snapshot/text, unavailable/unsupported/not-composed bridge, native failure,
cancellation, revoked owner and shutdown. Required semantic candidate failure
retains the prior last-good UI/semantic generation.

Limits cover nodes, depth, relations, actions, text bytes, live regions, updates per
frame, queue bytes/messages, resync attempts and diagnostics. Exhaustion coalesces
replaceable node updates or rejects the candidate/message according to policy; it
does not block, truncate required identity or emit a partially linked tree.

Accessible text may contain user/project content. Ordinary metrics use bounded
role/status/reason dimensions, never names, values, message keys or input. Native
dispatch receives only data required by the composed capability. Debug recording is
explicit, bounded, development-only and follows project privacy policy.

### 12. Compatibility, lifecycle and shutdown are transactional

Semantic snapshots declare schema version, required feature bits, runtime/audience/
viewport generations and localization/style/layout/interaction evidence. Adapters
negotiate a supported range before activation. Unknown required roles/actions or an
expired schema return capability mismatch; Platform does not reinterpret them as a
similar native role.

Runtime/route/element destruction publishes removal under the same presentation
ordering, closes action admission and retires snapshots after UI/platform leases
drain. Adapter replacement invalidates the native session, cancels queued work and
publishes a complete snapshot to the new supported session.

Shutdown closes platform action and semantic admission, invalidates sessions,
cancels queued announcements/updates, drains or detaches native callbacks through
owner tokens, retires UI semantic generations, then releases Platform,
Localization, Input and Configuration dependencies. Partial activation and repeated
shutdown are idempotent.

### 13. Verification is part of the contract

Required coverage includes:

- every core role/state/action combination, invalid combinations, duplicate/dangling
  relations, cycles, depth/node/text/action limits and deterministic ordering;
- authored/runtime/node identity across reload, route replacement, element removal,
  slot reuse, stale native actions and multiple game/player/viewport contexts;
- presented versus skipped/failed/covered/hidden/offscreen/modal exposure and exact
  focus/action eligibility revision;
- localized names/values/descriptions, label relations, icon-only controls, locale
  change, bidi, long pseudo-locales and announcement de-duplication/coalescing;
- configuration revision, high contrast, scale, reduced motion, personal override
  precedence and independence from renderer quality;
- keyboard/gamepad/touch/native actions, focus trapping/restoration, disconnect and
  player/audience isolation;
- queue count/byte overflow, full resync, unavailable recovery, adapter replacement,
  cancellation, late callbacks and shutdown at every lifecycle state;
- Null returns `Unsupported` with no output; Recording proves deterministic transport
  without being counted as native support;
- platform thread affinity, native role/action mapping and real assistive-technology
  interoperability for every manifest entry claiming `Supported`;
- editor validation, cook/qualification severity, visual matrices, captions without
  audio/device and headless/ModelOnly semantic snapshots;
- dependency tests proving Runtime UI/semantic APIs expose no native/ImGui/renderer
  types and Platform cannot call mutable widgets/gameplay.

## Consequences

Runtime UI semantics remain coherent with visible/input generations and usable in
model-only tests. Configuration, Localization, Input, Renderer, Platform and Editor
retain narrow authorities. Native support claims become evidence-based per feature
and platform instead of being inferred from semantic metadata or a null adapter.

The model adds versioned semantic snapshots, capability negotiation, bounded native
resynchronization and qualification manifests. Those costs are necessary to avoid
stale native trees, reverse dependencies and misleading accessibility claims.

## Rejected Alternatives

### Build accessibility nodes from renderer draw commands

Rejected because pixels do not preserve role, relationships, state, actions,
localized intent or authoritative focus.

### Let each platform adapter inspect Runtime UI widgets directly

Rejected because it leaks mutable lifetimes/native types across the boundary and
makes platform thread affinity part of widget behavior.

### Treat metadata presence as native screen-reader support

Rejected because engine semantics and OS/assistive-technology interoperability are
different capabilities requiring different evidence.

### Make unsupported native integration fail all Runtime UI activation

Rejected because semantic UI, keyboard navigation, captions and visual preferences
remain valuable and testable without a native bridge; products must report the
missing dimension truthfully.

### Use the null or recording adapter as production qualification

Rejected because neither invokes the native API or proves interoperability with an
actual assistive technology.

### Publish node callbacks through a global accessibility manager

Rejected because callbacks retain widget/module code across unload and create a
second focus/action/settings authority.
