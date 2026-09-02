# ADR-071: Procedural Audio Graph Ownership

- **Status**: Proposed
- **Date**: 2026-09-02
- **Supersedes**: None
- **Scope**: Procedural sound asset identity, graph compilation, generator execution, deterministic inputs, extension nodes, editor ownership, mixer/music boundaries, limits, and lifecycle
- **Issue**: [AUD-013.1](https://github.com/abdullahbodur/horo-engine/issues/655)
- **Jira**: [HORO-655](https://horo-engine.atlassian.net/browse/HORO-655)
- **Parent**: [AUD-013](https://github.com/abdullahbodur/horo-engine/issues/654)
- **Related**: [ADR-062](062-audio-runtime-ownership-and-update-order.md), [ADR-063](063-audio-sample-format-and-channel-layout.md), [ADR-064](064-audio-asset-and-cook-boundary.md), [ADR-065](065-mixer-topology-and-constrained-dag.md), [ADR-068](068-music-transport-and-cross-system-ownership.md), [ADR-069](069-audio-extension-capability-and-abi.md)
- **Normative documents**: [Audio Architecture](../architecture/runtime/audio-architecture.md), [Asset Pipeline](../architecture/runtime/asset-pipeline.md), [Editor Panel Host](../architecture/editor/editor-panel-host.md), [Extension System](../architecture/extensions/plugin-system.md)

## Context

Procedural audio needs graph authoring, typed parameters, deterministic random
inputs, bounded real-time execution, extension nodes, preview, cooking, and normal
voice routing. Those concerns touch Assets, Editor, AudioFrontend, the callback,
the mixer, music orchestration, and package extensions, but none of those existing
graphs has the same authority.

Treating a procedural graph as a mixer graph would let a source asset alter bus
topology. Treating it as adaptive music would move gameplay decisions into the
callback. Executing the editor graph directly would retain mutable UI state and
perform validation or allocation on the real-time path. The system instead needs
one compiled sound-generator boundary that reuses Audio's existing source, voice,
clock, mixer, extension, and lifecycle contracts.

## Decision

### 1. A procedural graph compiles to a separate sound generator

`ProceduralAudioGraph` is an Audio-owned authoring asset. Its runtime projection is
an immutable `CompiledSoundGenerator`, not a mixer graph, DSP effect chain, music
state machine, gameplay behavior graph, or executable editor document.

The generator produces one declared Horo audio output for one voice instance. The
voice is created and controlled through the normal `AudioFrontend`, participates
in voice priority/concurrency/virtualization, and routes by `AudioBusId` through
the ADR-065 mixer plan. The callback receives only an already prepared generator
instance and borrowed ADR-063 blocks. There is no second device, callback, command
queue, clock, voice registry, mixer, or backend path.

One source descriptor may select a cooked clip, stream, variation container, or
compiled generator by typed kind. Display names, editor node positions, source
paths, extension registration order, and string event names are never runtime
identity.

### 2. Authorities remain separate

| Concern | Authority |
|---|---|
| Stable source identity, sidecars, dependencies, cache, staging, publication and rollback | AST under ADR-064 |
| Graph schema, typed node/pin/link/parameter semantics, compiler and cooked generator format | AudioModel and AudioCook |
| Document tabs, selection, node placement, undo/redo, copy/paste, preview controls and diagnostics presentation | Editor |
| Instance preparation, command admission, voice ownership, generation publication, limits, faults and retirement | Audio control/runtime |
| Exact bounded sample generation | Audio callback using the compiled plan |
| Bus topology, effects, sends, returns and final output order | Mixer under ADR-065 |
| Gameplay/adaptive-music state, narrative choice and desired transitions | Gameplay/application/package orchestrator under ADR-068 |
| Package discovery, trust, native module activation and leases | EXT/PKG under ADR-069 |

The editor does not compile with widget types or mutate a live instance. AST does
not interpret graph nodes. A generator cannot discover gameplay, ECS, editor,
assets, packages, buses, services, devices, or clocks from its process function.

### 3. Source and cooked identities are explicit

The authoring asset has one stable `AssetId`, one procedural-graph `AssetTypeId`,
one schema version, stable node/pin/link/parameter/trigger/output IDs, dependency
asset IDs, and typed metadata. Renaming or moving the file does not change identity.
Editor viewport coordinates, selection, folded state, color themes, breakpoints,
preview history, temporary meters, and undo stacks are editor-session data and are
excluded from runtime semantics and cook fingerprints.

AudioCook receives an immutable validated source snapshot through AST. It resolves
declared dependencies, validates the graph and profile, and emits a versioned
`CompiledSoundGenerator` artifact inside the host-owned publication transaction.
The artifact identifies at least:

- source asset and semantic revision;
- graph schema and compiler version;
- exact node capability/schema versions and extension contribution identities;
- target/profile, sample representation/layout/rate/block constraints;
- canonical plan and constant/resource tables;
- parameter/trigger/output schemas and defaults;
- maximum instance state, scratch, work, latency, tail, event and voice costs;
- deterministic seed/input policy and compatibility fingerprint.

The domain cook fingerprint includes all semantic source bytes, dependency
digests, compiler/profile versions, selected node contributions, and deterministic
options. AST retains cache-key, output-placement, atomic publication, rollback and
last-good authority. A cooker cannot publish files or invent another asset ID.

### 4. The authored graph is typed and the compiled plan is immutable

Nodes, pins and links use stable canonical IDs. Pins declare an exact signal or
control type, rate class, channel/layout contract, cardinality and default policy.
Baseline rate classes are audio-block/sample processing, block-rate control,
scheduled trigger/event, and immutable constant/resource input. Stringly typed
maps and implicit scalar/audio coercion are rejected.

The baseline signal/control dependency graph is acyclic. The compiler rejects
self-links, hidden dependencies, incompatible pins, missing required inputs,
multiple writers where not declared, ambiguous outputs and cycles. Oscillators,
envelopes, filters, delay lines and similar nodes may own declared bounded history;
that private state is not a graph feedback edge. A future feedback feature requires
an explicit delay contract, minimum latency, bounded state/gain policy and its own
accepted decision.

Compilation performs all type checking, constant folding, dependency planning,
layout/rate adaptation planning, state/scratch allocation layout, resource
resolution and node preparation. The callback cannot compile, validate, allocate,
look up a registry, resize storage or change topology.

### 5. Determinism is input and plan based

The compiler constructs one canonical topological order. When multiple nodes are
ready it chooses the smallest canonical node ID; it orders a node's connected
inputs by canonical link ID and serialized parameters/resources by stable ID.
Source file order, editor action history, locale, pointer value, unordered-map
iteration, worker completion and contribution registration order never choose the
plan.

Every random/noise node consumes a generator-instance seed plus stable node/stream
identity through a declared deterministic algorithm. Ambient process entropy,
wall time, device time, thread ID and global random generators are forbidden.
Retries and preview may choose a new seed only through an explicit frontend input;
the chosen seed is observable and recordable by the owning product domain.

For the same cooked artifact, effective format, block schedule, parameter/trigger
stream, seed and declared resource bytes, the generator preserves the documented
sample result and state progression. The baseline promise is deterministic order
and a profile-defined numerical tolerance across qualified platforms, not implicit
bit identity across compilers, SIMD modes or third-party providers. A bit-exact
profile is a separately declared capability and rejects any node that cannot prove
it. Null/offline reference rendering is the conformance oracle for admitted
profiles.

### 6. Runtime instances are voice-owned and callback bounded

Audio control loads and validates the cooked header, pins asset and node-provider
leases, allocates fixed instance/scratch/output storage, prepares resources, and
constructs a private candidate. Only a fully prepared candidate may enter a new
voice generation at a callback boundary. Preparation failure leaves the active
runtime and last-good artifact unchanged.

The callback executes the immutable plan into a borrowed Horo output block and
then hands that signal to the ordinary voice gain/pitch/spatial/routing path. It
may use only preallocated instance state, immutable constants/resources, bounded
scheduled inputs and the exact Audio RT operations admitted by the plan. It cannot
allocate, block, lock, wait, log, perform I/O, spawn work, throw, query services or
retain borrowed views.

Each plan declares worst-case nodes, edges, channels, block frames, state, scratch,
work units, resources, parameters, queued triggers, latency, tail and simultaneous
instance cost. Product/profile budgets are checked at cook, runtime load and voice
admission. Unknown or unbounded cost is rejection, not best-effort callback work.
Queue or voice pressure returns a typed outcome and never silently drops an
authoritative trigger.

Virtualization behavior is explicit per graph: advance deterministic state without
rendering when admitted and bounded, suspend then reset/restart by declared policy,
or remain non-virtualizable. The runtime never skips arbitrary blocks and later
claims equivalent state. Voice stealing, seek, restart and tail cancellation
produce typed observations through normal Audio control queues.

### 7. Parameters and triggers reuse AudioFrontend scheduling

Public control uses stable `AudioParameterId` and `AudioTriggerId` values plus
typed bounded payloads. Display labels and extension/vendor strings stay in
authoring/presentation adapters. A parameter schema declares scalar/vector/enum/
asset type, range, default, smoothing/ramp policy and update rate. A trigger schema
declares payload, multiplicity and queue bound.

Frontend requests carry voice/generator generation, expected asset revision,
stable request/occurrence identity, value or trigger, timing target, late policy
and owner/cancellation token. Audio control validates and converts them into the
same sample-boundary `ScheduledCommandBatch` model used by ordinary voices and
music transport. The callback never calls gameplay to pull a value.

A gameplay or adaptive-music system may create a generator voice and submit
parameters/triggers. The procedural graph cannot evaluate combat state, narrative
branches, sequence tracks or tempo policy, and it cannot schedule another voice or
transport transition. Music transport may provide admitted sample/phase evidence
as an explicit typed input; it does not transfer ADR-068 clock or state authority.

### 8. Generator nodes and mixer DSP have different roles

A generator node creates or transforms signal within one source instance. A mixer
DSP node processes an admitted voice/bus signal in the ADR-065 bus plan. A
procedural graph cannot create, rename or route buses; add sends/returns; install
bus effects; query meters; change Master; or call another voice. Its one declared
output enters the selected bus exactly like clip/stream voice output.

Shared algorithms may have separate generator-node and mixer-DSP descriptors over
one private implementation, but the host validates each role independently. A DSP
capability is not automatically a procedural-node capability, and a graph cannot
smuggle hidden mixer routes or sidechains through a node.

### 9. Extension nodes use a closed Audio capability

Procedural node packages register the versioned `audio.generator.node` capability
family through the ADR-069 activation transaction. Its descriptor declares stable
node type and schema IDs, pin/rate/layout contracts, deterministic tier, state/
scratch/work/latency/tail limits, resource kinds, trust requirements, and compatible
compiler and Audio RT ABI versions. Registration order never resolves duplicate
node identities.

Pure-data built-in opcodes are preferred. Native third-party process code must use
the separate ADR-069 Audio RT C ABI, trust, fixed memory, fault and module-lease
rules; a generic extension callback, scripting object, editor command or service
capability cannot execute in the callback. The cook fingerprint and runtime plan
pin the exact selected contribution. Missing, disabled, changed or incompatible
nodes fail compile/load/admission explicitly rather than choosing a similarly named
provider.

### 10. Editor owns authoring interaction, not semantics

The Editor hosts a procedural-audio document/controller using the shared node
surface contract. It owns open/dirty/save state, selection, node placement,
undo/redo, clipboard, contextual menus, preview controls and localization. The
widget emits `GraphEditCommand` values against a document revision and renders
Audio-owned schema metadata and diagnostics.

AudioModel applies semantic edits to an owned graph candidate and returns typed
diagnostics keyed by stable IDs. AudioCook or a bounded preview compiler produces
an immutable candidate; the preview runtime instantiates that candidate through
the same AudioFrontend/voice/mixer path as a game. Stale diagnostics, compilation
and preview results are rejected when the document revision changes.

The UI never retains a live callback instance, provider pointer, native node type,
borrowed asset view or mutable compiler graph. Preview stop, tab close, project
close, package change and editor shutdown cancel/join compilation, stop the preview
voice, drain generations and release leases in the normal Audio order.

### 11. Reload, failure and retirement preserve generations

An asset or node-package change builds a complete new cooked artifact and prepared
runtime candidate. It does not patch instructions, state pointers or topology in a
live callback plan. Product policy chooses restart, explicit state migration, or
keep-current-until-next-play; migration is admitted only between exact compatible
schema versions through a bounded off-callback function.

The old artifact, resources, node code and instance state remain leased until all
voices, tails, queued commands/events, callback epochs, preview jobs and diagnostic
views release their generation. Timeout, device loss, stuck work or an unknown tail
keeps the code/artifact loaded and reports pending restart/failure; it never
force-frees callback-visible memory.

Callback faults are bounded stable codes/counters associated with asset, plan,
node, voice and callback generations. The compiled policy chooses silence, bypass
where semantically valid, or voice failure; control owns formatting and recovery.
Diagnostics exclude source samples, proprietary node state, pointers and user
content by default.

### 12. Migration and verification

Audio Architecture projects this separate-generator model. Asset Pipeline records
procedural graphs as an Audio-owned domain cooker over AST authority. Editor Panel
Host keeps graph widgets presentation-only. AUD-013.2 and later children define the
exact schema, compiler, core node library, parameters, preview and runtime without
creating an alternate graph or callback path.

Required contract coverage includes:

- stable asset/node/pin/link/parameter/trigger identity across rename, source order,
  editor action history and serialization round trips;
- malformed schemas, duplicate/missing IDs, pin/type/rate/layout mismatches,
  ambiguous outputs, self/indirect cycles and canonical diagnostics;
- canonical compiled bytes/order/fingerprints across worker order, map seed,
  contribution order and locale, plus pinned numerical reference renders;
- deterministic seed/stream fixtures, explicit new-seed behavior, no ambient
  entropy and bit-exact-profile rejection for unsupported nodes;
- cook cancellation, missing dependency/provider, cache mismatch, publication
  rollback, last-good retention and runtime cooked-header validation;
- state/scratch/work/resource/event/latency/tail/voice budget rejection before
  publication, queue pressure and declared virtualization/state progression;
- sample-boundary parameter ramps/triggers, stale generation/revision, duplicate
  occurrence and late-policy outcomes through AudioFrontend;
- proof that generator graphs cannot mutate mixer topology or music/gameplay state,
  and that output follows ordinary voice spatial/routing/priority behavior;
- extension ABI/version/trust/limit mismatch, transactional registration rollback,
  package disable/update and lease-gated retirement;
- editor revision races, undo/redo, failed preview compile, tab/project close and
  shutdown with no widget/provider/asset view retained by runtime;
- callback allocation, lock, wait, logging, I/O, registry/service lookup, dynamic
  topology and unbounded-call guards.

## Consequences

Procedural synthesis gains a reproducible, cookable and extensible asset model
without becoming a second mixer or music engine. Games use the same frontend,
voice, scheduling, spatialization, bus routing, limits and lifecycle as clip-based
audio, while editor interactions remain outside runtime authority.

The cost is a dedicated graph schema/compiler, stable typed IDs, explicit profile
budgets, immutable runtime plans, deterministic seed/input policy, provider leases
and revision-aware preview. These constraints are necessary to keep authored
graphs safe for a real-time callback and portable across builds.

## Rejected Alternatives

### Execute the editor graph directly in the callback

Rejected because mutable UI state, widget IDs, validation, allocation, undo data
and borrowed document memory have no callback-safe lifetime or deterministic plan.

### Reuse the mixer DAG as the procedural graph

Rejected because the mixer owns global buses, routes, effects and output order,
while a generator owns one voice's signal. Combining them would let a source asset
change product-wide routing and lifecycle.

### Model procedural audio as adaptive music

Rejected because gameplay/music orchestration owns state and transition choice.
A generator may receive scheduled typed inputs but cannot become that authority.

### Let nodes call gameplay, services or other voices

Rejected because pull callbacks create hidden dependencies, unbounded work,
thread-affinity violations and non-reproducible state. Owners push bounded inputs
through AudioFrontend instead.

### Compile or patch the graph incrementally on the callback

Rejected because type checking, allocation, resource resolution and topology
changes are fallible and unbounded. Complete plans are built off-thread and swapped
only at a generation boundary.

### Identify parameters and nodes by display strings

Rejected because renames, localization, vendor naming and duplicate labels would
change identity and break saved mappings. Stable typed IDs own runtime references.
