# ADR-075: Runtime UI Font Asset, Family and Fallback

- **Status**: Proposed
- **Date**: 2026-09-02
- **Supersedes**: None
- **Scope**: Runtime UI font source/face/family identity, weights/styles/variations, deterministic face matching and fallback chains, import/cook dependencies, platform discovery, missing-font behavior, lifecycle, errors, security, and compatibility
- **Issue**: [RUI-003.1](https://github.com/abdullahbodur/horo-engine/issues/717)
- **Jira**: [HORO-717](https://horo-engine.atlassian.net/browse/HORO-717)
- **Parent**: [RUI-003](https://github.com/abdullahbodur/horo-engine/issues/716)
- **Related**: [ADR-008](008-error-model-exception-boundary-and-registry.md), [ADR-054](054-extension-and-package-authority-boundary.md), [ADR-073](073-runtime-ui-ownership-scope-and-update-order.md), [ADR-074](074-runtime-ui-layout-units-and-measure-arrange.md)
- **Normative documents**: [Game UI and HUD](../architecture/runtime/game-ui-and-hud.md), [Asset Pipeline](../architecture/runtime/asset-pipeline.md), [Rendering Architecture](../architecture/runtime/rendering-architecture.md)

## Context

Runtime UI text needs stable font assets, family names, face selection, fallback
coverage, metrics and cooked bytes before shaping, layout and glyph rendering can
be implemented. A family may contain regular, italic, variable and collection
faces, while one localized string may require several scripts and fallback fonts.
Selecting by a filename, operating-system family name or renderer-native font
object would make results depend on machine state and leak ownership across Assets,
Runtime UI, Localization, Platform and Renderer.

Fonts are also untrusted structured binaries. Importers must bound table counts and
sizes, validate offsets and reject malformed collections. Cooking may preserve full
faces or produce licensed subsets, but must retain tables required by later shaping
and must include every declared fallback dependency. At runtime a locale, font,
content or family revision can change intrinsic metrics and therefore must publish
with the ADR-073/074 layout lifecycle rather than replacing a face under an
in-flight snapshot.

Platform font discovery can be useful for editor inspection, development tools and
an explicitly non-portable product profile. It cannot be an invisible fallback:
installed families, versions, variations, licenses and glyph coverage differ by OS
and machine. Packaged output must state whether it is self-contained or requires a
reported platform-font capability.

This decision defines font asset and fallback authority. RUI-003.2 owns Unicode
segmentation/shaping and glyph-run generation, RUI-003.3 owns bidi and locale line
breaking, RUI-003.4 owns glyph-atlas realization, and RUI-003.5 owns text element
wrapping/overflow. Those systems consume this decision's immutable face instances,
coverage and metrics; they do not redefine family matching or fallback order.

## Decision

### 1. Assets transports fonts; Runtime UI owns font semantics

The responsibility split is:

| Responsibility | Owner |
|---|---|
| Stable `AssetId`, source/sidecar access, generic importer/cooker orchestration, cache, package generation, atomic publication and runtime byte delivery | Assets |
| Font source parsing, authoring/cooked schemas, family/face/variation identity, matching, fallback expansion, coverage/metric validation and font compatibility | Runtime UI Font domain |
| Locale/script/language evidence and product localization fallback profile | Localization/application policy |
| Optional installed-font enumeration and bounded byte acquisition | Platform font-discovery capability |
| Script shaping, bidi/line breaking, glyph atlas and text layout | RUI-003 child capabilities |
| GPU images/buffers, upload and sampling | Renderer |

The host composition root registers font import/cook/runtime strategies through
the existing typed catalogs. Assets does not parse OpenType tables or choose a
fallback face. Runtime UI does not scan files, create a cache/output tree, publish
asset generations, discover a service locator or own renderer resources. Platform
and Renderer never return native font handles through a Horo public contract.

One game-runtime `RuntimeUiService` owns a generation-scoped `RuntimeFontRegistry`
over immutable cooked font-family generations. Editor preview uses the same font
domain model in an isolated preview runtime; ImGui/editor design-system fonts are
not runtime font assets.

### 2. Source, face, family and instance identities are distinct

The model has four non-interchangeable identities:

- `FontSourceAssetId`: one tracked source binary such as OpenType/TrueType font or
  collection. It retains stable asset identity independent of path.
- `FontFaceId`: one face record within one exact source revision, identified by a
  stable authored face key plus collection index and normalized metadata.
- `FontFamilyAssetId`: one authored Horo family descriptor containing ordered face
  references, matching metadata, fallback family references and cook policy.
- `FontFaceInstanceId`: one immutable runtime face generation plus a complete
  normalized variation-coordinate set and synthetic-style policy.

Serialized UI/styles reference `FontFamilyAssetId` or a typed semantic font token,
never a filename, display family string, collection index alone, platform name,
PostScript name, memory pointer, glyph-atlas slot or native handle. Display names
are presentation metadata and cannot resolve identity.

Runtime instance/glyph handles are generation checked and never serialized. Source
hash, face table fingerprint, family descriptor digest, cook target/profile and
font schema versions participate in cooked/runtime compatibility identity.

### 3. Font source import is strict, bounded and inert

Baseline source containers are bounded OpenType-compatible `.ttf`, `.otf`, `.ttc`
and `.otc` files. Additional containers require a registered typed importer and
schema revision; an extension alone never grants support. Web/network font loading
and remote CSS are outside the runtime contract.

Import validates the container and each selected face before producing an immutable
`FontFaceModel`. Validation includes:

- file/table/face count and byte limits, checked offsets/ranges and non-overlap
  rules required by the admitted container;
- required scalar/outline, character-map, metrics and naming tables;
- unique normalized face keys and collection indexes;
- finite units-per-em, ascender, descender, line gap, underline and variation
  axis/range/default metrics;
- character-map ordering, glyph-count bounds and coverage-index construction;
- checksums and deterministic canonical metadata extraction;
- rejection of malformed, recursive, oversized, unsupported or suspicious tables.

Import never executes embedded programs, loads a platform font, creates a glyph
atlas, shapes user text or invokes renderer APIs. Optional font features/hinting
programs are preserved only when the cook policy and later qualified rasterizer
support them; source code is not executed during generic AST publication.

Font sources are untrusted. Parsing runs within the repository's importer resource,
cancellation and isolation policy. Diagnostics identify asset/face/table/reason but
do not dump proprietary font bytes or user text.

### 4. Family descriptors provide deterministic face matching

A `FontFamilyAsset` owns a finite ordered face set. Each face entry declares:

```cpp
struct FontFaceDescriptor {
    FontFaceId face;
    FontWeight weight;       // integer 1..1000
    FontStretch stretch;     // normalized bounded percentage
    FontStyle style;         // Normal, Italic, or Oblique(angle)
    FontVariationSet defaults;
    FontSynthesisPolicy synthesis;
};
```

Duplicate `(weight, stretch, style, variation-defaults)` entries, out-of-range
values, conflicting source metadata, undeclared axes or unstable face references
reject the family. Synthetic bold/italic is disabled by default. A family may
explicitly permit a bounded synthetic transform for a missing request, but the
result is a different `FontFaceInstanceId` and diagnostics/cook identity record it.

Given one requested family, weight, stretch, style and variation set, face matching
sorts candidates by this lexicographic tuple:

1. exact style; compatible `Italic`/`Oblique` when policy permits; other style;
2. absolute stretch distance, then lower normalized stretch;
3. absolute weight distance, then the lower weight for requests at or below 500
   and the higher weight for requests above 500;
4. stable authored face order and `FontFaceId`.

Requested variation coordinates are clamped only when the descriptor explicitly
allows clamping; otherwise an out-of-range coordinate fails. Unspecified axes use
the family entry's normalized default. Matching never depends on source filename,
registration order, platform enumeration, hash iteration or atlas availability.

### 5. Fallback is an ordered, validated family graph

Each family declares a finite ordered `fallbackFamilies` list. Application/
Localization policy may prepend or append a versioned script/locale fallback
profile and must name one terminal product fallback family. At preparation, Runtime
UI expands the graph depth-first in declared order, de-duplicates by first
occurrence and publishes a flat immutable `FontFallbackChain`. Self edges, cycles,
duplicates inside one list, missing families, excessive depth/count and a missing
terminal family reject the candidate.

Resolution for one shaping cluster/run is:

1. start with the requested primary family and requested face attributes;
2. choose that family's deterministic face candidate;
3. accept it only if its immutable coverage index contains every required scalar
   for the cluster plus required variation-selector/feature support;
4. continue through the flattened family chain using the same requested attributes;
5. return one `ResolvedFontFace` with face/instance/family/fallback-chain/source
   generations and the selected chain index.

Fallback does not split a grapheme cluster codepoint by codepoint. RUI-003.2 may
split script/directional runs at valid cluster boundaries, but consumes this same
ordered resolver. Emoji/color-glyph capability and sequence coverage are explicit
face capabilities; a monochrome face cannot silently claim a required color mode.

Family fallback and translation fallback are separate. Localization supplies
normalized locale/script evidence and policy revision, not a font filename or
native face. Changing locale can select a different versioned fallback profile and
therefore creates a new font/layout candidate generation.

[ADR-081](081-runtime-ui-and-localization-ownership-boundary.md) owns the adjacent
catalog/locale/formatting snapshot boundary and keeps translation, font and
localized visual/audio asset fallback as separate revisioned decisions.

### 6. Missing-font behavior is explicit and observable

Every shipping/self-contained profile includes a terminal cooked family with a
validated `.notdef` glyph and product-approved replacement glyph policy. Runtime
content that has no covering face follows one declared `MissingGlyphPolicy`:

- `Replacement`: emit the terminal face's replacement/`.notdef` glyph with stable
  metrics and a bounded once-per-face/script/revision diagnostic;
- `OmitWithAdvance`: omit visible ink but retain a declared replacement advance,
  allowed only for explicitly optional/decorative text;
- `FailStrict`: return a typed missing-coverage failure for validation, cook and
  qualification paths.

Required UI activation also distinguishes missing family/face/artifact from a
runtime string containing an unsupported cluster. Missing required asset or
invalid chain fails candidate activation and retains the prior last-good font/UI
generation. Arbitrary later content uses the declared missing-glyph policy; it
does not crash, block on I/O or query installed fonts.

Raw source characters, translated strings and user input are not included in
ordinary diagnostics. Observability uses bounded script/range/category evidence,
stable family/face IDs and counts.

### 7. Cooking publishes self-describing font artifacts and dependencies

The font cooker consumes validated immutable face/family models and emits a
versioned `CookedFontFamily` manifest plus referenced `CookedFontFace` payloads.
The family artifact declares:

- schema/ABI versions, family/face/source IDs and canonical fingerprints;
- normalized face attributes, variations and synthesis policy;
- flattened fallback chain and terminal policy;
- compact immutable coverage and metric tables;
- retained shaping/raster tables and their qualified feature capabilities;
- subsetting mode, included ranges/glyph closure and required dependency IDs;
- target/profile, byte/count limits, integrity digest and compatibility window.

Cook dependency edges include every source face, fallback family, locale/script
fallback profile and configured feature data. AST owns graph scheduling, cache keys,
staging and atomic generation publication; the font domain owns the semantic
fingerprint and payload validation.

Subsetting is explicit: `FullFace`, `DeclaredUnicodeRanges`, or
`ClosedStaticCorpus`. Static-corpus closure includes required replacement glyphs,
composites, variation dependencies and shaping substitutions reachable under the
admitted feature set. Dynamic/localized/user-input text must use full faces or a
declared range set covering its product contract. Missing dynamic coverage cannot
trigger runtime source access, remote download or recook.

Editor preview may use source models, but packaged/runtime composition consumes
only published cooked artifacts. Cookers emit no GPU atlas/image and do not embed
absolute source paths, editor selection, preview caches or native handles.

### 8. Platform font discovery is optional and never a hidden fallback

Platform may expose an optional `PlatformFontDiscoveryCapability` with:

- an immutable capability/support revision and supported container/feature limits;
- bounded enumeration/filter queries returning Horo metadata and opaque discovery
  IDs, not native handles;
- explicit byte acquisition for an authorized discovered face;
- source fingerprint, normalized face metadata and availability generation;
- typed unavailable, permission, licensing, stale-generation and malformed errors.

The capability is absent in headless and portable packaged compositions by
default. Its presence does not append installed fonts to a fallback chain.

Two product modes are admitted:

- `SelfContained`: every runtime face is a cooked project/package asset; platform
  discovery is ignored for runtime resolution.
- `SystemAugmented`: an explicit manifest names required/optional discovery
  fingerprints and fallback behavior. Activation verifies the current capability
  and exact compatible face before publication; absence never substitutes a
  same-named installed family.

Editor tooling may preview a discovered font as visibly non-portable. Saving a
portable family requires an authorized import/copy into a tracked font source and
records project-owned metadata; the editor cannot serialize the discovery ID.
Licensing/embedding metadata and user authorization are explicit inputs. Horo does
not infer redistribution rights from successful OS discovery.

### 9. Runtime activation and reload are transactional

Font preparation resolves the complete cooked family/fallback graph, validates
headers/digests/capabilities/limits, maps or copies bounded immutable face bytes,
builds coverage/metric indexes and creates candidate face instances without
publishing them. Required success publishes one `RuntimeFontRegistry` generation
at the ADR-073 owner-thread/VariableUpdate boundary.

UI document/style/font/locale/scale/content changes capture exact registry and
policy revisions. ADR-074 intrinsic text measurement, future shaping and
`UiRenderSnapshot` extraction use one generation only. A replacement never mixes
old metrics with new glyph identity in one layout/render snapshot.

Old face bytes, shaped runs, layout snapshots and glyph-atlas resources remain
leased until every interaction/frame/presentation reference retires. Reload failure
retains the last-good generation. Scope unload cancels pending work and prevents
late publication into a reused owner generation. Shutdown closes admission,
cancels/joins preparation, retires UI/layout/text snapshots, releases renderer
leases, face instances and mapped artifacts, then releases Assets/Platform.

### 10. Shaping and Renderer consume face identity; they do not select it

RUI-003.2 receives immutable Unicode text, language/script/direction/features and a
resolved fallback chain. It returns bounded glyph runs naming
`FontFaceInstanceId`, glyph IDs, clusters, logical advances/offsets and source
revisions. It cannot change family order based on a native shaping library's font
discovery.

Renderer receives immutable positioned glyph runs and Horo face/glyph resource
identities. RUI-003.4/Renderer may rasterize/cache glyph images and own atlas
residency, uploads and deferred GPU retirement. Atlas miss, eviction or backend
replacement cannot select a different font, modify glyph metrics, alter line
breaking or feed physical pixel results back into ADR-074 layout.

Third-party parsers/shapers/rasterizers remain private strategies behind bounded
Horo contracts. Their pointers, allocators, exceptions, tables and native error
codes do not cross public/editor/runtime snapshot boundaries.

### 11. Errors, limits and compatibility are typed

Font results follow ADR-008 and include stable codes for malformed source/table,
unsupported container/feature, duplicate identity, invalid attributes/variation,
fallback cycle/depth, missing dependency/coverage, subset closure, integrity/
version mismatch, budget, cancellation, stale generation, platform capability and
licensing-policy failure. Context carries bounded asset/family/face/table/target/
revision evidence without raw bytes or text.

Limits exist for source/artifact bytes, faces, tables, axes, family faces, fallback
depth/count, coverage ranges, mapped resident bytes, concurrent preparations and
diagnostic cardinality. Exhaustion returns failure/backpressure according to the
operation policy; it never truncates a required chain or silently drops a table.

Cooked headers use explicit endianness, scalar widths, schema version, feature bits
and checksums. The runtime supports only its documented compatibility window;
newer required semantics and older expired versions request recook rather than
guessing. Provider/library version changes enter semantic cook/runtime fingerprints
when they can alter metadata, coverage, metrics or retained tables.

### 12. Verification is part of the contract

Required coverage includes:

- valid/malformed/oversized TTF, OTF and collection fixtures; table bounds,
  overlaps, checksums, face indexes, duplicate keys and parser cancellation;
- regular/italic/oblique, weights 1/400/500/700/1000, stretch ties, authored-order
  ties, variation defaults/ranges/clamp policy and disabled/enabled synthesis;
- fallback ordering, first-occurrence de-duplication, self/cross cycles, missing
  terminal family, depth/count limits and script/locale profile changes;
- grapheme/variation-selector/color-glyph coverage and no codepoint-level cluster
  fragmentation;
- replacement, omit-with-advance and strict missing behavior with bounded/redacted
  diagnostics;
- full/range/static subsets, composite/substitution closure, dynamic input coverage,
  dependency/cache-key changes and byte-identical deterministic cook;
- SelfContained and SystemAugmented profiles, capability absence, same-name wrong
  fingerprint, stale discovery, permission/licensing failure and headless mode;
- locale/font/style/content/scale reload, last-good retention, concurrent old/new
  layout/render leases, scope unload and repeated shutdown;
- equal family/face/fallback resolution across editor preview, packaged, headless
  ModelOnly and renderer peers;
- version skew, integrity failure, resident/budget pressure and no parser/platform/
  renderer/native types in public contracts.

Fuzzing targets source/container/table parsing and cooked artifact validation.
Golden fixtures compare canonical metadata, flattened chains, selected face IDs,
coverage decisions and logical metrics rather than native raster pixels.

## Consequences

Font families and fallback results become stable project/runtime data rather than
machine-dependent names. Assets, Runtime UI, Localization, Platform and Renderer
have non-overlapping authorities; layout and shaping can pin exact metric/face
generations; portable packages can prove all font dependencies are present.

The cost is explicit family descriptors, imported/cooked schemas, coverage indexes,
font fingerprints, subset closure, platform-capability policy and generation/lease
tracking. Projects cannot rely on an arbitrary installed font appearing at runtime.

## Rejected Alternatives

### Serialize font filenames or display family names

Rejected because paths/names are not stable identity, collections contain multiple
faces and installed families differ by host. UI references Horo asset/family IDs.

### Let the shaping library or OS choose fallback fonts

Rejected because fallback order, coverage and metrics would be machine/library
state rather than versioned product data. Libraries execute Horo's selected chain.

### Always scan platform fonts when a glyph is missing

Rejected because it is blocking, non-deterministic, non-portable and may violate
licensing/product policy. Discovery is an explicit optional capability and profile.

### Make Renderer own font faces and family matching

Rejected because GPU atlas residency is not semantic identity and backend changes
must not change layout, fallback or glyph metrics.

### Store glyph atlases as the canonical cooked font

Rejected because atlases are renderer/scale/raster-policy realizations and cannot
replace source tables, coverage, metrics or shaping data across backends and DPI.

### Fall back per Unicode codepoint

Rejected because grapheme clusters, variation sequences and shaping substitutions
must remain intact. Resolution changes only at valid cluster/run boundaries.

### Silently substitute a same-named or nearest face

Rejected because names and approximate attributes cannot prove compatible bytes,
coverage or metrics. Matching and optional synthesis follow the explicit descriptor.
