# ADR-065: Mixer Topology and Constrained DAG

- **Status**: Proposed
- **Date**: 2026-09-02
- **Supersedes**: None
- **Scope**: Mixer bus hierarchy, sends and returns, feedback policy, deterministic processing order, graph compilation, publication, and retirement
- **Issue**: [AUD-004.1](https://github.com/abdullahbodur/horo-engine/issues/562)
- **Jira**: [HORO-562](https://horo-engine.atlassian.net/browse/HORO-562)
- **Parent**: [AUD-004](https://github.com/abdullahbodur/horo-engine/issues/561)
- **Related**: [ADR-062](062-audio-runtime-ownership-and-update-order.md), [ADR-063](063-audio-sample-format-and-channel-layout.md)
- **Normative document**: [Audio Architecture](../architecture/runtime/audio-architecture.md)

## Context

Audio Architecture requires named buses, routing, sends, effects, off-thread graph
preparation, and callback-safe swaps, but it does not define which topologies are
legal or how two equivalent mixer assets produce one processing order. A bus tree
alone cannot represent shared reverbs or submix sends. An unconstrained graph can
contain feedback, require allocation during rendering, or make output depend on
authoring order, registration order, or unordered-container iteration.

Mixer assets, editor projections, the control runtime, and the callback also have
different lifetimes. If the callback validates or rebuilds a graph, it can exceed
its deadline or observe partial authoring state. If editor widgets mutate the live
graph, graph ownership and rollback become ambiguous. The system needs one legal
routing model and one generation-safe compilation and publication path before the
persisted schema, compiler, DSP nodes, automation, latency, tails, and hot reload
are implemented.

## Decision

### 1. Mixer topology is a constrained directed acyclic graph

The authoring model contains buses and typed directed edges. An edge direction is
signal and dependency direction: `source -> destination`. The complete admitted
graph is a DAG with one output root (the unique sink in signal direction). It
combines:

- one primary parent route from every non-Master bus;
- zero or more explicitly authored send routes;
- explicit control/read dependencies declared by DSP nodes or providers.

Each graph has exactly one bus with the `MasterOutput` role. Master has no outgoing
audio route and is the only bus delivered to the selected device/output adapter.
Every other bus has exactly one primary parent and reaches Master by following
primary routes. Primary routes therefore form a rooted tree inside the larger DAG.
Sends add cross-tree signal routes without replacing or weakening that invariant.

Bus display names such as `Music`, `SFX`, `UI`, `Voice`, and `Ambient` are template
and presentation data. A versioned stable `AudioBusId` is identity. Names, array
positions, editor tree expansion, registration order, pointer values, and hashes
with process-randomized seeds never determine routing or processing order.

### 2. Sends and returns are explicit routes and buses

A send is a typed edge with its own stable `AudioRouteId`, source and destination
bus IDs, tap point, gain/automation identity, and enabled state. Baseline tap
points are:

```cpp
enum class AudioSendTap : std::uint8_t {
    PostInsertPreFader,
    PostFader,
};
```

The source bus primary route always uses its post-fader output. Routing uses a
pull model: a send exposes the selected immutable source tap, and the destination
reads and accumulates it only when that destination executes. It does not transfer
buffer ownership or mutate the source tap. Send gain is applied exactly once while
the destination accumulates that edge. An unknown tap, non-finite gain, duplicate
route ID, missing endpoint, or unsupported layout conversion is a graph validation
error.

A return is an ordinary bus with a typed `Return` role, an effect chain, one
primary route toward Master, and incoming sends. It is not a hidden paired object
owned by a source bus. Direct voice routing to a Return bus is rejected in the
baseline contract, keeping return inputs explicit. Multiple sends may target the
same return, and one source may send to multiple returns when the combined graph
remains acyclic and within budgets.

### 3. Feedback is rejected in the baseline

The graph compiler rejects every cycle across primary routes, sends, and declared
same-block control/read dependencies. This includes self-sends, a send from an
ancestor into a descendant, indirect send/return loops, sidechain dependencies
that close a cycle, and a route that attempts to leave Master. Adding a positive
gain below unity, muting a route, or disabling it only at runtime does not make a
cycle legal; enabled topology is resolved and validated before compilation.

DSP nodes may contain private bounded history required by their declared algorithm,
such as filter state or a finite delay/reverb line. That state is not permission
to route graph output back to graph input. A future feedback capability would need
an explicit delay node, a minimum delay of at least one processing block, bounded
state and gain policy, latency/tail semantics, migration, and separate acceptance.
No implicit one-block delay or backend-specific feedback is admitted by this ADR.

An optional route is either absent from the compiled topology or present and
validated. The callback cannot toggle an edge in a way that changes reachability
or creates a graph the compiler never admitted. Topology-changing enable/disable
requests compile a new generation.

### 4. One canonical processing order

The compiler operates on canonical stable IDs and constructs one deterministic
topological order. It uses Kahn's algorithm over the complete dependency graph;
when multiple buses are ready, the smallest canonical encoded `AudioBusId` is
selected. This stable-ID tie break is part of the graph schema/compiler version.
Equivalent graphs produce the same order regardless of file order, editor action
history, map iteration, job completion, provider registration, or machine.

Incoming audio edges for a bus are accumulated in ascending canonical
`AudioRouteId`, with the primary child routes and sends represented by distinct
typed route IDs. Direct voices are accumulated in stable admitted voice-slot order,
including generation as the validity check. DSP descriptors execute in their
persisted stable chain order. Implementations may vectorize or parallelize only
when they preserve the declared accumulation result and dependency barriers within
the documented numerical tolerance; scheduling completion order is never mix order.

For each processing block, a bus executes:

1. clear its preallocated accumulator to ADR-063 positive-zero silence;
2. pull and accumulate admitted direct-voice output slots, then the immutable
   source taps of incoming routes in canonical order, applying each route gain;
3. run its prepared insert chain in stable descriptor order;
4. publish the immutable `PostInsertPreFader` tap;
5. apply bus gain, mute, and bounded sample-safe automation to produce post-fader;
6. publish the immutable post-fader tap used by primary and post-fader send routes;
7. for Master only, convert the post-fader output to the output adapter.

The compiled order guarantees every source is complete before its destination
executes. A non-Master bus never writes a destination accumulator. The compiler
assigns retained tap storage and may alias a tap with working storage only when its
complete read lifetime proves that later fader/DSP writes cannot change the
published samples. Every destination clears before pulling, so no global clear
pre-pass is required and already published source data is never erased. Bus pause
semantics act on admitted voice/automation clocks according to their separate
typed policy; pause never changes graph topology or processing order. Solo and
editor preview remain projection/debug policy and do not alter the cooked graph
unless an explicit runtime profile compiles their effective routes.

### 5. Layout, latency, and resource constraints are graph inputs

Each bus and route carries an ADR-063 semantic layout. Equal channel counts are
not proof of compatibility. Every route either has matching layouts or names one
admitted prepared conversion operation. The compiler rejects an implicit downmix,
speaker/Ambisonic reinterpretation, missing conversion, unsupported sample rate,
or capacity beyond the selected audio profile.

Before publication, the compiler proves bounded limits for buses, routes, fan-in,
fan-out, effects, processing blocks, scratch, retained state, and per-block work.
It also computes explicit route/node latency and required compensation under the
future latency contract. Until that contract is implemented, a graph requiring
undeclared compensation fails rather than silently misaligning parallel paths.
Unbounded DSP history, scratch, tail, or callback work is never admitted.

### 6. Control owns compilation; callback owns one immutable generation

The Audio control runtime is the sole owner of graph build requests, accepted
authoring revisions, compiler jobs, publication, acknowledgements, and retirement.
The editor, gameplay, asset reload, and extensions submit typed requests; they do
not mutate callback state. A build pins an immutable MixerAsset snapshot, Audio
profile, DSP/provider catalog snapshot, sample format, and compiler/schema versions.

Compilation and every fallible operation occur outside the callback:

1. validate stable identity, root/tree rules, endpoints, topology, layouts, and
   profile limits;
2. construct the canonical dependency graph and deterministic order;
3. prepare DSP/conversion state, fixed buffers, scratch offsets, route tables,
   automation slots, latency data, and declared tail/migration policy;
4. produce one owned immutable `MixerRenderPlan` generation;
5. discard the result if its source revision or pinned catalog/profile became
   stale before publication;
6. enqueue a bounded `SwapMixerGraph` command naming the complete generation.

The callback adopts a prepared generation only at a processing-buffer boundary
and emits a bounded acknowledgement with request and generation IDs. It performs
no allocation, graph traversal for validation, topological sort, provider lookup,
file access, logging, or authoring migration. A build, preparation, queue, or swap
failure leaves the last good generation active and returns a typed diagnostic.

Parameter changes that fit existing prepared slots use bounded real-time commands
and do not rebuild topology. Adding/removing/reparenting a bus or route, changing
an effect chain or layout, or enabling a topology-affecting route requires a new
compiled generation. Coalescing never crosses a graph-generation boundary.

### 7. Swap and retirement are generation safe

Every voice, bus handle, automation slot, meter, and queued parameter command is
validated against the target graph generation or a compiler-produced stable-ID
remap. Missing or type-incompatible IDs fail explicitly; array offsets from the
old plan are never reused as identity.

The control runtime retains the new plan until callback adoption is acknowledged.
It retains the old plan and all provider/DSP owner leases until the callback has
acknowledged that no render epoch references it and any explicitly compiled bounded
tail/migration plan has completed or been cancelled by declared policy. Editor
document replacement, package disable, device recovery, and shutdown cannot unload
code or free buffers visible to either generation.

Only one ordered swap sequence is active per callback epoch. A newer build may
supersede a queued but unadopted build through control-owned cancellation, but it
cannot free a generation already visible to the callback. Device reset recompiles
or revalidates against the negotiated format before publication; it does not
silently reinterpret an old plan.

### 8. Diagnostics and extension boundary

Validation returns typed diagnostics containing the mixer asset/revision,
offending stable bus/route/node IDs, error category, and bounded path evidence.
Cycle diagnostics report one canonical cycle path rotated to the smallest stable
ID, so equivalent invalid graphs produce equivalent evidence. User-visible labels
may be attached as presentation context but never replace IDs.

DSP and spatial provider descriptors declare their audio/control dependencies,
layouts, latency, scratch, state, tail, and real-time guarantees before compilation.
They cannot add hidden routes during `Process()`, call another bus, query the live
registry, retain authoring views, or bypass the plan's order. Registration order
does not resolve descriptor conflicts or processing order. Package removal waits
for all graph-generation leases under ADR-062 lifecycle ownership.

### 9. Migration and verification

Audio Architecture summarizes this decision and does not retain an unconstrained
"routing" promise. AUD-004.2 and later children define the persisted MixerAsset,
compiler, core DSP, automation, latency, tails, reload, editor surfaces, and tests.
Those schemas must project onto this topology; they cannot add a second graph or
callback-side builder.

Required contract coverage includes:

- zero/multiple Master, orphan bus, missing/duplicate IDs, missing endpoints,
  duplicate routes, illegal Return input, and route-from-Master rejection;
- self, primary-parent, send/return, ancestor-to-descendant, indirect, and
  same-block control/sidechain cycle fixtures with canonical cycle evidence;
- byte-identical compiled order/route tables across source order, editor history,
  unordered-map seed, registration order, job completion order, locale, and host;
- canonical direct-voice, incoming-route, insert, pre/post-fader send, primary,
  and Master output order using numerical reference signals, including proof that
  destination clearing cannot erase an earlier source and tap aliasing cannot
  mutate a published pre-fader signal;
- layout mismatch, missing conversion, fan-in/fan-out, scratch, state, latency,
  tail, and callback-work budget failures before publication;
- stale build cancellation, failed prepare, queue saturation, superseded build,
  atomic buffer-boundary swap, last-good retention, and generation acknowledgement;
- old-generation voice/command/meter handling, stable-ID remap, effect/provider
  lease retirement, package removal, device reset, and repeated shutdown;
- callback allocation, lock, logging, I/O, registry lookup, validation, and
  topological-sort guards.

## Consequences

The mixer can express submix trees and shared send/return effects while every
admitted graph has a bounded deterministic order. Unsupported feedback cannot
reach the callback, and authoring/editor state remains separate from immutable
runtime generations. Stable IDs and canonical route order make diagnostics,
tests, cooking, Null rendering, and cross-platform behavior reproducible.

The cost is stricter authoring validation, explicit route/tap/layout identities,
an off-thread compiler, precomputed storage and order, and generation-aware swap
and retirement. Some common audio-tool feedback techniques require a later
explicit delayed-feedback capability rather than working accidentally.

## Rejected Alternatives

### Restrict the mixer to a bus tree

Rejected because shared reverbs, parallel effects, and submix sends require
cross-tree routes. A constrained DAG retains those uses without admitting cycles.

### Allow arbitrary cycles and rely on processing order

Rejected because output would depend on buffer mutation order and could create
unbounded or unstable feedback. Baseline cycles fail before publication.

### Insert an implicit one-block delay for every cycle

Rejected because it hides latency and gain policy, changes authored sound, and
makes block size part of undocumented semantics. Delayed feedback needs its own
typed, versioned capability.

### Use authoring array order as processing order

Rejected because editor actions, migration, serialization, and merge resolution
can reorder arrays without changing topology. Stable-ID topological order is the
authority.

### Rebuild or patch routing directly on the callback

Rejected because validation, allocation, provider preparation, and sorting are
not bounded real-time work, and partial patches cannot provide atomic rollback.

### Let each backend define its own mixer graph

Rejected because routing, DSP order, diagnostics, and Null tests would diverge.
Backends consume only Master output through the Horo-owned plan and ADR-063 adapter.
