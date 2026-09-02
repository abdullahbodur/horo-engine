# ADR-064: Audio Asset and Cook Boundary

- **Status**: Proposed
- **Date**: 2026-09-02
- **Supersedes**: None
- **Scope**: Asset Pipeline and Audio ownership for source extraction, domain schemas, cook profiles, cache identity, publication, and runtime media
- **Issue**: [AUD-002.1](https://github.com/abdullahbodur/horo-engine/issues/537)
- **Jira**: [HORO-537](https://horo-engine.atlassian.net/browse/HORO-537)
- **Parent**: [AUD-002](https://github.com/abdullahbodur/horo-engine/issues/536)
- **Related**: [ADR-063](063-audio-sample-format-and-channel-layout.md)
- **Normative documents**: [Asset Pipeline](../architecture/runtime/asset-pipeline.md), [Audio Architecture](../architecture/runtime/audio-architecture.md)

## Context

Asset Pipeline is the generic authority for stable asset identity, bounded import
and cook orchestration, dependency-aware cache keys, staging, atomic publication,
and runtime payload delivery. Audio Architecture currently says that the asset
pipeline owns audio import and cooking while also defining decoder plugins,
metadata, formats, and streaming policy. Without a sharper boundary, Assets may
start interpreting codecs and channel layouts, or Audio may create its own cache,
paths, publication state, and job system.

Audio source files are untrusted media with domain-specific parsing, gapless and
loop semantics, loudness analysis, channel layout, codec constraints, and runtime
stream requirements. Those semantics must remain in Audio while every domain uses
the same generic AST transaction, cache, diagnostics, cancellation, and packaging
contracts.

## Decision

### 1. One generic pipeline, domain-owned audio semantics

AST owns generic orchestration and publication. Audio owns every audio-domain
schema, parser/decoder semantic, cook policy, and runtime media contract. The
dependency direction is:

```text
Assets API / Asset Pipeline contribution contracts
                    ^
                    |
AudioModel / AudioCook contribution
                    ^
                    |
Application/host composition registers the contribution

AudioRuntime -> Assets provider API + AudioModel cooked schemas
```

The generic Assets implementation does not depend on Audio targets. The Audio
cook contribution depends on narrow Assets/Asset Pipeline APIs and is registered
explicitly by the host composition root. Registration descriptors are inert;
they do not scan files, install codecs, start workers, or mutate a global registry.
Editor, CLI, and MCP adapters invoke the same application import/cook operations.

| Responsibility | Authority |
|---|---|
| `AssetId`, `AssetTypeId`, sidecars, project-relative source identity | AST |
| Source file access, trust/root checks, byte/dependency limits, cancellation | AST |
| Audio container probing, parsing, decoding, and semantic validation | Audio |
| AudioClip/AudioStream/variation/sound schemas and versions | Audio |
| Audio import settings and platform cook-profile schema | Audio |
| Generic target/profile selection and contribution invocation | AST/Application |
| Resampling, channel conversion, loudness/waveform/loop/seek analysis | Audio |
| Generic cache-key envelope and dependency/source/tool digesting | AST |
| Canonical audio semantic fingerprint added to that key | Audio |
| Output staging, hashing, manifest transaction, atomic publication, rollback | AST |
| Cooked audio header/payload validation and runtime decode/stream semantics | Audio |
| Package/chunk placement and release artifact ownership | AST/Release |

Neither side duplicates the other's state. An audio artifact has one `AssetId`,
one AST cache/publication record, and one Audio-owned cooked schema. There is no
Audio asset registry, Audio cache directory, or codec-specific AST asset identity.

### 2. Source extraction boundary

AST resolves the tracked source record and admitted dependencies, opens the file
through platform/trust policy, enforces generic source/dependency byte and count
limits, captures immutable bytes or a bounded seekable reader, and invokes the
selected contribution with cancellation and diagnostic context. It does not infer
format from a file extension alone or parse RIFF/Ogg/Opus/MP3/native bank fields.

Audio owns a versioned `AudioSourceExtractor` contribution selected through its
container/codec registry. It performs bounded probing, container parsing, decoder
selection, PCM extraction, and audio-semantic validation. It returns an owned
typed candidate containing, as applicable:

- exact source container/codec identities and decoder/tool versions;
- sample rate, sample representation, semantic channel layout, frame count, and
  duration derived with checked arithmetic;
- loop regions, cue/marker data, encoder delay/padding, and seek information;
- loudness, true-peak, waveform, and analysis provenance;
- bounded warnings/errors with source byte/frame locations;
- discovered domain dependencies using AST-owned stable asset/dependency IDs.

The extractor never chooses project paths, writes sidecars, publishes registry
state, creates cache entries, launches untracked subprocesses, or exposes decoded
buffers to the editor callback. AST owns source-reader lifetime and output sinks;
Audio owns every returned audio type and invariant.

Malformed, truncated, oversized, encrypted/unsupported, contradictory, or
non-finite media fails with typed Audio diagnostics inside the AST operation.
Failure commits no sidecar/editor asset/cooked artifact unless the generic import
transaction explicitly supports a validated metadata-only result.

### 3. Domain schemas and authoring representation

Audio owns separate versioned schemas for `AudioClip`, `AudioStream`,
`AudioVariationContainer`, and extensible sound definitions. Their persisted
authoring data stores stable asset/type IDs and semantic settings, not source
paths, decoder pointers, native formats, runtime voice handles, or AST cache keys.

Schema validation and migration live with AudioModel. AST treats validated domain
payloads as typed contribution values/serialized bytes plus declared dependencies;
it owns the generic envelope, identity, size/hash, and transaction. Unknown major
schema versions, invalid enum values, incompatible layouts, impossible loop/seek
ranges, duplicate variation entries where forbidden, and dependency type mismatch
fail before cook publication.

The source file, editor/authoring representation, cooked target artifact, runtime
stream/decoded state, and live voice are distinct identities and lifetimes. A
reimport may replace authored payload under the same `AssetId`; it does not mutate
a live callback resource in place.

### 4. Cook profiles and target planning

Application policy selects an admitted generic cook target. The Audio contribution
resolves that target plus typed Audio settings into an immutable
`AudioCookPlan`. Audio owns the profile schema and semantics, including:

- resident versus streamed representation and chunk/block policy;
- codec/container and exact encoder/decoder compatibility identity;
- cooked sample rate and ADR-063 channel-layout conversion;
- quality/bitrate/compression mode with bounded accepted values;
- loop, encoder-delay/padding, gapless, seek-table, and preroll policy;
- loudness normalization, peak/headroom, waveform, and analysis policy;
- runtime memory, decode-block, stream-buffer, and platform capability requirements.

Profile names are not hidden bundles of mutable defaults. The resolved plan records
every effective value and provenance. Unsupported target/codec/layout combinations
fail planning; Audio does not silently choose another codec, downmix, truncate
channels, disable gapless behavior, or convert a streamed asset to resident merely
to make cook succeed.

AST schedules the plan through its bounded job/cancellation graph and supplies
owned inputs and host-approved tool execution. Audio performs domain transforms
and writes only through bounded logical output sinks. CPU-heavy decoding,
resampling, analysis, and encoding may use AST-owned jobs but cannot create a
second Audio worker pool or block an owner/callback thread.

### 5. Cache and compatibility identity

AST owns the canonical cache-key algorithm and includes source/dependency digests,
generic target identity, contribution identity/version, settings envelope,
toolchain/build identity, and pipeline schema. Audio supplies one deterministic
`AudioCookFingerprint` as a domain-key component. It includes at least:

- Audio authoring/cooked schema and extractor/cooker versions;
- resolved AudioCookPlan values and provenance;
- container/codec/decoder/encoder/resampler/layout-converter/analysis identities;
- source semantic facts that affect output, including layout, rate, loop,
  delay/padding, and relevant metadata policy;
- ADR-063 processing/layout contract version and runtime decoder ABI/schema;
- deterministic option and numeric-policy versions.

The fingerprint is canonical typed serialization, not JSON map iteration, display
names, timestamps, absolute paths, ambient installed codecs, current audio device,
locale, or callback state. AST hashes it once inside the generic dependency-aware
key. Audio does not hash the same source into a competing cache identity.

An exact cache hit still passes AST envelope/hash validation and Audio cooked-
schema/compatibility validation before publication/use. Native or middleware
runtime acceleration state is not a portable cooked artifact or AST cache hit.

### 6. Output and atomic publication

Audio cook returns a typed logical output set: versioned metadata/header, resident
or stream payload chunks, seek/loop/gapless tables, analysis records, dependency
manifest, requirements, and bounded diagnostics. Output order and canonical
serialization are deterministic. Audio does not select filesystem names or expose
temporary paths as identity.

AST validates declared sizes/hashes/dependencies, writes private staging, builds
the generic generation manifest, and atomically publishes the complete requested
target set. Required chunk/table failure, cancellation, stale source/dependency/
settings revision, or output validation failure discards staging and preserves the
last good generation. Optional outputs may be omitted only when the resolved plan
declared them optional before work began.

Publication emits generic asset revision/availability results. Audio-specific UI,
runtime, and diagnostics query the published typed metadata through application/
Audio services; AST does not interpret LUFS, channel roles, codecs, loop points,
or stream starvation policy.

### 7. Runtime media boundary

Runtime Asset providers resolve the published `AssetId`/type/target artifact and
return owned immutable bytes/leases. AudioRuntime validates the Audio cooked header,
schema, requirements, chunk table, codec identity, layout, sample rate, gapless/
seek data, and integrity before creating resident/stream state. AST never decodes
audio for playback and the callback never asks AST to read a file or fetch a chunk.

Resident decode and streaming I/O/decode happen on bounded Audio/host job paths
outside the callback. Prepared ADR-063 blocks or stream-ring generations reach the
callback through ADR-062 command/lifetime boundaries. Missing/corrupt/incompatible
payload, exhausted budgets, cancellation, reload, and shutdown return typed Audio
results without mutating the last good live generation.

Hot reload is a new AST publication revision followed by Audio-owned prepare and
generation-safe replacement. Old voices/streams retain admitted old payload leases
until their declared retirement; a published asset revision does not retarget a
live callback pointer in place.

### 8. Extension and security boundary

Audio owns the container/codec/analysis contribution registry and semantic IDs.
AST owns generic contribution registration validation, package trust, permission,
resource limits, process execution, cancellation, and output transaction. A codec
extension receives only the narrow Audio extraction/cook context granted by its
descriptor; it cannot access arbitrary project paths, AST internals, callback
memory, or native device handles.

In-process and subprocess tools have pinned identity, license/provenance, bounded
arguments/input/output/time/memory, and no source-derived shell command. Network
fetch, ambient plugin discovery, runtime package installation, and unbounded parser
allocation are forbidden. Package removal/reload cannot unload decoder code while
an import/cook/runtime decoder generation still holds its owner lease.

### 9. Migration and verification

Audio Architecture and Asset Pipeline summarize this decision and do not retain
the ambiguous statement that either subsystem owns the other's semantics. AUD-002.2
through AUD-002.13 implement schemas, sound references, codec registry, import,
cook, runtime decode/stream, gapless metadata, validation, reload, and budgets.
These are scope assignments, not scheduling inferred from issue numbers/milestones.

Required contract coverage includes:

- AST target registration with no Assets-to-Audio target dependency;
- identical GUI/CLI/MCP operation behavior through one application use case;
- WAV/Ogg and extension fixtures for malformed/truncated/oversized metadata,
  hostile counts, unsupported codecs, cancellation, and bounded parser memory;
- deterministic reimport/cook/cache hit across paths, machines, locale, device,
  file timestamps, and contribution registration order;
- cache invalidation for every source/dependency/profile/schema/tool/codec/layout/
  numeric-policy input and no invalidation from presentation-only metadata;
- atomic multi-output publication, stale revision rejection, cancellation/partial-
  output rollback, last-good retention, and package removal;
- runtime resident/stream validation with no source-format dependency or callback
  I/O/decode, plus reload with active old-generation voices;
- dependency/type errors, loop/seek/gapless invariants, layout/rate conversion,
  budget exhaustion, and actionable source/frame diagnostics.

## Consequences

AST remains reusable generic infrastructure and Audio remains the only owner of
media meaning. All hosts share one operation, cache, staging, publication, and
runtime provider path, while codec/layout/gapless/stream policy cannot leak into
Assets. Cache identity is complete without creating a parallel Audio cache.

The cost is a deliberate contribution seam, typed AudioCookPlan/fingerprint/output
schemas, explicit compatibility validation at cook and runtime, and leases across
extension reload/hot-reload. Those costs make failures transactional and keep
callback/runtime behavior independent of source formats.

## Rejected Alternatives

### Put audio parsers, codecs, and profiles in Asset Pipeline

Rejected because generic Assets would depend on one domain's semantics and grow
backend/runtime media policy. Audio contributes those operations through a narrow
generic seam.

### Give Audio its own importer scheduler, cache, and publication tree

Rejected because GUI/CLI/MCP behavior, cancellation, dependency invalidation,
packaging, and atomic publication would diverge from every other asset type.

### Store only opaque cooker settings in string maps

Rejected because cache completeness, migration, validation, and tooling would
depend on ad hoc parsing. Audio owns versioned typed settings and a canonical
fingerprint.

### Choose runtime codec/layout from the active device during cook

Rejected because cook targets must be deterministic/offline and one artifact may
serve different devices. Target/profile policy is explicit; runtime validates it.

### Publish individual stream chunks as they finish

Rejected because readers could observe incompatible metadata/chunk/table versions.
AST publishes one complete generation atomically.

### Let runtime fall back to source files when cooked media is missing

Rejected because packaged playback would depend on authoring codecs, files, tools,
and callback-unsafe I/O. Missing cooked media is a typed failure.
