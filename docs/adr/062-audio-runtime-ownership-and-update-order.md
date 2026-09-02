# ADR-062: Audio Runtime Ownership and Update Order

- **Status**: Proposed
- **Date**: 2026-09-02
- **Supersedes**: None
- **Scope**: Process, control-thread, callback, scene-context, suspend, failure, and teardown authority
- **Issue**: [AUD-001.1](https://github.com/abdullahbodur/horo-engine/issues/525)
- **Jira**: [HORO-525](https://horo-engine.atlassian.net/browse/HORO-525)
- **Parent**: [AUD-001](https://github.com/abdullahbodur/horo-engine/issues/524)
- **Normative documents**: [Audio Architecture](../architecture/runtime/audio-architecture.md), [Runtime Lifecycle](../architecture/runtime/runtime-lifecycle.md)

## Context

Audio Architecture requires one runtime and a real-time-safe callback, but it
does not assign every lifecycle transition to one thread or define the ordering
between process startup, scene activation, host suspension, device loss, fatal
callback failure, and shutdown. A callback that can open devices, notify scene
objects, or tear down its own storage would violate the real-time contract. A
scene or editor surface that owns the runtime would introduce a reverse dependency
and make headless/null compositions behave differently.

The process host, audio control runtime, native or middleware backend, and audio
callback have different responsibilities. Their state machines and handshakes
must be explicit before typed handles, sample formats, memory pools, command
transport, clock mapping, completion queues, and reset reconciliation are
implemented.

## Decision

### 1. Authority and dependency direction

The process composition root owns the `AudioRuntime` object lifetime. Exactly one
audio control runtime owns all audio lifecycle state transitions within that
process. A selected backend owns native device objects and callback registration.
The callback owns only the real-time render-core state published for its current
epoch. Scene, gameplay, editor, timeline, asset, streaming, and middleware-event
producers are clients; none owns the runtime or callback.

| Authority | Owns | Must not own or call |
|---|---|---|
| Process host | Composition mode, runtime construction/destruction, fatal/degraded product policy | Native callback state, scene audio objects, editor UI |
| Audio control runtime | Runtime/device state machines, registries, scene contexts, graph generations, command normalization, completion reconciliation | Editor/ImGui, mutable ECS, scene lifecycle, backend-native handles in public state |
| Audio backend | Native device/stream objects, platform negotiation, callback install/start/stop | Runtime policy, scene lookup, command production, product fallback |
| Real-time callback | Current render-core epoch, sample cursor, voices/mixer/DSP state admitted to that epoch, bounded acknowledgements/events | Lifecycle transitions, allocation, blocking, I/O, logs, config, GUI, ECS, scene or application callbacks |
| Scene/game/editor/timeline producers | Typed value snapshots and command requests | Runtime/device destruction, callback memory, native handles, direct mixer mutation |

Dependency direction is:

```text
Foundation / Assets value contracts
        -> Audio API and model
        -> Audio control runtime
        -> selected private audio backend
        -> platform device API

Scene / Gameplay / Cinematic / Editor adapters
        -> Audio API or application audio capability
```

`AudioRuntime` does not include or query Scene Runtime, editor services, ImGui,
GUI, MCP, CLI, or gameplay module internals. Host/application adapters resolve
scene/listener/emitter/asset state into immutable typed audio values before
submission. Completion consumers query the control-owned store/queue by ID; the
callback never invokes a producer.

### 2. Composition modes

The host resolves one mode before runtime construction:

```cpp
enum class AudioCompositionMode : std::uint8_t {
    Omitted,
    Null,
    Device,
};
```

`Omitted` means no `AudioRuntime` exists. A capability request fails explicitly;
the host does not construct a hidden null runtime. `Null` constructs the same
control runtime and deterministic callback/core contract without a hardware
device. `Device` selects one verified native or middleware backend before startup.
Changing mode is host-owned runtime replacement, not an in-place callback switch.

Editor preview, editor play, packaged game, headless tests, and tools use these
same modes and contracts. They may choose different typed policy inputs, but do
not define new lifecycle states or bypass command staging.

### 3. Runtime, device, and callback state are separate

The control-owned runtime state machine is:

```text
Constructed -> Starting -> Active
Active -> Suspending -> Suspended
Suspended -> Resuming -> Active
Active | Suspending | Suspended | Resuming -> Recovering
Recovering -> Active | Suspended
Starting | Active | Suspending | Suspended | Resuming | Recovering
    -> Stopping -> Stopped
Starting | Active | Suspending | Suspended | Resuming | Recovering
    -> Failed -> Stopping
```

There are no transitions out of `Stopped`, and `Failed` is latched until teardown.
Repeated requests for the current state are idempotent acknowledgements or typed
invalid-transition results according to the operation contract; they never rerun
side effects. Only the audio control owner thread validates and commits these
transitions. The process host and device/callback threads submit requests or
facts; they do not write runtime state.

Device state is a subordinate backend fact:

```text
Closed -> Opening -> Ready -> Quiescing -> Closed
Ready -> Reconfiguring -> Ready
Ready | Reconfiguring -> Lost -> Opening | Closed
```

The audio control runtime owns the requested device transition and translates
backend outcomes. Device loss normally places the runtime in `Recovering`, not
immediately in process-fatal state. Product policy may admit bounded reopen, an
explicit replacement runtime, or a terminal audio failure; the backend cannot
silently select Null or another device.

Callback publication has a third, generation-scoped handshake:

```text
Detached -> Priming(epoch) -> Rendering(epoch)
Rendering(epoch) -> Quiescing(epoch) -> Detached
```

The control thread publishes immutable/preallocated epoch state and requests
transitions. The callback may acknowledge a matching epoch at a buffer boundary,
switch to preallocated silence, and emit bounded fault/device records. It cannot
publish a new epoch, free the old one, stop its native stream, or commit runtime/
device state. Control does not reclaim callback-visible memory until the matching
quiescence acknowledgement and backend callback-stop guarantee are both observed.

Every acknowledgement carries runtime generation, device generation, negotiated
format revision, and callback epoch. `Recovering` may commit to `Active` only when
control atomically validates the complete current tuple and observes the new
`Priming(epoch)` acknowledgement. It then publishes `Rendering(epoch)` and clears
the recovery flag in the same control-owner transition. A stale or partial
acknowledgement cannot activate the runtime or admit old-format commands.

### 4. Startup and partial failure

Normal startup is ordered:

1. The host resolves configuration, product policy, composition mode, and selected
   verified backend without starting a callback.
2. The host constructs the audio control runtime with Foundation, jobs,
   observability ingest, asset/audio-provider capabilities, and backend factory.
3. Control validates configuration and allocates all bounded queues, registries,
   pools, silence buffers, scratch, completion storage, and initial mixer graph.
4. The backend creates/opens the selected device or Null stream and reports its
   negotiated immutable format; no callback sees unpublished state.
5. Control validates the format against the audio contract, builds epoch 1, and
   publishes `Priming(1)`.
6. The backend installs/starts the callback. The callback adopts epoch 1 at its
   first buffer boundary and emits a bounded ready acknowledgement.
7. Control observes the matching acknowledgement, commits `Active`, and only then
   admits scene contexts and ordinary producer commands.

Every fallible step records what it acquired. Failure or cancellation closes
ordinary admission, requests callback quiescence when necessary, stops the stream,
releases backend/device objects, destroys pools/queues, and returns a typed startup
result in reverse acquisition order. A timeout before callback readiness does not
permit freeing callback-visible memory. `AudioRuntime` is never published as
active after partial initialization.

### 5. Control update and callback order

Audio does not become a hidden simulation scheduler. Its process-side work uses
existing Runtime Lifecycle boundaries:

- `ApplyQueuedOwnerThreadCommands`: apply host audio lifecycle, focus/suspend,
  device-reset, scene-context activation/unload, settings-generation, and fatal-
  policy requests. Publish only complete validated state transitions.
- fixed-tick/gameplay/cinematic/animation systems: submit typed timestamped or
  unscheduled producer requests; they never call the callback or drain audio state.
- `VariableUpdate`: control drains the bounded MPSC producer queue and callback
  completion/device records, validates handles/generations, resolves clock
  mapping, coalesces only permitted parameters, prepares streaming/resource work,
  and publishes bounded SPSC batches/immutable graph state.
- `CommitDeferredLifecycleChanges`: commit scene unload/context retirement and
  other lifecycle barriers whose callback acknowledgement was observed; stale
  generations cannot re-enter admission.
- `EndFrame`: publish bounded control-side snapshots/metrics and service required
  reconciliation even when rendering is omitted.

The hardware callback runs independently at backend-selected buffer boundaries.
It consumes only the current epoch, prevalidated SPSC work, fixed-capacity pools,
and retained audio payloads. The audio sample-frame clock is authoritative for
callback scheduling. Engine fixed ticks, presentation frames, cinematic time, and
wall time map into it through AUD-001.7; they do not determine callback cadence.

When the host is explicitly suspended and ordinary `VariableUpdate` is skipped,
the bounded lifecycle/control subset remains serviceable through
`ApplyQueuedOwnerThreadCommands` and `EndFrame`. The callback follows the committed
suspend policy: continue, hold buses, render silence, or quiesce the device. It
does not infer suspension from missing frames or focus state. The remaining pump
drains lifecycle-critical acknowledgements, device/fault records, and reserved
critical work; it does not admit ordinary scene/game command preparation.

### 6. Scene activation and unload

Scene Runtime submits an immutable `AudioSceneContextDescriptor` containing stable
scene/runtime identity, listener policy, resolved emitter snapshots, bus/asset
references, and finite capacity requirements. Audio control validates it and owns
the resulting generation-checked `AudioSceneContextHandle`. The scene holds a
client handle; it does not own voices, mixer nodes, callback slots, or the audio
runtime.

Activation is admitted only while the runtime is `Active`, after required assets,
budgets, listener policy, and device/core capabilities validate. Control publishes
one context generation at a command boundary. Callback commands carry context
generation and cannot query ECS/transforms. Per-frame/fixed-tick adapters submit
owned emitter/listener value snapshots; closing a scene invalidates later producer
submissions immediately.

Unload is a barrier, not a sequence of best-effort voice stops. Control closes
ordinary context admission, stages stop/release work through reserved critical
capacity, and records the barrier sequence/epoch. The callback acknowledges after
all earlier commands for that context and the barrier have been applied. Only then
does control publish terminal outcomes, retire callback slots/resources when safe,
and allow `CommitDeferredLifecycleChanges` to release the context. A late asset,
stream, editor, or scene command with the old generation is rejected. Scene
destruction never waits from the callback and the callback never calls scene code.

Process/global music or UI audio uses an explicitly host-owned context. It does
not stay alive accidentally because a scene context forgot to release voices.

### 7. Focus, suspend, reset, and recovery

Window focus, app backgrounding, host suspension, device interruption, and device
loss are distinct typed inputs. The host resolves focus policy; audio control owns
its transition. Scene or editor code cannot pause the device directly.

`Suspending` closes new ordinary start/admission work and submits one ordered
suspend batch after already accepted commands. After the callback acknowledges the
batch, control commits `Suspended`. Existing voices follow the committed policy;
their sample cursors either advance, hold, or restart only as that policy declares.
`Resuming` establishes a new clock correlation/discontinuity record before start
commands are admitted. Suspended wall time never becomes an unbounded catch-up
batch.

Device reconfiguration/loss closes device-dependent admission, quiesces the old
epoch when possible, reconciles accepted operations, and creates a fresh device/
callback epoch. Frontend asset IDs may remain, but voice/device handles and native
resources follow AUD-001.2/.9 generation rules. Old commands cannot cross the
reset barrier. Recovery does not silently resume a voice unless its declared
policy and retained source permit it.

### 8. Fatal callback and control failure

The callback cannot construct ordinary errors or invoke host failure handling.
On an invariant violation, unrecoverable backend fault, or configured deadline
failure, it writes one bounded preallocated fault record, switches the current
epoch to its admitted silence/quiescence behavior, and continues only the minimal
acknowledgement path required for safe detachment.

Control drains that record, validates epoch identity, closes admission, commits
`Failed`, and reconciles every accepted operation to exactly one terminal result
or an explicit reset/shutdown cancellation. It then notifies the process host
through the typed application/subsystem failure boundary. The host alone decides
whether the product terminates, continues without audio, or performs an explicitly
supported runtime replacement. No callback, backend, scene, or editor adapter can
make that policy decision.

A fatal control-thread invariant likewise closes admission and enters `Failed`.
Control must not destroy callback-visible memory before quiescence; if a bounded
stop cannot prove detachment, normal destruction is forbidden. At composition the
process host preallocates one bounded `FatalAudioEpochRetention` slot for the audio
runtime. The failure path moves the epoch-visible allocation, native stream/device
owner, and backend library lease into that slot without allocating. The quarantine
is never exposed as a service locator and is intentionally not destroyed or
unloaded before OS process exit. The host records the retained bytes and failed
identity, then follows its non-unwinding process-fatal termination path rather than
reporting a clean `Stopped` state.

### 9. Shutdown and ownership release

Host shutdown stops scene/game/editor/timeline/audio producers before destroying
Audio Runtime. The control-owned teardown is:

1. Atomically enter `Stopping`; reject new ordinary operations and context creation.
2. Close all producer ports/generations and cancel or join control-owned decode,
   stream-fill, and preparation work.
3. Stage scene/global stop, release, reset, and final barrier work through reserved
   critical capacity; continue draining completions.
4. Request callback `Quiescing(epoch)`. Outside the callback, wait only within the
   documented bounded backend deadline while continuing the required control pump.
5. Stop/unregister the native callback/stream and require the backend's detached
   guarantee. Observe the matching callback acknowledgement when the API permits.
6. Reconcile every accepted operation and publish terminal/cancelled outcomes
   before their control-side owners disappear.
7. Release callback-owned voices, graph/DSP state, payload leases, pools, queues,
   and epoch memory; then close/destroy the device and backend.
8. Release control registries, completion stores, metrics descriptors, and runtime
   dependencies; commit `Stopped` exactly once.

Steps 7–8 execute only after step 5 proves that native callback entry is
impossible. If the deadline expires without that proof, control still closes
admission and reconciles terminal operation outcomes, but transfers the complete
callback/native ownership island to `FatalAudioEpochRetention`; it does not free
epoch memory, destroy the native stream/device, unload backend code, or commit
`Stopped`. This failure branch is an intentional process-lifetime leak and ends in
the host's fatal termination path.

Shutdown after partial startup executes the applicable suffix in reverse
acquisition order. Repeated shutdown returns the recorded outcome and performs no
second native stop/free. No destructor calls a device API after backend teardown.
No callback-owned reference, producer port, scene context, voice, stream job, or
completion can survive runtime destruction.

### 10. Concurrency and observability

The control runtime declares one owner thread. Producer ingestion is bounded MPSC;
control-to-callback delivery is ordered bounded SPSC or an equivalently proven
single-consumer transport; callback-to-control records use a bounded real-time-safe
queue. Queue saturation cannot transfer lifecycle ownership to a producer or drop
required stop/release/barrier work. AUD-001.6 owns exact reservation/coalescing
policy.

Ordinary diagnostics, formatting, storage queries, and UI projection run outside
the callback. The callback may update allocation-free counters and bounded records.
Control associates them with runtime, device, callback epoch, scene context,
operation, and sample-frame identity before publication. Slow observability/UI
consumers never block control or callback progression.

### 11. Migration and verification

The broad `AudioRuntime` ownership paragraph and device-only state machine in
Audio Architecture become summaries of this ADR, not competing lifecycle policy.
AUD-001.2 through AUD-001.11 implement the typed identities, real-time data,
transport, clock, completion, teardown, Null, and instrumentation responsibilities.
AUD-001.12 reconciles the wider audio document. These statements identify scope;
issue numbers and milestones do not define execution order.

Required contract coverage includes:

- Device and Null startup, callback-ready acknowledgement, cancellation at every
  acquired stage, timeout without premature reclamation, and repeated shutdown.
- Legal/illegal runtime, device, and callback-epoch transitions with one control
  owner and stale/duplicate acknowledgement rejection.
- Scene activation/unload barriers with active voices, streams, queue saturation,
  late producers, asset completion, and scene replacement.
- Focus/suspend/resume policies, clock discontinuity, device loss/reconfigure,
  failed reopen, and explicit no-silent-Null behavior.
- Callback fault, control fault, deadline failure, completion overflow, and every
  accepted operation receiving at most one terminal result.
- Instrumented callback execution with no general allocation/free, blocking,
  ordinary logging, file/network/config/GUI/ECS access, or unbounded user code.
- Editor preview, editor play, packaged, headless omitted, and deterministic Null
  compositions consuming the same Audio API without reverse dependencies.

## Consequences

Every audio transition has one authority: the host owns composition policy,
control owns runtime/device transition commits, the backend owns native objects,
and the callback owns only its published epoch. Scene and editor code remain
producers over typed values, so Audio Runtime can be used by headless and packaged
hosts without depending upward. Startup, suspend, recovery, scene unload, fatal
failure, and shutdown now have testable ordering and memory-reclamation gates.

The cost is three coordinated state machines, epoch acknowledgements, reserved
critical transport capacity, context unload barriers, bounded control pumping
during suspension/teardown, and explicit operation reconciliation. Those costs are
required to avoid callback use-after-free, lost terminal outcomes, and hidden
editor/scene ownership.

## Rejected Alternatives

### Let the callback own device/runtime lifecycle

Rejected because native open/close, allocation, waiting, logging, and host policy
are not real-time safe. The callback only acknowledges published epochs and faults.

### Let each scene own an Audio Runtime

Rejected because device/mixer/global audio must outlive individual scenes and
scene unload would race callback storage. Scene contexts are generation-checked
clients of the process runtime.

### Let editor preview bypass the runtime

Rejected because it creates a second command, lifetime, and device policy and can
hide callback-safety defects from authoring workflows.

### Share mutable ECS/listener/emitter state with the callback

Rejected because callback timing would race scene storage and introduce a reverse
dependency. Producers submit owned immutable value snapshots.

### Free callback state after a timeout without detachment proof

Rejected because a late native callback becomes use-after-free. Fatal policy may
retain memory until process exit when safe detachment cannot be established.

### Silently fall back to Null after device loss

Rejected because composition mode and product degradation are host policy. Device
loss is observable recovery/failure, never an implicit backend change.

### Treat scene unload as ordinary best-effort StopVoice commands

Rejected because saturation/reordering could leave callback references after scene
owners disappear. A reserved ordered barrier provides a proof point for retirement.
