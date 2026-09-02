# ADR-081: Runtime UI and Localization Ownership Boundary

- **Status**: Proposed
- **Date**: 2026-09-02
- **Supersedes**: None
- **Scope**: Runtime UI and Localization ownership, catalogs, locale policy, formatting, localized references, shaping/layout, localized asset and font fallback, immutable snapshots, change notification, failure, compatibility, editor preview, unload, and shutdown
- **Issue**: [RUI-010.1](https://github.com/abdullahbodur/horo-engine/issues/795)
- **Jira**: [HORO-795](https://horo-engine.atlassian.net/browse/HORO-795)
- **Parent**: [RUI-010](https://github.com/abdullahbodur/horo-engine/issues/779)
- **Related**: [ADR-008](008-error-model-exception-boundary-and-registry.md), [ADR-054](054-extension-and-package-authority-boundary.md), [ADR-073](073-runtime-ui-ownership-scope-and-update-order.md), [ADR-074](074-runtime-ui-layout-units-and-measure-arrange.md), [ADR-075](075-runtime-ui-font-asset-family-and-fallback.md)
- **Normative documents**: [Localization Architecture](../architecture/editor/localization.md), [Game UI and HUD](../architecture/runtime/game-ui-and-hud.md), [Runtime Lifecycle](../architecture/runtime/runtime-lifecycle.md), [Asset Pipeline](../architecture/runtime/asset-pipeline.md)

## Context

Runtime UI needs translated messages, typed formatting, locale-aware assets, font
fallback, shaping and layout. Localization needs no knowledge of widget trees,
routes, renderer resources or editor state to provide those inputs. If either
subsystem owns the whole pipeline, Localization becomes a UI/renderer dependency
or Runtime UI becomes a second catalog and locale authority.

There are also two product surfaces. HoroEditor localizes its own GUI, while each
game runtime can have a different product/project locale and may be model-only or
headless. Editor preview must exercise the game contract without making editor
language, ImGui font state or transient preview selection authoritative game data.

Translation fallback, font fallback and localized image/audio fallback are not the
same decision. A missing message chooses another catalog entry. A shaped cluster
chooses a font face. A localized asset reference chooses a cooked asset variant.
Conflating them makes cache identity, failure and package dependencies unstable.

ADR-073 owns Runtime UI scope and frame order. ADR-074 owns layout. ADR-075 owns
runtime font matching and fallback. This decision assigns the remaining boundary
so RUI-010 child tickets can implement catalog assets, locale switching, localized
text, formatting, fallback and editor tooling against one authority.

## Decision

### 1. Localization and Runtime UI have one-way typed collaboration

The ownership split is:

| Responsibility | Owner |
|---|---|
| Generic source/cooked asset identity, dependency transport, package publication and byte delivery | Assets |
| Catalog schema, message identity, namespace ownership, locale normalization/resolution, CLDR data, message formatting, translation fallback and immutable localization snapshots | Localization |
| Product default/source locale and per-game/session/user locale selection policy | Application/game host |
| Typed localized references stored by UI elements and binding/action argument projection | Runtime UI |
| Unicode segmentation, bidi, script runs, shaping, line breaking, font-chain use, text measurement, wrapping and layout | Runtime UI Text/Layout domains |
| Font family/face coverage, matching and font fallback | ADR-075 Runtime UI Font domain |
| Localized visual/audio asset role and presentation fallback policy | Owning feature/Runtime UI document schema |
| GPU glyph/image realization and immutable draw execution | Renderer |
| Editor catalog authoring, diagnostics and isolated preview orchestration | HoroEditor tooling |

Runtime UI depends only on Horo Localization value/snapshot/query contracts.
Localization does not include or call Runtime UI, Renderer, ImGui, editor screens,
ECS, native locale APIs or gameplay objects. The host composition root constructs
the services, supplies explicit capabilities and controls activation order.

Localization may consume cooked bytes through the narrow Assets contract. Assets
does not parse message formats, normalize locale tags or select fallbacks. Runtime
UI never opens catalog files, scans package paths or maintains a parallel active
locale.

### 2. Catalog and message identities are stable typed data

Localization owns these non-interchangeable identities:

- `LocalizationNamespaceId`: stable owner-qualified namespace;
- `MessageKey`: namespace plus semantic local key;
- `CatalogAssetId`: stable Assets identity for one authoring catalog;
- `CatalogGenerationId`: one validated immutable catalog payload revision;
- `LocalizationSnapshotId`: one complete active locale/fallback/formatter set;
- `LocalePolicyRevision`: application selection and fallback-policy revision.

Runtime UI serializes `LocalizedMessageRef`, containing `MessageKey`, a finite
typed argument schema and declared failure/accessibility policy. It never serializes
resolved prose, catalog slot/index, filesystem path, raw ICU object, borrowed
string pointer or editor preview handle as the authoritative text reference.

Keys are semantic and append-only within their compatibility window. Catalogs
declare normalized locale, namespace owner, schema/message-format versions and
argument metadata. Duplicate identities, namespace violations, incompatible
argument types or malformed patterns reject a candidate before publication.

### 3. Locale authority belongs to application policy

Each game runtime receives an explicit `GameLocaleContext` containing normalized
requested locale, product default, source fallback, admitted overrides and policy
revision. Localization validates BCP 47 tags and builds the CLDR parent-locale
chain. Runtime UI may request a locale change only through a typed application
command; it cannot mutate Localization or user settings directly.

A project may define one game locale or an explicit per-player locale profile.
Per-player locale is supported only when the product composition declares the
additional catalog/font/layout memory and presentation cost. It creates separate
localization/UI presentation generations; it never changes a process-global
locale. Shared world-space text must name which locale audience owns resolution.

Operating-system locale discovery is optional Platform evidence admitted by the
application policy. Platform never activates a locale or returns a native locale
object through public contracts.

Editor language and game-runtime language are separate contexts. Play/preview
receives an explicit game locale; changing HoroEditor language does not silently
change a running game, and project locale testing does not mutate editor settings.

### 4. Formatting ends at an immutable resolved-message boundary

Localization resolves one `LocalizedMessageRef` against one pinned
`LocalizationSnapshot` and returns an owned bounded `ResolvedLocalizedMessage`:

```cpp
struct ResolvedLocalizedMessage {
    LocalizationSnapshotId snapshot;
    MessageKey key;
    NormalizedLocale locale;
    TextDirectionHint directionHint;
    Utf8Text text;
    std::vector<TypedTextSpan> spans;
};
```

`text` and `spans` own their storage. Spans use validated byte offsets into `text`,
not pointers or views, so copying or moving the resolved message cannot invalidate
their targets.

Arguments use a closed type set such as integer, decimal, boolean, date/time,
duration, stable enum, path display value, shortcut and safe typed rich span.
Formatting owns plural/select/number/date rules and escaping. Patterns cannot call
gameplay functions, access arbitrary objects, inject HTML/ImGui commands or invent
link/command targets.

Localization produces Unicode text and validated semantic spans. It does not
shape glyphs, measure text, wrap lines or choose a renderer resource. Runtime UI
Text consumes the resolved message plus locale/language/direction evidence and
owns Unicode segmentation, bidi, shaping, line breaking and font-chain selection.
ADR-074 then measures and arranges the resulting logical runs.

### 5. Runtime UI freezes one coherent localization view per generation

At VariableUpdate, Runtime UI pins one compatible localization snapshot and
locale-policy revision before resolving visible/measurement-relevant text. Every
message, font policy, shaped run, intrinsic measurement and layout result in the
published UI generation records that exact evidence. Render extraction never
re-resolves a key or observes a newer catalog.

Resolution caches are keyed by snapshot, key, canonical typed arguments and safe
formatter-policy revision. Shaping/layout caches additionally include text,
language/script/direction, font registry and layout/style revisions. Cache entries
retain immutable data only and have explicit byte/count limits.

Mutable argument providers publish through ADR-079 snapshots. Localization never
queries providers, ECS or widgets while formatting, and a formatter cannot invoke
a callback into gameplay or Runtime UI.

### 6. Locale/catalog changes publish transactionally

A locale/catalog change follows:

```text
request policy change
  -> load and validate complete catalog/fallback candidate
  -> publish immutable LocalizationSnapshot
  -> emit typed revision event
  -> Runtime UI prepares text/font/layout candidate
  -> publish complete UI generation at ADR-073 cutoff
  -> retire old snapshots after leases close
```

Localization publication never patches catalogs in place. The revision event
contains snapshot/policy/namespace change identities and reason, not raw catalog
pointers or callbacks. Runtime UI subscriptions are generation-scoped leases and
event delivery is bounded/coalesced by latest compatible revision.

Runtime UI may continue presenting its last-good generation while the new locale
candidate prepares. It cannot mix old/new localized text, shaped runs or layout in
one generation. Required preparation failure keeps the previous UI presentation
and reports the requested/active revision mismatch to the application. Application
policy decides whether to revert the requested locale, show a safe recovery route
or terminate a strict qualification run.

### 7. Translation, font and asset fallback remain separate

Localization owns deterministic message fallback: requested locale through CLDR
parents, product default and source fallback, normalized and de-duplicated. It
returns the locale/catalog that supplied the resolved message and bounded missing-
key evidence.

ADR-075 owns font fallback. Localization supplies locale/script policy evidence;
it never supplies filenames, chooses faces or probes the operating system. Font
selection works on complete shaping clusters and its revision participates in the
UI text/layout generation.

Localized visual/audio references are typed variant sets owned by the presenting
feature/document. Localization supplies the normalized locale fallback chain;
Assets resolves declared stable `AssetId` variants and delivers bytes; the feature
chooses only among its authored variants under a declared Required, UseNeutral or
Omit policy. Neither Localization nor Assets substitutes an undeclared path or
generic file. Selected variants are cook/package dependencies and UI generation
evidence.

### 8. Namespace ownership and unload are explicit

Engine, project and extension namespaces are disjoint. Core namespaces are
reserved; project namespaces derive from stable project identity and extension
namespaces derive from canonical package identity. Registration descriptors are
finite inert metadata and activate only through the host-owned catalog registry.

Removal first closes catalog/formatter admission, publishes a candidate snapshot
without the namespace, invalidates dependent Runtime UI candidates and drains
snapshot/cache/UI/job leases. Only then may catalog memory, formatter strategy or
extension code unload. Immutable old snapshots may retain catalog data but cannot
retain extension callbacks or executable code.

Project/game content never overrides core messages except through an explicitly
declared branding/override namespace contract. Collisions fail the whole
registration batch and retain the previous snapshot.

### 9. Failures are typed, bounded and safe to present

Localization results follow ADR-008 with stable codes for invalid locale/catalog/
namespace/key/pattern/argument, missing required key, unsupported schema/format/
CLDR data, dependency/integrity/version failure, budget, cancellation, stale
revision and revoked owner. Context is bounded and may identify locale, namespace,
key and revision; it does not dump user values, full translated strings or catalog
bytes.

Missing optional translations use the deterministic fallback chain. If no safe
text exists, product policy selects a localized source fallback, a bounded safe
placeholder or strict failure. Shipping UI does not expose raw keys by default.
Formatting argument mismatch never throws through the frame loop or reuses a
partially formatted value.

Required catalog/fallback/font/localized-asset failure rejects candidate
activation and preserves last-good state. Optional presentation fallback must be
declared in authored data and remains observable. Runtime work never blocks on
network/download/source import or silently changes locale.

### 10. Catalog assets are cooked and compatibility-fingerprinted

Authoring catalogs are UTF-8, namespace/locale scoped and schema validated. The
Localization cooker emits deterministic immutable artifacts containing message
bytecode/data, canonical argument schemas, locale/namespace identity, fallback
metadata and integrity checks. Assets owns graph scheduling, cache/package
publication and byte transport; Localization owns semantic validation/fingerprint.

The fingerprint includes catalog schema, message-format syntax/engine, Unicode/
CLDR data version, canonical key/argument definitions, locale policy inputs and
relevant formatter strategy versions. Cooked headers have explicit scalar widths,
endianness, limits, required feature bits and compatibility window. Unknown
required semantics or expired versions request recook instead of guessing.

Cook dependency closure includes source/product fallback catalogs, declared
localized asset variants and locale-specific font policy assets needed by the
product profile. Editor preview may use validated authoring candidates; packaged
runtime consumes only published cooked generations.

### 11. Renderer, editor and headless compositions preserve the boundary

Renderer receives immutable positioned glyph runs and visual asset identities. It
does not receive message keys, catalog snapshots, formatter objects or native
locale handles and cannot re-resolve text per backend.

HoroEditor owns catalog editing, extraction, diagnostics and pseudo-locale tools.
Its GUI localization service is a separate host composition that follows the same
snapshot/value semantics. Runtime preview creates an isolated game Localization +
Runtime UI composition and communicates through typed commands/snapshots; ImGui
fonts, editor theme, selection and current editor language never enter game data.

ModelOnly/headless Runtime UI can resolve, shape and layout when requested by tests
or accessibility/export features without Renderer. A composition that omits
Runtime UI may still use Localization for CLI/human presentation; Localization
therefore cannot depend on Runtime UI.

### 12. Lifecycle and shutdown are ordered

Localization starts after Foundation/Jobs/Assets capabilities and before Runtime
UI consumers. It activates complete catalog snapshots before the first eligible UI
VariableUpdate. Background preparation captures immutable asset/catalog inputs,
cancellation and target owner generation; late completions cannot publish into a
reused game/project/module context.

Shutdown closes locale/catalog/formatter admission and revision subscriptions,
cancels and joins preparation, retires Runtime UI text/layout/render generations,
drains localization snapshot/cache leases, unloads extension/project namespaces,
then releases catalogs and Assets. Partial initialization and repeated shutdown
are idempotent. No callback may enter unloaded code after its activation lease is
revoked.

### 13. Verification is part of the contract

Required coverage includes:

- normalized BCP 47 tags, CLDR parents, override/default/source precedence and
  chain de-duplication;
- duplicate/foreign namespaces, missing/duplicate keys, malformed UTF-8/patterns,
  argument name/type drift and forbidden rich content;
- plural/select/number/date formatting and safe typed spans across supported
  locales with fixed snapshots;
- locale/catalog change with old/new snapshot leases, coalesced events, last-good
  UI presentation, cancellation and stale completion;
- no mixed localization/font/shaping/layout revision in one UI/render generation;
- translation versus font versus localized-asset fallback and required/neutral/
  omit failure policy;
- engine/project/extension isolation, registration collision, namespace removal,
  outstanding async/cache/UI leases and module unload;
- editor/game locale isolation, preview, packaged, ModelOnly and headless parity;
- bidi, script shaping, locale line breaking, long pseudo-locales, accessibility
  text and per-player locale audience separation;
- schema/message-format/Unicode/CLDR version skew, integrity, recook and byte-
  deterministic artifacts;
- byte/count/cache/event limits, redaction, missing-key diagnostic cardinality and
  shutdown after every partial lifecycle state;
- dependency tests proving Localization has no Runtime UI/Renderer/Editor/native
  API dependency and Renderer receives no catalog/locale authority.

## Consequences

Localization becomes reusable by editor, CLI, headless and game compositions
without becoming a widget or renderer subsystem. Runtime UI can react to locale
changes while pinning coherent text/font/layout generations. Catalog, font and
localized-asset fallbacks remain independently testable and package-visible.

The boundary requires composite revision keys, immutable snapshot leases and an
explicit application locale policy. Live locale changes may temporarily retain a
last-good UI generation while the replacement prepares, which is preferable to a
partially relaid-out mixed-language frame.

## Rejected Alternatives

### Let Runtime UI own catalogs and the active locale

Rejected because CLI/headless/editor consumers would depend on Runtime UI and each
presentation system could create a competing locale authority.

### Let Localization shape and lay out text

Rejected because shaping requires Runtime UI font generations and layout policy,
creating a reverse dependency and making Localization renderer/widget aware.

### Resolve keys during render execution

Rejected because backends could observe different catalog revisions, allocate or
format on frame-hot threads and produce geometry inconsistent with hit testing.

### Publish mutable catalogs and notify with callbacks

Rejected because replacement/unload can invalidate strings or executable code
while UI frames and background work still retain them.

### Use one fallback mechanism for text, fonts and assets

Rejected because the three domains have different identities, dependencies,
failure policy and qualification evidence.

### Share HoroEditor locale state with game preview

Rejected because editor preference and project/game product policy are separate
authorities and preview must reproduce packaged behavior.
