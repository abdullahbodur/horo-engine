# ADR-067: Platform Audio Backend Strategy

- **Status**: Proposed
- **Date**: 2026-09-02
- **Supersedes**: None
- **Scope**: Native and portability output-backend roles, 1.0 platform matrix, compile-time composition, runtime selection, capability parity, and qualification
- **Issue**: [AUD-007.1](https://github.com/abdullahbodur/horo-engine/issues/595)
- **Jira**: [HORO-595](https://horo-engine.atlassian.net/browse/HORO-595)
- **Parent**: [AUD-007](https://github.com/abdullahbodur/horo-engine/issues/594)
- **Related**: [ADR-062](062-audio-runtime-ownership-and-update-order.md), [ADR-063](063-audio-sample-format-and-channel-layout.md), [ADR-066](066-spatial-provider-and-required-capability.md)
- **Normative documents**: [Audio Architecture](../architecture/runtime/audio-architecture.md), [Build System](../architecture/delivery/build-system.md)

## Context

Horo needs output-device enumeration, opening, callback delivery, format and
latency negotiation, hotplug, loss, recovery, and shutdown on Windows, macOS, and
Linux. The engine already uses SDL3 for platform windowing, but an SDL audio path
does not replace native capability and lifecycle qualification. Conversely, direct
WASAPI, Core Audio, and PipeWire integrations must not become separate public
audio APIs or architectural bases for one another.

The supported 1.0 matrix, portability/reference role, build composition, selection
point, baseline parity gate, and fallback policy must be fixed before detailed
device contracts and implementations. Otherwise hosts may compile every platform
dependency, a backend may leak native types into Audio API, SDL3 may become an
unnoticed fallback, or native implementations may expose materially different
lifecycle behavior while all claim generic support.

## Decision

### 1. Horo owns one backend-neutral output contract

Audio API, model, control runtime, mixer, graph, spatial policy, and assets use
Horo-owned typed descriptors, handles, capabilities, results, states, formats,
layouts, timestamps, and diagnostics. They do not expose or include WASAPI/COM,
Core Audio/AudioUnit/AVFoundation, PipeWire/SPA, SDL audio, middleware, or native
device handles.

Concrete output backends are equal peers behind one narrow internal Horo contract:

- `WASAPI` is the first-party native Windows output backend;
- `CoreAudio` is the first-party native macOS output backend;
- `PipeWire` is the first-party native Linux output backend;
- `SDL3Audio` is the first-party portability and contract-reference backend;
- `NullAudio` is the deterministic headless/test backend with no physical device.

No backend subclasses, wraps as policy, or uses another Horo backend as its
architectural base. A concrete implementation may privately use its platform API
or SDL3, but cannot reuse another backend by leaking its native identity through
the common contract. Shared Horo utilities are backend-neutral and contain no
platform selection.

The mixer always produces ADR-063 Horo planar blocks. Each backend privately maps
the selected Horo output layout/sample rate/block contract to native buffers and
reports the actual negotiated result. Native interleaving, sample conversion,
channel maps, timing APIs, and handles remain in that adapter.

### 2. Supported 1.0 desktop matrix

The intended supported 1.0 output matrix is:

| Product target | Native production backend | Portability/reference peer | Headless/test peer | Qualification baseline |
|---|---|---|---|---|
| Windows 11, native x86_64 | WASAPI | SDL3Audio | NullAudio | MSVC/Windows SDK host baseline; real default/explicit device, loss, Bluetooth, and shared-mode hardware lanes |
| macOS 14+, native arm64 and x86_64 | CoreAudio | SDL3Audio | NullAudio | Apple host baseline; real default/explicit device, route change, aggregate/Bluetooth where admitted, and both architecture lanes |
| Linux, native x86_64; Ubuntu 24.04 LTS reference | PipeWire | SDL3Audio | NullAudio | PipeWire session on qualified Wayland/X11 hosts; real default/explicit node, graph restart, Bluetooth where admitted, and published distribution lanes |

This table is a delivery obligation, not a claim that unimplemented or unqualified
lanes already pass. A target becomes release-supported only after its native
backend passes the common contract/parity suite and its published native hardware/
OS matrix. SDL3 passing on a platform does not qualify that platform's native
backend, and a native backend passing does not qualify SDL3.

Windows on ARM, Linux on ARM, iOS, iPadOS, Android, Web, consoles, XR-specific
device APIs, JACK, PulseAudio direct integration, ALSA direct integration, and
exclusive/pro-audio modes are outside the 1.0 baseline unless a later decision
adds a qualified row. PipeWire may bridge other Linux systems internally, but
Horo's supported backend remains PipeWire; it does not expose the bridge as a
second Horo device contract.

### 3. Native backends are product defaults; SDL3 is not a hidden fallback

The default packaged/editor selection for each supported desktop target is its
native backend: WASAPI on Windows, Core Audio on macOS, and PipeWire on Linux.
SDL3Audio exists to:

- provide a portable peer for development, embedding, comparison, and explicit
  products that choose its capability set;
- exercise the shared Horo device/backend contract independently of each direct
  native adapter;
- help diagnose whether behavior belongs to common Audio control or one native
  adapter;
- support future platforms only after those platform/backend tuples receive an
  explicit support decision and qualification.

SDL3's use for windowing/input does not select SDL3Audio. A missing, unavailable,
or failed native backend never silently activates SDL3Audio or NullAudio. A host
may evaluate an explicitly persisted/command-line ordered fallback list before
AudioRuntime activation, with a complete preflight and observable attempted/
selected IDs. Interactive products cannot fall back to NullAudio. Project intent
is not rewritten by an automatic recommendation or one successful fallback.

Once an AudioRuntime is active, backend identity is fixed for its lifetime. Device
loss and default-device change recover through the same backend's ADR-062 device
transaction. Changing WASAPI/CoreAudio/PipeWire/SDL3Audio/NullAudio requires an
ordered AudioRuntime teardown and replacement by the process host; the callback
and backend cannot switch identity themselves.

### 4. Compile-time composition is explicit and narrow

Each backend is a separate target with private native dependencies and platform
guards. Conceptual target ownership is:

```text
HoroAudioApi / HoroAudioModel
            ^
HoroAudioRuntime + internal backend contract/registry
            ^
  +---------+----------+-----------+----------+---------+
  |         |          |           |          |         |
WASAPI  CoreAudio   PipeWire   SDL3Audio   NullAudio  middleware adapter
  ^         ^          ^           ^          ^
 private platform/SDK dependencies only

Application/process composition root links admitted peers and registers their
inert descriptors/factories before AudioRuntime selection.
```

Supported builds may include the platform-native backend, SDL3Audio, and NullAudio
together. Minimal products may omit any backend they do not select, including
SDL3Audio. A headless host may include only NullAudio or omit AudioRuntime entirely.
Cross-compiling a target compiles only backends valid for that target platform; it
does not inspect or link host-machine audio APIs.

Registration functions publish inert IDs, descriptors, and factories. Static
initializers do not enumerate devices, initialize COM, create dispatch/run-loop
objects, connect PipeWire, initialize SDL audio, open streams, start threads, or
mutate an ambient service locator. The process/application composition root owns
registration and selection. Linking a backend proves neither availability nor
activation.

Repository-wide source/include roots are not exported. Public Horo headers remain
assigned to their owning targets. Windows SDK, Apple framework, PipeWire/SPA, and
SDL headers are private to the smallest backend adapter target, and their link
options/frameworks/packages do not fan out through `PUBLIC` or `INTERFACE` usage
requirements.

### 5. Build presence, availability, selection, and activation are distinct

The control model retains separate states:

| Dimension | Meaning |
|---|---|
| Compiled | Backend target/factory is present in this product artifact. |
| Host supported | Backend descriptor admits the target OS, architecture, and required runtime/library baseline. |
| Runtime available | A current bounded probe found the required service/API and can negotiate the requested baseline. |
| Selected | Startup policy resolved this stable backend ID before device construction. |
| Active | The selected backend completed initialization, opened a device, installed/started the callback, and matched ADR-062 readiness acknowledgement. |

A compiled descriptor or installed SDL/PipeWire runtime is not an availability
claim. Availability probes run outside the callback, are bounded/cancellable, and
record the exact backend version, target facts, service/API status, and probe
revision. They do not open the production stream or publish active capability.

Selection input priority is explicit command-line/test override, project/build
profile, user preference where product policy permits it, then platform-native
host default. Resolution uses stable backend IDs and an immutable registry/policy
snapshot. Registration order, link order, device enumeration order, and which
probe finishes first never select a backend.

### 6. Equal-peer parity means one mandatory lifecycle baseline

AUD-007.2 defines the complete versioned device/backend capability contract. Every
backend claiming a supported 1.0 matrix cell must at least provide equivalent Horo
semantics for:

- bounded enumeration and stable generation-scoped Horo device identity, including
  a typed default-device role without exposing native IDs as public identity;
- requested policy versus actual sample rate, ADR-063 layout, sample/native buffer
  representation, block size/period, and channel map;
- asynchronous open/start, callback readiness, quiesce/stop/close, cancellation,
  partial failure unwind, and idempotent shutdown under ADR-062;
- device/default-route hotplug and loss facts, reconfigure/reopen results, stale
  callback/device generation rejection, and no backend-owned product fallback;
- monotonic timing evidence, requested/actual period, reported latency components
  with provenance/unknown values, xrun/underrun, callback-load, and fault records;
- focus/suspend/recovery integration without inferring policy from missing frames;
- bounded callback behavior with no allocation, blocking, logging, file/network,
  GUI/ECS/configuration query, or owner-state mutation.

Parity means common observable states/results and lifecycle invariants, not that
every native API exposes identical optional features. Exclusive mode, pro-audio
priority, aggregate devices, session/category controls, per-app volume, hardware
spatial/object output, and native diagnostics are typed optional capabilities.
Missing optional capability is `Unsupported`, never a false zero/default or a
reason to reinterpret another operation.

NullAudio provides the same control states, generations, commands, timing schema,
and deterministic callback contract without claiming physical enumeration,
hardware latency, native hotplug, Bluetooth, exclusive mode, or device quality.

### 7. Requested, effective, and measured values remain separate

Project/profile policy requests latency class, preferred sample rate/layout,
period/block bounds, default or explicit device role, and allowed optional modes.
The selected backend negotiates an immutable effective device descriptor and
reports every deviation with a typed reason. Audio control validates it before
publishing a callback epoch.

Requested values are never overwritten by native results, and native results are
not claimed as measured end-to-end latency. The common snapshot distinguishes:

- requested policy and limits;
- backend/API-reported stream/device values with source and timestamp;
- Horo effective ADR-063 format/layout/block plan after adapter conversion;
- measured callback cadence/load/xrun observations and their window/provenance;
- unknown, unavailable, estimated, and unsupported values.

A backend cannot silently change sample rate, layout, channel roles, block size,
exclusive/shared mode, or device identity. If negotiation produces a value outside
the admitted profile, startup/reconfigure fails or evaluates an explicitly defined
profile fallback before publication.

### 8. Native ownership and threading stay private

Each backend owns its native service/client/device/stream/callback objects and the
adapter memory needed to convert between native buffers and ADR-063 blocks. It
reports device/callback facts to Audio control but cannot commit runtime/device
state, select a new backend, call scene/game/editor producers, or reclaim control-
owned callback-visible state.

Backend-specific thread/apartment/run-loop rules are declared in the implementation
contract and honored during open, reset, and close. WASAPI COM apartments, Apple
audio/run-loop objects, PipeWire loop/proxy ownership, and SDL audio streams are not
created on arbitrary workers. Shutdown first closes admission, quiesces and stops
the matching callback/device generation, joins or detaches native work under its
documented guarantee, releases adapter/native state, and only then unloads owners.

Native callbacks translate only bounded facts into Horo callback/fault queues. A
platform callback cannot format ordinary logs, invoke a user callback, enumerate
devices, load a backend, or run recovery policy.

### 9. Qualification and distribution are per matrix cell

A backend/platform/architecture tuple is supported only when CI/build evidence and
real-device qualification cover the common suite plus native risk cases. Required
1.0 evidence includes:

- build/link/package with no native type/header/dependency escaping its adapter;
- contract tests shared across WASAPI, CoreAudio, PipeWire, SDL3Audio, and NullAudio;
- actual default and explicitly selected device, requested/effective mismatch,
  format/layout conversion, callback cadence, latency provenance, and xrun paths;
- open/start cancellation and failure at every acquired stage; loss, hotplug,
  default change, reconfigure, suspend/resume, recovery, and repeated shutdown;
- wired/Bluetooth and other published device classes, with unsupported native
  modes reported truthfully rather than generalized from one fixture;
- long-running callback allocation/lock/deadline checks and stale generation/
  late callback rejection;
- side-by-side native versus SDL3 reference results without using one as proof of
  the other.

Hardware smoke tests are opt-in/local or protected CI lanes and record OS,
architecture, backend/API/runtime version, device identity in privacy-safe form,
requested/effective format, workload, and result. Simulator, virtual, software,
or Null results are labeled and do not qualify physical hardware claims.

### 10. Migration and delivery

Existing generic "platform backend" text is refined to name the equal-peer matrix
and selection boundary. Existing products with no explicit audio backend migrate
to the platform-native default for their target. They do not inherit SDL3Audio
merely because the editor already links SDL3 for windowing/input.

AUD-007.2 through later children deliver the device contract, SDL3 reference,
WASAPI, Core Audio, PipeWire, hotplug/default change, latency policy, tests, and
qualification. These implementations may add private native details and optional
capabilities but cannot revise the common ownership, selection, fallback, or
support matrix without an architecture update.

Required strategy coverage includes:

- supported/unsupported target guards for every backend and architecture;
- native-only, SDL3-only, Null-only, combined, Audio-omitted, and cross-target
  composition without ambient registration or dependency leakage;
- selection-priority fixtures for command-line/test override, project/build
  profile, permitted user preference, and platform-native host default, plus
  explicit/default/fallback-list resolution independent of registration, link,
  enumeration, and probe-completion order;
- missing/unavailable/init-failed native selection with no implicit SDL3/Null
  activation, including an interactive-host startup failure instead of NullAudio,
  and active-runtime backend identity fixed until host replacement;
- shared asynchronous open/start/close and format-conversion parity fixtures for
  WASAPI, CoreAudio, PipeWire, SDL3Audio, and NullAudio, plus per-platform real
  hardware/lifecycle qualification;
- deterministic NullAudio clock, callback cadence, generation, and control-state
  transition fixtures without physical-hardware claims;
- public-header and transitive-usage-requirement checks proving that native SDK,
  PipeWire/SPA, and SDL audio dependencies remain private to their adapters;
- requested/effective/reported/measured value separation and typed optional
  capability absence;
- partial startup, callback-ready timeout, device loss/recovery, stale/late native
  callbacks, provider re-preflight, and idempotent shutdown.

## Consequences

Horo gets direct native control and qualification on its three supported desktop
families while retaining SDL3 as a valuable portability/reference implementation.
All four physical-output paths plus Null remain peers behind one Horo contract.
Product defaults, fallback, build footprint, device behavior, and support claims
are explicit and reviewable.

The cost is four output adapters plus Null, a shared parity suite, platform-specific
hardware lanes, narrow build targets, and duplicate qualification of native and
SDL3 paths. That cost prevents portability convenience from hiding native gaps or
creating a lowest-common-denominator public API.

## Rejected Alternatives

### Use SDL3Audio as the only 1.0 backend

Rejected because it cannot establish direct native capability, timing, lifecycle,
and platform-qualification ownership, and it would make native backends subordinate
future exceptions rather than peers.

### Make SDL3Audio the base class or implementation of native backends

Rejected because WASAPI, Core Audio, and PipeWire ownership and native guarantees
would be hidden behind another concrete API. All implementations target Horo's
contract directly.

### Ship native backends only and remove SDL3Audio

Rejected because a portability/reference peer improves contract testing, embedding,
diagnosis, and future explicit platform work without weakening native support.

### Compile every backend into every executable

Rejected because headless/minimal tools would acquire unused platform dependencies
and initialization risk. Hosts link only admitted targets and register explicitly.

### Fall back from a failed native backend to SDL3Audio automatically

Rejected because backend identity, device semantics, latency, and capabilities may
change. Only an explicit pre-activation fallback list may select another peer.

### Treat native optional features as mandatory parity

Rejected because platform APIs differ. The baseline lifecycle/result contract is
mandatory; additional behavior is a typed optional capability with truthful
availability and separate qualification.
