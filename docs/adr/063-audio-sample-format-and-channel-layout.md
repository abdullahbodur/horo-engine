# ADR-063: Audio Sample Format and Channel Layout

- **Status**: Proposed
- **Date**: 2026-09-02
- **Supersedes**: None
- **Scope**: Backend-neutral real-time sample representation, buffer layout, speaker/Ambisonic order, silence, denormals, and clipping
- **Issue**: [AUD-001.3](https://github.com/abdullahbodur/horo-engine/issues/527)
- **Jira**: [HORO-527](https://horo-engine.atlassian.net/browse/HORO-527)
- **Parent**: [AUD-001](https://github.com/abdullahbodur/horo-engine/issues/524)
- **Related**: [ADR-062](062-audio-runtime-ownership-and-update-order.md)
- **Normative document**: [Audio Architecture](../architecture/runtime/audio-architecture.md)

## Context

Audio Architecture leaves the mixer sample type, planar/interleaved boundary,
alignment, channel ordering, silence, denormal, and clipping policy unspecified.
If a backend's native format becomes the internal contract, DSP nodes, mixer
graphs, middleware adapters, Null tests, and platform devices will disagree on
buffer stride, full scale, speaker indices, and conversion ownership. An integer
or interleaved device format must not leak upward merely because one API prefers it.

The real-time core needs one representation that is efficient for per-channel DSP,
supports explicit multichannel and Ambisonic semantics, and can be allocated from
the callback-safe pools established by AUD-001.5. Device/source conversion remains
necessary, but it must occur at named boundaries with deterministic validation.

## Decision

### 1. Canonical processing sample

The canonical Horo processing sample is IEEE-754 binary32 (`float`) in native CPU
endianness. `AudioSample` is a Horo-owned semantic type alias/wrapper; public audio
contracts do not expose `SDL_AudioFormat`, `AVAudioFormat`, WASAPI/Core Audio/
PipeWire structs, middleware sample enums, or device buffer pointers.

Finite values are required. `-1.0F` and `+1.0F` are nominal negative/positive full
scale, and `+0.0F` is canonical silence. Internal buses and DSP intermediates may
exceed nominal full scale to preserve headroom; they are not clipped after every
node. NaN and infinity are never valid audio data or silence.

The internal representation is not a serialized asset format. Cooked clips,
compressed streams, decoder output, capture devices, and native output may use
other typed formats. Their adapters convert into/out of canonical processing
blocks at the source/capture and final-device boundaries.

### 2. Planar block contract

Mixer, voice, bus, DSP, spatializer, and resampler processing uses planar
channel-major storage:

```cpp
struct AudioPlanarBlockView {
    AudioChannelLayout layout;
    AudioSampleRate sampleRate;
    std::span<AudioSample*> planes;
    AudioFrameCount validFrames;
    AudioFrameCount capacityFrames;
};
```

`planes.size()` equals the validated layout channel count. Each plane contains
`capacityFrames` contiguous `AudioSample` values; the first `validFrames` are
semantic audio. Plane starts are aligned to at least 64 bytes. Capacity may be
rounded up for a private SIMD width, but padding is zero and is never counted as
audio time, hashed as payload, or exposed as extra frames. Every plane in a block
has the same capacity and valid-frame count.

The exact storage owner is a callback-safe pool, retained decoded block, or bounded
adapter buffer. A view is borrowed for one declared processing call/epoch and
cannot escape to jobs, scene/editor code, or a later callback. No DSP node changes
the plane array, layout, sample rate, capacity, or ownership. In-place sample
mutation is permitted only when the compiled graph grants exclusive write access;
otherwise the graph assigns a separate admitted output block.

Native interleaved input/output is converted only in the private backend adapter.
Decoder/capture adapters may accept interleaved bytes outside the mixer, but must
publish validated planar blocks before real-time graph use. Interleave/deinterleave
uses preallocated memory and bounded work when it occurs on a callback path.

### 3. Layout identity and channel roles

Channel count alone is not a layout. Every block carries one validated Horo-owned
layout value:

```cpp
enum class AudioLayoutKind : std::uint8_t {
    Speaker,
    Ambisonic,
    Discrete,
};

struct AudioChannelLayout {
    AudioLayoutKind kind;
    std::span<const AudioChannelRole> orderedChannels;
    std::optional<AmbisonicDescriptor> ambisonic;
};
```

The stored layout is an owned immutable value in descriptors/artifacts/graph state;
the span above illustrates processing access and does not authorize borrowed
configuration lifetime. Unknown role values, zero channels, duplicate speaker
roles, inconsistent Ambisonic metadata, channel-count mismatch, and counts above
the admitted profile limit are typed validation failures.

Horo's baseline speaker presets use this exact plane order:

| Preset | Ordered `AudioChannelRole` values |
|---|---|
| Mono | `FrontCenter` |
| Stereo | `FrontLeft`, `FrontRight` |
| 2.1 | `FrontLeft`, `FrontRight`, `LowFrequency` |
| Quad | `FrontLeft`, `FrontRight`, `BackLeft`, `BackRight` |
| 5.1 | `FrontLeft`, `FrontRight`, `FrontCenter`, `LowFrequency`, `SideLeft`, `SideRight` |
| 7.1 | `FrontLeft`, `FrontRight`, `FrontCenter`, `LowFrequency`, `SideLeft`, `SideRight`, `BackLeft`, `BackRight` |
| 7.1.4 | `FrontLeft`, `FrontRight`, `FrontCenter`, `LowFrequency`, `SideLeft`, `SideRight`, `BackLeft`, `BackRight`, `TopFrontLeft`, `TopFrontRight`, `TopBackLeft`, `TopBackRight` |

These orders are Horo processing order, not a claim that every file/container or
device uses the same indices. Adapters map semantic roles explicitly. A device
that reports back channels where Horo expects side channels cannot reinterpret
indices; it supplies an explicit role map or fails admission. `LowFrequency` is a
distinct role and is never inferred from a low-pass filter or channel position.

`Discrete` layouts use ordered `Discrete0` through `DiscreteN` roles and carry no
speaker-position meaning. Generic mixing does not automatically route them to a
speaker layout; an authored/registered conversion matrix is required.

### 4. Ambisonic contract

Canonical Ambisonic processing uses AmbiX semantics:

- ACN (Ambisonic Channel Number) order;
- SN3D normalization;
- real-valued components;
- channels ordered `ACN0, ACN1, ... ACN((order + 1)^2 - 1)`;
- channel count exactly `(order + 1)^2` using checked arithmetic.

The baseline contract admits orders 0 through 3 when the active audio profile and
spatial provider declare the required capacity. Higher orders are optional future
capabilities and fail typed admission today. FuMa ordering, N3D normalization,
legacy B-format labels, and platform-native Ambisonic enums are source/provider
formats; adapters convert them explicitly to ACN/SN3D before canonical graph use.

Speaker and Ambisonic layouts cannot be reinterpreted by channel count. Encoding,
rotation, and decoding require a typed spatial operation/provider with declared
order, coordinate convention, speaker target, latency, and capacity. A missing
provider is unsupported, not permission to route ACN planes as speakers.

### 5. Layout conversion and mixing

Every conversion names source and destination layout identities and uses a
versioned finite matrix or specialized admitted spatial operation. Matrix rows
are destination roles and columns are source roles. Coefficients must be finite,
bounded, and immutable for the processed graph generation. Validation rejects
missing required roles, duplicate routes, dimension mismatch, overflow, and a
matrix whose declared normalization policy cannot be satisfied.

No generic conversion silently invents center, LFE, height, or surround content.
Mono/stereo fold-down/upmix behavior, LFE contribution, center/surround gains,
headroom, and normalization are explicit profile policy. Unsupported layouts
return a typed result rather than truncating extra channels or copying the first N
planes. Layout conversion does not change sample rate; AUD-001.4 owns resampling.

### 6. Silence, clearing, and tails

Canonical silence is positive zero in every valid and padding sample. Pool acquire
returns a block in a declared state: `Cleared` or `UninitializedForOverwrite`.
Only a node proven to overwrite every valid output sample may request the latter.
Partial writers must clear or mix onto a `Cleared` block. Pool release does not
promise clearing; the next acquire owns initialization under this rule.

An inactive voice produces silence but may retain finite DSP tail state according
to its declared lifecycle. A bus/node that has a tail receives explicit silent
input for its bounded tail interval; it does not read stale pool bytes. End-of-
stream padding, decoder short reads, underrun fill, muted output, and missing input
all write canonical silence for the exact missing frame range.

### 7. Denormals and non-finite samples

The audio backend establishes the approved floating-point environment for the
callback thread before Horo processing and restores any host-owned environment if
the API reuses a foreign thread. Where supported, denormal inputs/results are
treated as zero using FTZ/DAZ or the platform-equivalent mode. DSP code cannot
change rounding/denormal policy per node. Platforms without a reliable mode use
bounded explicit near-zero sanitization in approved DSP primitives.

Denormal handling is not a noise gate: only subnormal binary32 values normalize to
canonical zero. It must not erase ordinary low-level audio. Reference/Null tests
exercise both the platform mode and explicit fallback.

Non-finite production is a DSP invariant violation. Development instrumentation
records the node/bus/epoch and fails the candidate path. Shipping safety replaces
the offending output sample with canonical silence through bounded sanitization,
increments a fault counter, and submits a preallocated ADR-062 fault record. It
does not continue indefinitely while hiding repeated corruption.

### 8. Clipping and device conversion

Internal graph operations may exceed `[-1, +1]`. Nodes that intentionally saturate,
limit, or distort declare that behavior; ordinary mix/format conversion does not
hard clip. The master graph owns any product limiter and headroom policy.

After master processing and immediately before native-device conversion, the
backend adapter performs one finite safety clamp to `[-1.0F, +1.0F]`. Conversion
to integer PCM uses a versioned rounding, saturation, and optional dither policy;
conversion to native float preserves the clamped binary32 values unless the
device contract explicitly requires another finite range. Input capture conversion
maps its declared native full scale into Horo nominal full scale without reusing
output clipping policy.

Clipped-sample/peak counters are allocation-free and bounded. Ordinary logging or
per-sample diagnostics are forbidden on the callback. Clipping does not alter
layout identity or permit non-finite values.

### 9. Backend, asset, middleware, and extension boundaries

Public runtime and extension DSP contracts consume `AudioPlanarBlockView`, Horo
sample rates, and Horo layouts only. A backend or middleware adapter may use a
private interleaved/native format internally, but must validate and convert at its
boundary. It cannot register its native enum as an `AudioChannelRole` or make
device negotiation mutate an already published graph layout.

Asset/cook metadata records its source/cooked sample format and semantic channel
layout independently. Runtime load/stream preparation converts decoded blocks to
the canonical representation before callback publication. A cache key includes
the target format/layout conversion contract; raw device-format bytes are not a
portable mixer cache entry.

DSP/spatial extensions declare supported Horo layout kinds, roles/orders, maximum
channels/frames, alignment, in-place behavior, tail policy, and scratch needs at
registration/prepare time. Unsupported combinations fail graph construction. A
plugin cannot inspect native device buffers or allocate a replacement layout in
`Process()`.

### 10. Migration and verification

Audio Architecture gains one summary of this ADR; it does not repeat a competing
sample or channel policy. AUD-001.4 consumes canonical blocks for resampling,
AUD-001.5 owns their pools/scratch, AUD-001.6 transports descriptors/commands,
and later mixer/spatial/backend tickets consume the same layout identities.
AUD-001.12 reconciles remaining audio documentation. These scope boundaries do not
infer delivery order from ticket numbers or milestones.

Required contract coverage includes:

- binary32 full-scale/silence/non-finite behavior and bitwise positive-zero clear;
- plane count, 64-byte alignment, valid/capacity/padding, exclusive in-place, and
  stale borrowed-view rejection;
- exact preset orders through native/file mappings, including side/back mismatch;
- Discrete rejection without a matrix and no silent truncation/reinterpretation;
- Ambisonic orders 0–3, checked `(order + 1)^2`, ACN/SN3D fixtures, and rejection
  or explicit conversion of FuMa/N3D inputs;
- conversion-matrix dimensions, headroom/normalization, LFE policy, and layout-
  conversion/sample-rate separation;
- denormal platform/fallback behavior without erasing normal low-level signals;
- internal over-range headroom, declared limiter behavior, final safety clamp,
  integer rounding/saturation/dither fixtures, and clip/fault counters;
- Null, native, middleware, streaming, capture, underrun, mute, tail, reset,
  suspend, and shutdown paths with callback allocation/lock instrumentation.

## Consequences

All DSP and mixer code sees one backend-neutral planar binary32 representation and
explicit semantic channel order. Backends remain free to negotiate native formats,
but conversion ownership is visible and testable. Speaker/Ambisonic data cannot be
silently reinterpreted, and silence, denormal, non-finite, and clipping behavior no
longer varies by device.

The cost is planar/interleaved conversion at some boundaries, explicit layout
maps/matrices, alignment/pool discipline, Ambisonic metadata, and final format
conversion tests. Planar storage can be less convenient for APIs that natively
interleave, but it keeps channel DSP, SIMD, routing, and validation consistent.

## Rejected Alternatives

### Use the active device format internally

Rejected because devices vary in sample type, interleave, channel order, and
range. Device change would otherwise mutate graph contracts and leak native types.

### Use interleaved float throughout the mixer

Rejected because most DSP/routing operates per channel and would carry stride/
shuffle complexity through every node. Interleaving is isolated to adapters.

### Use integer PCM as the mixer format

Rejected because repeated gain/mix/DSP operations lose headroom and require
format-specific saturation throughout the graph.

### Use float64 for every callback sample

Rejected because it doubles bandwidth/storage and reduces common SIMD throughput
without evidence that baseline real-time mixing needs it. Offline/reference tools
may use higher precision outside the public real-time block contract.

### Treat channel count as layout identity

Rejected because 4/6/8-channel speaker, discrete, and Ambisonic data have different
semantics and ordering. Every block carries a validated layout.

### Adopt a platform-native or FuMa Ambisonic order

Rejected because it would privilege one backend/ecosystem and make higher orders
ambiguous. ACN/SN3D AmbiX is explicit and scales by order.

### Clip after every DSP node

Rejected because it destroys headroom and compounds distortion. Only declared
effects/limiter policy and the final device safety boundary clip.
