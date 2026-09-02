# ADR-068: Music Transport and Cross-System Ownership

- **Status**: Proposed
- **Date**: 2026-09-02
- **Supersedes**: None
- **Scope**: Sample-clock music transport, gameplay and narrative decisions, cinematic and animation timing bridges, localized media, captions/subtitles, and save-state boundaries
- **Issue**: [AUD-008.1](https://github.com/abdullahbodur/horo-engine/issues/607)
- **Jira**: [HORO-607](https://horo-engine.atlassian.net/browse/HORO-607)
- **Parent**: [AUD-008](https://github.com/abdullahbodur/horo-engine/issues/606)
- **Related**: [ADR-014](014-sequencer-ownership-clock-authority-and-binding-boundary.md), [ADR-061](061-animation-ownership-update-order-and-clock.md), [ADR-062](062-audio-runtime-ownership-and-update-order.md), [ADR-064](064-audio-asset-and-cook-boundary.md)
- **Normative documents**: [Audio Architecture](../architecture/runtime/audio-architecture.md), [Cinematic Sequencer](../architecture/runtime/cinematic-sequencer-architecture.md), [Animation Architecture](../architecture/runtime/animation-architecture.md), [Accessibility Architecture](../architecture/runtime/accessibility-architecture.md), [Save Game and Persistence](../architecture/runtime/save-game-and-persistence.md)

## Context

Music, cinematics, dialogue, animation events, gameplay, localization, captions,
and saves all need audio timing, but they do not share one authority. The audio
callback is the only place that can start or change rendering at an exact sample.
Gameplay and narrative own why a state changes. Cinematic Sequencer and Animation
own their clocks and directed event traversal. Localization owns locale resolution.
Accessibility/UI owns caption presentation. Save owns coherent durable domain
snapshots, not callback internals.

Without explicit boundaries, Audio can become an accidental narrative state
machine, Sequencer can schedule against render frames, Animation can replay sounds
on presentation sampling, subtitles can disappear when audio is muted, or save
files can capture stale voice handles and device sample positions. Device reset,
pause, seek, scrub, loop, locale change, queue pressure, and reload also need one
generation-safe reconciliation model.

## Decision

### 1. Each domain retains its authority

| Concern | Authority | Audio receives or publishes |
|---|---|---|
| Device/render sample clock, music transport cursor, tempo/marker-to-sample conversion, exact voice/parameter scheduling | Audio control/runtime and callback at their ADR-062 boundaries | Typed admitted commands, sample-boundary acknowledgements, transport observations |
| Gameplay and adaptive-music state choice, combat/exploration/narrative conditions, requested transition intent | Gameplay or an injected game/package music orchestrator | Desired transport/state/section request with timing and late policy |
| Dialogue line choice, narrative progression, semantic interruption and completion meaning | Narrative/dialogue domain | Resolved cue/media request; typed playback observation/result |
| Sequence playhead, track evaluation, seek/scrub/preroll intent, directed event crossing | Cinematic Sequencer under ADR-014 | Generation-tagged schedule/cancel/seek bundle with sequence timing evidence |
| Clip/player cursor and committed animation event occurrence | Animation under ADR-061 | One typed request per committed eligible occurrence |
| Locale, fallback chain, localized content key and media asset resolution | Localization plus application/dialogue adapter | Resolved `AssetId`/cue metadata and locale/content revision, never display strings |
| Caption/subtitle semantics, user settings, localization, layout and presentation | Semantic producer plus Accessibility/UI | Optional timing observation correlated by stable cue ID |
| Save transaction, durable game/narrative state and restore ordering | RuntimeSaveService plus each domain participant | Optional Audio control checkpoint containing stable semantic position only |

Audio does not decide combat music, branch dialogue, advance quests, choose a
locale, author subtitle text, render captions, pause gameplay, evaluate animation,
drive the sequence playhead, or own the save archive. Other domains do not compute
device sample indices, mutate live voices, call the callback, or maintain a second
music render cursor.

### 2. Audio owns one generation-scoped sample transport

Every active output epoch has a monotonic `AudioSampleClock` expressed as checked
integer frame positions at the effective ADR-063 sample rate. Its identity includes
runtime, device/callback epoch, and transport generation. Sample position zero or
the same integer in another epoch is not the same time.

A music transport is an Audio-owned runtime instance with a generation-checked
`AudioTransportId`. It owns the admitted audio definition revision, prepared media
leases, current section/loop and sample cursor, tempo and musical-marker mapping,
scheduled transitions, ramps, and playback acknowledgements. AUD-008.2 defines
the exact tempo/beat/bar representation, but conversion from a musical target to
an output sample boundary remains Audio authority.

The transport is not the gameplay, cinematic, animation, wall, or device policy
clock. Audio control maintains bounded immutable correlation snapshots that map an
accepted producer clock epoch/time to the current sample epoch. A mapping records
source clock identity/generation, source time, sample epoch/frame, rate, uncertainty,
validity interval, and discontinuity revision. The callback consumes only resolved
sample targets; it never queries another clock or estimates wall/simulation time.

Audio observations may report actual admitted/start/stop/marker sample frames and
a control-side mapped source time. They are evidence, not authority to rewrite the
producer's playhead or committed gameplay tick.

### 3. Typed requests separate intent from sample scheduling

Cross-system producers submit owned bounded values through AudioFrontend. A request
contains at least:

- stable producer/session/context identity and generation;
- stable request and semantic occurrence/cue identity;
- target transport/voice/asset identity and expected definition/content revision;
- intent such as prepare, play, stop, pause, seek, section transition, parameter
  ramp, or cancel;
- source clock epoch/time or typed musical quantization/marker target;
- allowed lateness, missed-deadline, pause, discontinuity, and replacement policy;
- cancellation/owner token and bounded completion destination.

Audio control validates identity, content readiness, clock correlation, capacity,
queue horizon, and policy, then converts intent to one or more exact sample targets.
It publishes a complete `ScheduledCommandBatch` to the callback. Commands sharing
one target sample and batch commit atomically in declared stable order. The callback
never calls a producer to ask what should happen next.

Stable occurrence IDs make retries idempotent. Audio control reports duplicate,
stale, already-completed, cancelled, too-late, unmappable, or capacity-exhausted
requests explicitly. It does not infer that two requests are duplicates from asset,
name, or timestamp alone. Queue pressure never silently drops an authoritative
music/dialogue/cinematic transition.

Late policy is selected by the producer/domain contract from an admitted set such
as reject, next buffer, next musical boundary, or explicit immediate-with-ramp.
Audio executes that policy; it does not convert a missed cinematic cue to the next
beat or replay an animation sound without authorization.

### 4. Gameplay and music orchestration own state decisions

Core Audio provides basic prepared music transport: play/pause/stop, intro-loop-
outro sections, exact seek, typed markers, gapless transitions where cooked data
supports them, and sample-safe ramps. It does not decide which state or transition
is appropriate for gameplay.

Gameplay or an optional adaptive-music package owns state machines, vertical stem
selection, intensity, transition rules, randomization, cooldowns, narrative locks,
and product-specific fallback. It reads committed gameplay state at its allowed
tick/service boundary and submits typed desired transitions. Audio resolves their
musical/sample targets and reports acceptance/actual timing. A callback marker
cannot directly mutate gameplay; control publishes a bounded observation consumed
at the next permitted owner boundary.

Gameplay pause remains Runtime/application authority. A transport profile states
whether it follows gameplay pause, follows a cinematic/external master, or uses an
admitted unscaled policy. Audio applies the committed request at a sample boundary
but never sets the scheduler pause state or infers pause from missing game frames.

### 5. Cinematic Sequencer owns sequence time and scrub intent

Cinematic Sequencer owns `SequencePlaybackClock`, track evaluation, event crossing,
loop identity, seek/scrub policy, and preroll intent under ADR-014. An AudioTrack
produces a bounded typed audio schedule bundle using sequence/player/track/keyframe
and traversal identities. It never converts `SequenceTime` directly to a device
sample index or starts a voice.

For continuous playback, the application Audio/Cinematic adapter supplies a current
sequence-to-sample correlation and Audio admits a schedule horizon. Cinematic
events are stable-ordered by Sequencer before submission; Audio preserves batch
order at equal sample targets. Render-frame sampling cannot emit audio commands.

Seek and editor scrub sample sequence state without replaying crossed audio cues by
default. The sequencer requests cancel/hold, prepare/preroll, and exact resume at
the new source epoch/time. Audio invalidates stale scheduled batches, positions
prepared media outside the callback, and acknowledges the new transport baseline.
The sequencer does not advance/resume audible output until its typed policy observes
the required acknowledgement. Unsupported seek/preroll is a typed failure, not a
best-effort play from the old cursor.

When an external media clock is authoritative, Sequencer still owns its external-
clock policy and Audio still owns sample rendering. Epoch jumps and drift beyond
admitted correction reset correlation; neither system silently chases the other by
replaying skipped events.

### 6. Animation owns committed occurrences, not playback

Animation stages event occurrences during fixed-tick evaluation and publishes them
only after the tick commits. The application event adapter maps an eligible typed
occurrence, such as a footstep, to gameplay/audio/VFX requests. Presentation pose
sampling, interpolation, editor scrubbing, a failed tick, or render cadence never
emits audio.

An Audio request retains animation player generation, event ID, traversal/loop
ordinal, direction, committed tick/time, and application mapping revision. Audio
uses that occurrence identity for deduplication and maps the committed timestamp to
its sample clock. Animation does not wait on the callback inside the fixed tick,
and Audio completion cannot retroactively change the committed event cursor.

Reverse playback, seek, loop endpoints, skipped large intervals, and event capacity
follow ADR-061 before Audio sees a request. Audio cannot decide that an ineligible
reverse or crossed event should play merely because its asset is available.

### 7. Localization resolves media before Audio

Narrative/dialogue/gameplay data stores stable localized content or cue keys and
their domain metadata. Localization owns active locale, fallback chain, catalog
revision, and content resolution. An application/dialogue adapter resolves the
selected voice asset, timing/viseme/caption references, and compatibility before
preparing an Audio request.

Audio receives resolved `AssetId`, Audio-owned cooked metadata, stable cue ID, and
locale/content revisions needed to reject stale work. It never reads translation
tables, selects language, falls back between voice lines, formats text, or stores
localized display strings. Missing localized voice may resolve to a product-
declared silent/text-only result upstream; Audio does not substitute another locale.

A runtime locale change does not retarget a live voice in place. Narrative/product
policy decides continue, stop, or prepare-and-replace. Audio applies the resulting
generation-safe request with owned old/new media leases.

### 8. Captions and subtitles are independent semantic presentation

Dialogue/gameplay owns the stable cue ID, content key, speaker/context metadata,
and intended simulation/timeline timing. It fans one semantic cue independently to
Audio and the bounded caption transport. Muting a bus, virtualizing/rejecting a
voice, missing hardware/provider/media, or Audio failure cannot suppress a required
caption or subtitle.

Accessibility/UI owns user caption settings, localization, layout, safe areas,
speaker association, reading duration, and display lifecycle. Audio may publish
actual start/stop/marker observations keyed by the existing cue ID. The presenter
may reconcile within its declared policy but does not create a second caption for
that observation, expose device sample time, or make audio success the semantic
trigger. Audio callbacks never format/localize text or call UI/accessibility code.

Genuinely audio-only environmental cues may originate from Audio metadata through
the existing bounded `AudioEventSnapshot` path, but they use stable semantic IDs
and remain separate from narrative dialogue ownership.

### 9. Save files store semantic resume state, never live Audio state

RuntimeSaveService coordinates a safe-point snapshot from participating owners.
Gameplay/narrative/adaptive-music domains decide which durable state belongs to a
slot: desired music state, definition/section identity, narrative cue progress,
and game-specific transition variables. Account/project audio volume, accessibility
settings, active locale preference, and device/backend selection remain their
independent configuration domains.

When exact music resume is supported, Audio control may provide an owned checkpoint
at the save barrier containing only stable definition/content revisions, section or
marker identity, rational musical position or validated content frame, loop ordinal
where semantically required, and declared resume policy. It contains no voice/bus/
stream/provider handles, callback/device epoch, raw output sample index, decoder
pointer, ring buffer, scheduled queue, DSP state, native handle, or owner pointer.

Restore first validates/migrates durable gameplay/narrative state and resolves the
current localized/cooked assets. Audio prepares a new transport generation, seeks
or reconstructs the semantic position, and acknowledges readiness before the host
publishes restored playback. Missing/incompatible content, unsupported exact seek,
or changed definition follows the saved game policy explicitly; it never reuses a
stale sample cursor. Transient SFX, ordinary animation occurrences, and live voice
allocation are not restored by default.

### 10. Discontinuities and failures reconcile by generation

Pause/resume, host suspension, device/backend epoch change, transport seek, sequence
scrub, locale/content replacement, scene unload, definition reload, and save restore
create or invalidate named generations. Every queued command and observation names
the transport/sample/content/producer generations it was built against. Audio
rejects stale commands and control drains or cancels them before owner leases retire.

Device loss never lets an old sample frame be interpreted in the new epoch. The
transport's declared discontinuity policy chooses hold semantic position, advance
silently against an admitted external clock, restart a section, or fail. Audio
executes the selected policy after device/media preflight; it does not invent one.

Callback completion/fault/marker records are bounded and allocation-free. Audio
control validates and deduplicates them, updates transport state, and publishes
typed observations. Narrative, gameplay, cinematic, animation, caption, and save
consumers process those observations only at their own allowed boundaries. No
cross-system callback or DataBus broadcast establishes sample/tick ordering.

### 11. Migration and verification

Audio Architecture gains one explicit cross-system transport section. Cinematic,
Animation, Accessibility, and Save documents project the same boundary and do not
create alternate sample schedulers or Audio-owned presentation state. Existing
`MusicPlayer` remains a preset/client of transport, not a second playback system.

AUD-008.2 and later children define tempo/marker math, basic music transport,
cinematic scheduling/scrub, dialogue playback, animation/gameplay bridges,
qualification, and save integration. Adaptive music remains a higher-level client
under AUD-015, not hidden in Audio core.

Required contract coverage includes:

- sample targets independent of render cadence with epoch/rate checked arithmetic;
- stable equal-time batch order, retry deduplication, cancellation, queue saturation,
  late/unmappable policy, and no partial authoritative transition;
- gameplay state changes and pause ownership without callback-driven decisions;
- cinematic play/loop/pause/seek/scrub/preroll/external discontinuity with stale
  schedule cancellation and acknowledgement-gated resume;
- committed Animation occurrence mapping, duplicate/skipped/reverse/loop endpoint
  behavior, and no presentation-sampling audio emission;
- locale fallback/missing voice/change during playback with no Audio localization
  or in-place live asset retarget;
- captions under mute, virtualization, missing media/device, Audio failure, timing
  observation, duplicate cue, locale change, and bounded queue pressure;
- semantic save/restore with changed content/definition, unsupported seek, partial
  prepare failure, last-good runtime retention, and no serialized live handles;
- device reset, host suspend, scene unload, hot reload, producer/transport/content
  generation mismatch, late callback records, and lease-safe shutdown.

## Consequences

Audio remains the only sample scheduler and transport renderer while every other
domain keeps its semantic, clock, localization, presentation, and persistence
authority. Music and cinematic audio can be sample accurate without making device
time the simulation clock. Captions remain available independently of sound, and
saves remain portable across devices and runtime epochs.

The cost is explicit clock correlation, typed request/observation identities,
generation-aware reconciliation, domain adapters, acknowledgement handling, and a
semantic checkpoint format. Cross-system integration cannot rely on direct calls or
casual float timestamps.

## Rejected Alternatives

### Make Audio own gameplay or adaptive-music state

Rejected because Audio would need game/narrative rules and become a second gameplay
authority. It renders typed desired transitions and reports timing.

### Let Sequencer or Animation compute device sample indices

Rejected because they do not own device/sample epochs, reset, queue horizon, or
callback timing. They submit source-clock evidence and Audio maps it.

### Trigger sounds from presentation sampling

Rejected because render cadence, interpolation, scrubbing, and repeated sampling
would duplicate or skip authoritative occurrences.

### Drive captions only from actual audio playback

Rejected because mute, virtualization, missing media/device, or Audio failure would
remove required semantic accessibility presentation.

### Resolve locale and fallback inside Audio

Rejected because translation/catalog policy and narrative product behavior do not
belong to the callback or Audio runtime. Audio consumes resolved assets.

### Serialize live voice and device sample state

Rejected because handles, epochs, queues, decoders, and buffers are process/device
state and cannot survive restore, migration, backend change, or content revision.

### Broadcast timing through EngineDataBus

Rejected because asynchronous broad events do not establish sample/tick ordering
and violate the narrow caption and high-frequency transport boundaries.
