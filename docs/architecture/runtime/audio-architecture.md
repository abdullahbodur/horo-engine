# Audio Architecture

## Purpose

This document defines audio asset loading, device ownership, mixer and voice
models, scene integration, streaming, real-time thread rules, lifecycle,
editor tooling, middleware boundaries, and observability.

## Core Decisions

- Engine audio APIs are backend-neutral.
- One audio runtime owns the active device, mixer graph, voices, and real-time
  command processing.
- The audio callback performs no heap allocation, blocking I/O, logging, or
  contended locking.
- Game and scene code use typed handles and enqueue commands.
- Decoding and streaming I/O occur outside the real-time thread.
- Audio assets use the common asset identity, cooking, and cache contracts.
- Headless hosts may omit audio or use a null backend.
- Core audio targets a practical Unity/Godot-level foundation: sources,
  listeners, buses, 2D/3D playback, streaming, basic effects, and profiling.
- Procedural audio graphs, advanced spatial audio, and middleware integrations
  are extension/package boundaries unless explicitly promoted to core later.

## Layer Model

```text
Scene / Gameplay
      |
      v
Audio Frontend
  sources, listeners, buses, commands
      |
      v
Real-Time Mixer
      |
      v
Platform Audio Backend / Null Backend
```

Backend device APIs and native handles remain private to the backend.

## Runtime Ownership

```text
AudioRuntime
  +-- AudioDevice
  +-- MixerGraph
  +-- VoiceRegistry
  +-- StreamBuffers
  +-- CommandQueue
  +-- EventQueue
```

The process host owns `AudioRuntime`. A game runtime may create one scene audio
context per active scene or listener policy.

## Handles

```cpp
using AudioClipHandle = Handle<AudioClipTag>;
using AudioVoiceHandle = Handle<AudioVoiceTag>;
using AudioBusHandle = Handle<AudioBusTag>;
```

Handles are generation checked. Asset IDs remain the persistent identity for
clips and streams.

## Audio Assets

The asset pipeline owns source import and platform cooking for:

- decoded short clips
- streamed music or ambience
- channel layout and sample-rate metadata
- loop points
- loudness and normalization metadata
- per-platform compression, quality, and streaming policy
- duration
- original and cooked sample rate
- bit depth and sample format
- channel count
- seek tables for streamed or compressed audio
- encoder delay and padding for gapless playback
- loop start/end expressed in sample frames

Runtime loading validates cooked format and backend capability. Source files are
not decoded synchronously inside the audio callback.

Core asset support includes `wav` and `ogg` source audio. Additional formats may
be provided by packages or platform-specific importers, but scenes and packages
reference the cooked `AudioClip` identity, not decoder-specific files.

Loop metadata is sample-frame based, not millisecond based. Music loops and long
ambience must preserve encoder delay and padding information so packaged builds
can perform gapless looping where the backend supports it.

Dialogue and localized voice clips are resolved before they reach the audio
runtime. Locale-aware asset resolution belongs to the asset/configuration
pipeline; audio components and commands receive the resolved `AssetId` for the
selected locale.

### Audio Variation Container

A single `AudioSourceComponent` may reference either a single `AudioClip` or an
`AudioVariationContainer`. The container owns multiple clip references and a
selection policy so that repeated one-shot playback does not repeat the exact
same asset:

```cpp
enum class VariationSelection {
    Random,
    RoundRobin,
    Shuffle,
    WeightedRandom
};

struct AudioVariationContainer {
    std::vector<AssetId> clips;
    VariationSelection selection;
    float minPitchDelta;
    float maxPitchDelta;
    float minGainDeltaDb;
    float maxGainDeltaDb;
    uint64_t deterministicSeed;
};
```

The runtime resolves the container to a concrete clip at voice creation time,
before the clip enters the audio callback. Variation containers are small assets
that live next to `AudioClip` assets and are authored through the same import
pipeline.

### Loudness And Metering Metadata

Cooked audio assets carry loudness metadata for mixing, normalization, and
platform compliance:

```text
- integrated LUFS
- short-term LUFS (optional)
- true peak dBTP
- RMS
- normalization gain
- broadcast compliance tag (optional)
```

The mixer may use this data to auto-level sources or to report loudness to
platform certification tooling. Loudness values are computed during import/cook,
not on the real-time path.

### Decoder Plugin Interface

Codec support is extensible through the asset cooking pipeline, not by adding
decoder-specific branches to runtime playback.

```cpp
class IAudioDecoder {
public:
    virtual bool CanDecode(AudioFormatTag format) const = 0;
    virtual DecodeResult Decode(std::span<const std::byte> source,
                                AudioDecodeOutput& output) = 0;
};
```

The decoder registry is used during import/cook. Runtime loaders consume cooked
audio payloads and validated stream metadata. Packages may add Opus, ADPCM,
MP3, platform-native codecs, or middleware bank decoders by registering decoder
plugins without changing runtime scene code.

## Audio Components

### AudioSourceComponent

`AudioSourceComponent` is the core scene emitter component. It describes a
playback source without exposing backend voice handles:

```cpp
enum class AudioSourceKind : uint8_t {
    NativeClip,
    MiddlewareEvent
};

struct AudioSourceComponent {
    AudioSourceKind kind;           // NativeClip or MiddlewareEvent
    AssetId sound;                  // valid when kind == NativeClip;
                                    // may be an AudioClip or AudioVariationContainer
    StableEventId event;            // valid when kind == MiddlewareEvent
    AudioBusId bus;
    AudioPlaybackMode playbackMode; // one-shot, loop, streamed
    AudioSpatialMode spatialMode;   // 2D or 3D
    float gain;
    float pitch;
    bool enableDoppler;
    bool playOnStart;
    AudioPriority priority;
    AttenuationDescriptor attenuation;
};
```

Runtime playback creates voices through the audio frontend. Persistent scene
data never stores `AudioVoiceHandle`. Native clip sources resolve `sound` to a
cooked `AudioClip` payload, either directly or by expanding an
`AudioVariationContainer`. Middleware event sources resolve through the
middleware event bridge described below.

In **event middleware** mode the native Horo mixer remains active. The bridge
must reserve a proxy slot in `VoiceRegistry` for each middleware event so that
core voice limits and virtualization policy apply to the combined native +
middleware voice budget. `MiddlewareVoiceHandle` is owned by the middleware
bridge; the proxy slot carries the same priority, bus, and virtualization
metadata used by native voices. If the bridge cannot create a proxy slot, the
event is rejected according to the same policy as a native voice. Backend
replacement mode may bypass the proxy slot because the middleware owns the
entire voice and mixer model.

### AudioListenerComponent

`AudioListenerComponent` defines the listener transform used for 3D audio,
usually attached to the active camera. A scene may contain multiple authored
listeners, but the runtime listener policy selects the active listener
deterministically.

Common policy:

```text
Camera
  [x] Main Audio Listener
```

If no active listener exists, 3D sources either fail validation or play through a
documented fallback policy. They must not silently use an arbitrary camera.

## Mixer Graph

The mixer graph contains named buses:

```text
Master
  +-- Music
  +-- SFX
  +-- UI
  +-- Voice
  +-- Ambient
```

The core bus/category set is:

| Bus | Purpose |
|---|---|
| Master | Final output control. |
| Music | Music and adaptive music stems. |
| SFX | Gameplay sound effects. |
| UI | Runtime game UI sounds. |
| Voice | Dialogue and voice lines. |
| Ambient | Long-running ambience and environmental beds. |

Buses support typed gain, mute, pause, routing, and effect descriptors. Graph
changes are prepared off-thread and committed through bounded real-time
commands.

Each bus may use the default stereo panner or a custom spatializer slot:

```cpp
class IAudioSpatializer {
public:
    virtual void ProcessSpatial(const VoiceSpatialInput& input,
                                VoiceSpatialOutput& output) = 0;
};
```

The core provides a deterministic stereo panner. Packages may register HRTF,
ambisonics, platform-native, or middleware spatializers. Spatializer processing
must consume prevalidated per-voice spatial input and must not query scene or
physics state from the real-time thread.

A spatializer computes per-voice 3D positioning, such as panning, HRTF, or
object-based spatial rendering. DSP nodes process bus-level signals such as
filters, reverb sends, or ambisonics decoding. A spatializer that needs bus-level
processing may internally use DSP nodes, but its public contract to the mixer is
per-voice positioning.

If a registered spatializer fails validation or is unavailable on the active
backend, the bus falls back to the default stereo panner. The fallback is
observable through metrics and must not reject otherwise valid voices.

User volume settings are configuration values. Per-frame GUI controls do not
directly edit backend state.

The editor may expose mute, solo, volume, routing, meters, and preview controls.
Only gain, mute, pause, routing, and effect changes are runtime mixer state.
`Solo`, meter visibility, and preview overrides are editor-session/debug state
and must not be serialized as packaged-game mixer behavior unless an explicit
debug profile requests it. Runtime-affecting controls submit mixer graph changes
through the audio frontend; they do not edit backend state directly.

## Effects And DSP Boundary

Core effects are intentionally small and deterministic:

- gain
- low-pass filter
- high-pass filter
- reverb send

Core effects and custom submix effects use the same DSP node contract:

```cpp
class IAudioDSPNode {
public:
    virtual Result<void> Prepare(const AudioDSPContext& context) = 0;
    virtual void Process(AudioBuffer& buffer) = 0;
};
```

`Prepare()` runs outside the real-time callback and may validate descriptors,
allocate fixed buffers, and build immutable processing state. `Process()` runs
on the real-time path and must not allocate, block, log, access files, or invoke
unbounded user callbacks. Nodes that do not declare this contract at graph
build time are rejected by the mixer graph validator.

Graph validation runs whenever a mixer graph is built or modified:
- in the editor when a bus graph or effect chain is authored
- at scene load time
- when a package hot-reloads an effect node

Validation is a contract check: the node must advertise whether it allocates,
blocks, or calls user code during `Process()`, and the validator rejects nodes
that do not match the real-time-safe contract. Static verification is preferred
where possible, but the minimum requirement is explicit declaration and
runtime rejection before the node reaches the audio callback.

Advanced effects such as convolution reverb, granular synthesis, procedural
node audio, platform-specific spatial audio, and middleware DSP are package or
extension features. The real-time callback may execute prevalidated DSP state,
but it must not allocate, block, or invoke unbounded user code.

A future MetaSounds-style procedural audio graph requires its own architecture
document. The DSP node contract exists now so bus/submix effects can be extended
without redesigning the mixer.

### Bus Sidechain And Auto-Ducking

Bus-to-bus automatic ducking (for example, dialogue ducking music when a voice
line starts) is not part of the core mixer. The snapshot system supports
manually triggered ducking through gameplay code. Automatic sidechain ducking
is provided as a package/extension DSP node:

```cpp
struct AudioSidechainDescriptor {
    AudioBusId sourceBus;     // bus whose level is measured
    AudioBusId targetBus;     // bus whose gain is modulated
    float thresholdDb;
    float attackMs;
    float releaseMs;
    float duckAmountDb;
};

class IAudioSidechainDuckNode : public IAudioDSPNode {
public:
    virtual void Configure(const AudioSidechainDescriptor& desc) = 0;
};
```

The node reads a buffered RMS/peak measurement from the source bus, computes a
control signal off-thread, and applies gain reduction to the target bus on the
real-time path. Core ships no built-in sidechain; packages provide the node when
a project needs broadcast-style auto-ducking.

## Voice Model

A voice represents one active playback instance with:

- clip or stream handle
- playback state and cursor
- gain and pitch
- loop policy
- spatial parameters
- bus destination
- priority and virtualization policy

Voice limits are explicit. When capacity is reached, the frontend applies the
configured reject, replace, or virtualize policy deterministically.

Voice priority and virtualization are core features. A low-priority distant SFX
may be virtualized or rejected before a high-priority UI or voice line. Policy
decisions are observable through metrics.

## 2D And 3D Playback

Audio sources support two spatial modes:

| Mode | Behavior |
|---|---|
| 2D | No spatial attenuation; routed directly to the selected bus. |
| 3D | Uses source/listener transforms, distance attenuation, and panning. |

Core 3D audio provides:

- distance attenuation
- stereo or backend-supported panning
- optional per-source doppler pitch shift
- configurable min/max distance and rolloff curve

Scene extraction provides either explicit velocity or previous/current
transforms for sources and listeners. The audio runtime does not query physics
state directly from the real-time path.

Doppler is core because the required source/listener velocity data already
exists in scene extraction and the calculation is deterministic. It is controlled
by `AudioSourceComponent::enableDoppler` so projects that do not need it pay no
per-source policy cost. Occlusion, obstruction, diffraction, HRTF, and
platform-specific spatial audio remain extension points.

### Occlusion Provider Interface

Occlusion, obstruction, and diffraction are not computed by the audio runtime.
Packages may provide an occlusion service that runs during scene extraction or a
non-real-time audio update phase:

```cpp
struct AudioOcclusionQuery {
    Vec3 sourcePos;
    Vec3 listenerPos;
    AudioOcclusionHandle outHandle;
};

class IAudioOcclusionProvider {
public:
    virtual void RequestOcclusion(std::span<AudioOcclusionQuery> queries) = 0;
};
```

The core ships a null provider. A physics-backed provider may raycast or perform
portal/room queries outside the real-time callback, then feed the result back at
most one game frame later as ramped gain, filter, or send parameters. Missing a
deadline is a provider bug: the runtime applies the last known valid result
ramped toward a documented safe fallback and emits a staleness metric. The audio
callback never performs raycasts or scene queries.

Observable occlusion/zone metrics:

```text
audio.occlusion.stale_value_age_ms     // age of the last applied occlusion result
audio.occlusion.deadline_miss_count    // deadline misses since startup
audio.occlusion.active_query_count     // queries pending off-thread
```

## Parameter Automation, Fades, And Snapshots

Audio parameter changes may be immediate, timestamped, or ramped. Gain, pitch,
filter cutoff, send level, and bus gain changes should use buffer-boundary-safe
or sample-safe ramps to avoid clicks and pops.

```text
SetBusGain(Music, -6 dB, fade = 250 ms)
SetVoicePitch(voice, 0.8, fade = 100 ms)
SetLowPassCutoff(SFX, 1200 Hz, fade = 500 ms)
```

The core supports simple audio snapshots as named mixer-state presets:

```text
pause_menu
underwater
combat
dialogue_focus
```

Snapshots apply bounded parameter ramps to buses and sends. Example:

```text
Dialogue starts
  -> Music -6 dB over 250 ms
  -> SFX   -3 dB over 250 ms
  -> Voice unchanged
```

Advanced adaptive music and procedural modulation remain package or extension
features. The core snapshot system is for predictable ducking and mix-state
transitions.

Adaptive music systems are higher-level orchestrators over this command and
snapshot model. They may run state machines, vertical remixing, stem crossfades,
or transition rules in gameplay/package code, then submit scheduled audio
commands through the normal command queue. They do not require a separate
real-time path.

## Audio Zones

`AudioZoneComponent` is a later engine feature, not required for the initial
core. When added, it owns area-based environment sends such as reverb routing,
low-pass regions, or bus overrides. Until then, reverb send is controlled by
source/bus parameters.

Audio zones should integrate through the same non-real-time query/update model
as occlusion. Scene extraction determines source/listener relation to zones,
then applies ramped send, filter, or routing changes through normal audio
commands. Zone intersection tests must not run inside the audio callback. Like
occlusion, zone-driven parameter changes should be applied at most one game
frame after the query, unless the provider documents a longer latency policy.

## Scene Integration

Audio source and listener components contain backend-neutral state. During
runtime update, the scene extracts an audio snapshot or commands:

- source transform and velocity
- listener transform
- playback requests
- parameter changes
- stopped or destroyed sources

Authoring documents never store live voice handles.

Editor create menus may expose:

```text
Create > Audio > Audio Source
Create > Audio > Ambient Sound
Create > Audio > Music Player
Create > Audio > Audio Listener
Create > Audio > Reverb Zone   (shown only when audio zones are enabled)
Create > Audio > Audio Bus / Mixer Asset
```

`Audio Listener` is normally created through the camera inspector rather than as
a free-floating object, but the component remains a first-class runtime type.

`Ambient Sound` and `Music Player` are presets over `AudioSourceComponent`, bus
selection, and streaming/loop policy. They are not separate playback systems.

`Reverb Zone` appears only when `AudioZoneComponent` is implemented or when an
audio-zones package/feature is enabled. It must not be shown as an available
create action before the feature can create valid runtime data.

## Real-Time Command Queue

Audio commands may originate from gameplay systems, animation events, timeline
playback, editor preview, asset hot reload, streaming services, scene unload, or
device lifecycle services. These producers do not write directly into the audio
callback's real-time command buffer.

Command flow:

```text
Gameplay / editor / asset / lifecycle producers
      |
      v
MPSC AudioCommandQueue
      |
      v
AudioFrontend drains, validates, normalizes, and coalesces commands
      |
      v
SPSC real-time command buffer
      |
      v
Audio callback consumes bounded prevalidated commands
```

Accepted command kinds include:

```text
CreateVoice
StartVoice
StopVoice
SetVoiceParameters
SwapMixerGraph
ReleaseResource
ScheduledCommandBatch
```

Queue-full behavior is explicit. Critical stop/release commands have reserved
capacity or a separate safe path. The frontend may merge redundant parameter
changes before they enter the real-time buffer, but it must not reorder commands
across documented lifecycle boundaries such as scene unload or device reset.

Scheduled commands carry an audio timeline timestamp or buffer-boundary target.
Commands in the same `ScheduledCommandBatch` with the same timestamp are applied
atomically at the same buffer boundary. Timeline systems and animation-event
systems use this to start multiple voices in sync without adding a separate
real-time API.

The audio thread publishes bounded completion and device events through a
lock-free or wait-free queue consumed by the owning main-thread service.

## Streaming

Streaming uses worker or I/O jobs to fill preallocated ring buffers. The audio
callback consumes available frames without waiting.

Underrun behavior:

- output silence for missing frames
- increment an underrun metric
- emit one rate-limited diagnostic outside the real-time callback
- continue or stop the stream according to policy

Streaming buffers have per-stream and global memory budgets.

Music and long ambience default to streaming. Short SFX and UI sounds default to
decoded resident clips. Per-platform cook settings may override compression,
sample rate, channel layout, and streaming thresholds.

## Editor Tooling

HoroEditor provides audio authoring tools:

- audio source inspector and preview playback
- listener selection and validation
- mixer/bus panel with runtime gain, mute, pause, routing controls and
  editor-only solo/meters/preview overrides
- stream/cook settings preview
- active voice and bus meter view
- asset import diagnostics for sample rate, channels, loops, and loudness
- scene validation for missing listeners, missing clips, invalid buses, and
  unsupported spatial settings
- Project Settings > Audio > Focus Behavior and per-platform profile UI

Editor preview uses the same audio frontend contracts as runtime playback. It
does not bypass real-time thread rules.

## Device Lifecycle

Device states:

```text
Unavailable -> Opening -> Active -> Reconfiguring -> Active
Active -> Lost -> Opening
Active -> Closing -> Unavailable
```

Device loss does not invalidate frontend asset identity. The runtime may
recreate device-owned resources and resume voices according to explicit policy.

Sample-rate, channel-layout, and buffer-size changes are committed at a safe
boundary.

## Threading Rules

The real-time callback cannot:

- allocate or free general heap memory
- acquire contended mutexes
- access files or network
- call ordinary logging
- query GUI, ECS, or configuration services
- wait for jobs
- execute user-provided unbounded callbacks

It consumes prevalidated state and writes only to real-time-owned memory and
bounded event queues.

When a middleware backend replaces the native device and mixer, Horo's
threading rules apply to `IAudioMiddlewareBackend::Submit()` and any Horo-side
processing. The middleware's own real-time thread is governed by its own
contract, which the integration must document and must not violate Horo's
ownership or observability invariants.

## Null Backend

The null backend validates resources, commands, handles, and lifecycle without
opening a device. It may advance deterministic playback clocks for tests.

## Middleware And Extension Boundary

Core provides:

- AudioSource / AudioListener
- AudioClip asset identity
- 2D and basic 3D playback
- mixer buses and categories
- basic DSP effects
- distance attenuation
- optional per-source doppler
- voice priority and concurrency policy
- streaming for music and ambience
- audio profiler metrics
- audio import and cook integration
- decoder plugin registry for import/cook
- DSP node and spatializer extension slots

Packages or extensions may provide:

- Wwise integration
- FMOD integration
- adaptive music systems
- procedural audio graphs
- advanced spatial audio
- occlusion, diffraction, and propagation
- convolution reverb
- dialogue and lip-sync systems
- text-to-speech or speech recognition

Middleware integration has two supported shapes:

```text
Backend replacement:
  Horo AudioFrontend -> middleware backend/device/mixer

Event middleware:
  Horo scene component -> Wwise/FMOD event trigger
  Horo mixer remains available for core audio
```

Backend replacement delegates device and mixer work to middleware while keeping
Horo's scene, asset identity, lifecycle, and observability contracts. Event
middleware maps scene events and components to middleware-authored events while
the native Horo mixer may still own basic audio. Both models must respect Horo's
real-time thread rules and must not bypass project trust or package lifecycle
policy.

### Middleware Backend Contract

Middleware integrations use explicit extension points rather than replacing
scene/audio ownership with ad hoc plugin calls.

```cpp
class IAudioMiddlewareBackend {
public:
    virtual Result<void> Initialize(const AudioBackendConfig& config) = 0;
    virtual Result<void> Submit(const AudioFrontendFrame& frame) = 0;
    virtual AudioMiddlewareStats QueryStats() const = 0;
    virtual void Shutdown() = 0;
};

class IAudioMiddlewareEventBridge {
public:
    virtual Result<MiddlewareVoiceHandle>
    PostEvent(StableEventId event,
              const AudioEmitterContext& emitter,
              const AudioCommandTiming& timing) = 0;

    virtual Result<void> StopEvent(MiddlewareVoiceHandle voice,
                                   AudioFadeDescriptor fade) = 0;
};

class IAudioParameterBridge {
public:
    virtual Result<void> SetParameter(AudioParameterTarget target,
                                      StableParameterId id,
                                      AudioParameterValue value,
                                      AudioCommandTiming timing) = 0;
};
```

Backend replacement may bypass Horo's native `MixerGraph` and `VoiceRegistry`,
but it must still consume normalized frontend frames and publish bounded stats.
Event middleware uses `AudioSourceKind::MiddlewareEvent` and routes stable event
IDs, emitter transforms, and parameters through the same command staging path as
native clip playback.

Parameter bridge IDs are stable authoring/runtime IDs. They are registered in
the audio parameter registry during project cook or package build so that
middleware-specific RTPC/FMOD-style parameters are resolved to numeric IDs
before entering the real-time command buffer. Runtime code never maps strings
to parameter IDs inside the audio callback.

```cpp
struct AudioParameterRegistry {
    StableParameterId Register(StableParameterName name);
    std::optional<StableParameterId> Find(StableParameterName name) const;
};
```

Middleware event IDs follow the same rule. Event names are registered in the
audio event registry at cook/build time and resolved to stable numeric event IDs
before runtime. The bridge receives `StableEventId`, not raw strings.

```cpp
struct AudioEventRegistry {
    StableEventId Register(StableEventName name);
    std::optional<StableEventId> Find(StableEventName name) const;
};
```

Event string-to-ID lookup is forbidden on the audio thread. Missing event IDs
are reported through metrics and diagnostics, not through runtime logs.

The middleware backend participates in the device lifecycle. `Initialize` and
`Shutdown` are invoked from the same state machine that manages native device
transitions. Device loss, reopen, and reconfigure events are reported by the
middleware backend so that Horo's `AudioRuntime` remains in a typed state even
when the middleware owns the device.

## Platform Focus And Suspension Policy

Audio focus behavior is explicit per host/profile:

```text
On focus lost:
  - continue audio
  - pause all buses
  - pause gameplay buses but keep music
  - mute output
```

Editor preview, play-in-editor, and packaged games may use different defaults,
but each policy is typed configuration. Focus loss must not silently leave the
audio runtime in an undocumented state.

This policy is configured in Project Settings > Audio > Focus Behavior, with
per-platform profiles for editor preview and packaged games. The same page
defaults mute-on-minimize on most desktop hosts, pause-gameplay-buses for
mobile focus loss, and continue-everything for dedicated audio preview windows.

## Metrics

Audio exposes:

- callback duration and budget utilization
- underrun count
- command/event queue depth and drops
- active, virtualized, and rejected voice counts
- stream buffer fill
- decoded and resident audio bytes
- device sample rate and buffer size
- per-bus peak/RMS levels where supported
- mixer CPU time
- effect CPU time
- rejected voice count by reason
- spatializer fallback count and reason
- occlusion/zone staleness age and deadline misses
- parameter lookup failures (missing stable ID)
- event lookup failures (missing stable event ID)
- variation container selection telemetry (optional)

No ordinary log formatting occurs on the callback thread.

## Testing

Required tests cover:

- voice and resource handle generations
- mixer routing and gain behavior
- voice capacity policies
- real-time queue saturation
- streaming underrun recovery
- device loss and reopen
- scene unload with active voices
- null backend deterministic clock
- callback path allocation and lock checks
- audio asset format validation
- 2D vs 3D source behavior
- distance attenuation policy
- listener selection and missing-listener diagnostics
- default bus/category routing
- editor preview command path
- mixer mute/solo/volume controls
- middleware extension boundary does not bypass real-time thread rules
- command staging from MPSC queue to SPSC real-time buffer
- parameter automation and fade ramp behavior
- audio snapshot/ducking transitions
- gapless loop metadata preservation
- platform focus/suspension policy
- decoder plugin registration and cook-time selection
- DSP node prepare/process real-time contract
- spatializer registration and fallback behavior
- occlusion provider one-frame-late ramp behavior
- scheduled batch atomicity for timeline and animation events
- middleware event and parameter bridge validation
- locale-aware clip resolution happens before audio runtime
- middleware device lifecycle state propagation
- stable parameter ID authoring-time registry resolution
- stable event ID authoring-time registry resolution
- variation container resolves to concrete clip at voice creation
- sidechain duck node configuration and gain reduction
- occlusion provider deadline miss produces stale-value fallback and metric
- spatializer fallback emits observable metric
- parameter/event lookup failures emit observable metric

## Editor UI Wireframes

Audio editor surfaces must include an ASCII wireframe or equivalent design note
in this architecture document or in a feature-specific UI plan before
implementation begins. This applies to audio modals, pages, panels, tabs,
inspectors, and tool windows.

The wireframe is not decorative. It defines the expected layout, grouping,
alignment, primary actions, secondary actions, scroll regions, and empty/error
states before code is written.

Required wireframe coverage:

| Surface | UI Placement | Access Pattern |
|---|---|---|
| Audio Source component | Inspector | Select object with AudioSourceComponent |
| Audio Listener / Camera integration | Inspector | Select camera or listener object |
| Audio Mixer / Bus panel | Persistent dockable panel/tab | Menu: Window > Audio > Mixer |
| Audio asset import/cook settings | Inspector + Project Settings | Select AudioClip asset or Project Settings > Audio > Import |
| Audio preview | Inline inspector panel | Select clip/asset, click Preview |
| Active Voices / Profiler | Profiler tab or dedicated Audio Profiler panel | Menu: Window > Audio > Profiler |
| Missing clip/listener/bus diagnostics | Diagnostics/Validation panel + inline inspector warnings | Scene save/validation or build |
| Project Settings > Audio > Focus Behavior | Project Settings page | Edit > Project Settings > Audio |
| Per-platform audio profile | Project Settings page | Edit > Project Settings > Audio > Platforms |

Modal usage for audio is restricted to:

- import conflict resolution
- missing dependency repair wizard
- destructive mixer/bus reset confirmation
- delete bus with active references confirmation
- unsaved mixer changes prompt

Persistent audio tools (mixer, profiler, bus meters) should not be modals.
They must remain visible while the user edits the scene so audio state can be
compared against scene state.

Example mixer panel wireframe:

```text
+--------------------------------------------------------------+
| Audio Mixer                                           [Reset] |
+----------------------+---------------------------------------+
| Buses                | Selected Bus: SFX                     |
|                      |---------------------------------------|
| > Master             | Volume     [-----------o-----]  -6 dB |
|   Music              | [ ] Mute   [ ] Solo                   |
|   SFX                | Route      Master v                   |
|   UI                 | Effects                               |
|   Voice              |   + Gain                              |
|   Ambient            |   + Low Pass                          |
|                      |                                       |
+----------------------+---------------------------------------+
|Output: 48 kHz / 512 frames | Voices: 24 active, 3 virtualized|
+--------------------------------------------------------------+
```

Example audio source inspector wireframe:

```text
+--------------------------------------------------------------+
| Audio Source                                                 |
+--------------------------------------------------------------+
| Clip        [ explosion_01.ogg                         ... ] |
| Bus         [ SFX                                      v   ] |
| Mode        (x) One Shot  ( ) Loop  ( ) Stream               |
| Spatial     ( ) 2D        (x) 3D                             |
| Volume      [-----------o-----] 0.80                         |
| Pitch       [------o----------] 1.00                         |
| Priority    [ Normal                                   v   ] |
|                                                              |
| Attenuation                                                  |
|   Min Distance [ 1.0 ]   Max Distance [ 40.0 ]               |
|   Rolloff      [ Linear                              v   ]   |
|                                                              |
| [Preview] [Stop]                              [Apply]        |
+--------------------------------------------------------------+
```

Implementation should not introduce a modal/page/tab whose layout expectation
has not been captured at architecture-note or feature-plan level. If the
implementation intentionally deviates from the agreed wireframe, the design note
or feature plan must be updated in the same change.

## Usage Scenarios

| Senaryo | UI Hareketi | Runtime Yolu | Sonuç / Not |
|---|---|---|---|
| 1. Asset import | Project panel → Assets/Audio/SFX → Import Asset → `explosion.wav` | Import dialog → AssetImporter validates WAV → AudioCooker builds platform formats → AudioClip asset + registry | Asset browser'da görünür; inspector'da duration, sample rate, loop settings okunur |
| 2. Audio Source ekleme | Hierarchy → Player → Add Component → Audio → Audio Source → sound (clip veya variation container)/bus/mode/spatial/preview | AudioSourceComponent sahneye serileşir → SceneRuntime transform çıkarır → sound resolve edilir → CreateVoice → MPSC → SPSC → callback → SFX bus → mixer | Preview ayrı bir bypass değil, normal command queue ile çalışır |
| 3. Mixer'da bus ayarı | Window → Audio → Mixer → SFX bus volume -6 dB, mute; solo (editor-only) | AudioFrontend redundant command birleştirir → SwapMixerGraph / SetVoiceParameters → buffer boundary'de uygulanır | SFX bus runtime'da mute; solo sahneye yazılmaz |
| 4. Middleware event | Inspector → Audio Source Kind: Middleware Event → StableEventId from registry (authoring name `enemy_footstep`) | AudioFrontend → IAudioMiddlewareEventBridge → VoiceRegistry proxy slot → MiddlewareVoiceHandle | Native ve middleware voice aynı voice bütçesini ve policy'yi paylaşır |
| 5. Snapshot tetikleme | Game code: `AudioMixer.ApplySnapshot("pause_menu")` | AudioFrontend snapshot command → Mixer ramps Music↓, UI↑, SFX↓ | Ducking click/pop olmadan uygulanır |
| 6. Animation event | Animation editor → frame 42 → Event type: Play Audio Clip → `footstep_run` | Animation system command producer olur → CreateVoice + StartVoice with timestamp | Animation sistemini audio thread'den poll etmeden senkron ses |
| 7. Timeline senkron ses | Timeline → explosion/dialogue/music cue'ları aynı zaman damgasına hizala | Timeline → ScheduledCommandBatch → SPSC → callback atomik uygular | Sesler sample-accurate sync başlar |
| 8. Mobil focus kaybı | Kullanıcı başka app'e geçer | Host focus event → Project Settings > Audio > Focus Behavior UI authors `audio-focus-policy.json` → AudioRuntime reads that policy → gameplay bus'ları pause, music devam | Oyun sesi leak etmez; dönüşte resume |
| 9. Audio Profiler | Window → Audio → Profiler | Callback bounded metrics yayınlar → observability queue → panel okur; callback'te log/allocate yok | Gerçek zamanlı maliyet; audio thread'ı bozmaz |
| 10. Localized dialogue | Proje tr-TR; sahne `dialogue_greeting` clip'i kullanır | Asset pipeline `dialogue_greeting` → `dialogue_greeting_tr-TR` resolve eder → runtime resolved AssetId alır | Runtime locale bilmez, sadece son AssetId'yi görür |

## Related Documents

- [Audio Mixer UI Reference](./audio-mixer.html): bus routing, DSP chains, meters, and middleware bridge status panel.

- [Asset Pipeline](./asset-pipeline.md)
- [Runtime Lifecycle](./runtime-lifecycle.md)
- [Scene Runtime](./scene-runtime.md)
- [Game UI And HUD](./game-ui-and-hud.md)
- [Configuration System](../foundation/configuration-system.md)
- [Observability Metrics And Profiling](../observability/observability-performance.md)
