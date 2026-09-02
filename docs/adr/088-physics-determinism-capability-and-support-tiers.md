# ADR-088: Physics Determinism Capability and Support Tiers

- **Status**: Proposed
- **Date**: 2026-09-02
- **Supersedes**: None
- **Scope**: Physics determinism tier names, support status, exact execution tuple, prerequisites, included outputs, exclusions, capability negotiation, build/FP policy, queries/events, streaming/reload, rollback boundary, evidence, lifecycle and qualification
- **Issue**: [PHY-007.1](https://github.com/abdullahbodur/horo-engine/issues/899)
- **Jira**: [HORO-899](https://horo-engine.atlassian.net/browse/HORO-899)
- **Parent**: [PHY-007](https://github.com/abdullahbodur/horo-engine/issues/834)
- **Related**: [ADR-008](008-error-model-exception-boundary-and-registry.md), [ADR-023](023-world-index-and-cell-format-architecture-decision.md), [ADR-026](026-large-world-precision-and-floating-origin-strategy.md), [ADR-061](061-animation-ownership-update-order-and-clock.md), [ADR-084](084-canonical-physics-solver-units-and-tolerances.md), [ADR-085](085-physics-shape-authoring-cook-and-runtime-boundary.md), [ADR-086](086-collision-layer-profile-and-query-channel-policy.md), [ADR-087](087-scene-to-physics-ownership-and-conversion.md), [ADR-092](092-character-controller-determinism-and-state-composition.md)
- **Normative documents**: [Physics Architecture](../architecture/runtime/physics-architecture.md), [Runtime Lifecycle](../architecture/runtime/runtime-lifecycle.md), [Scene Runtime](../architecture/runtime/scene-runtime.md), [Coordinate Precision and Origin Rebasing](../architecture/runtime/coordinate-precision-and-origin-rebasing.md), [Metrics and Profiling](../architecture/observability/observability-performance.md)
- **Upstream references**: [Jolt v5.6.0 deterministic simulation](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Docs/Architecture.md#deterministic-simulation), [Jolt v5.6.0 build defines](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Build/README.md#defines)

## Context

ADR-084 deliberately rejects a universal cross-platform bit-identical claim. It
pins one solver/profile and says reproducibility depends on build, compile/FP/job
configuration, fixed delta, content and ordered commands. That tuple now needs a
product-facing capability model: a project, network/session host, replay harness or
test cannot safely request a boolean named `deterministic` without knowing which
runs are claimed compatible and which outputs are compared.

Jolt v5.6.0 documents same-binary determinism when modifying APIs are invoked in
the same order. It offers `JPH_CROSS_PLATFORM_DETERMINISTIC`, while also requiring
identical source/defines and application-side precise FP behavior, consistent FPU
state, portable math/container algorithms and stable ordering. It explicitly notes
that broadphase queries, query result order, listener callback order and active-body
enumeration need extra normalization. Upstream capability is necessary evidence,
not a Horo product guarantee.

The rest of Horo can invalidate a solver-only claim. Scene/prefab expansion,
streaming completion, origin shifts, structural command insertion, gameplay math,
container iteration, event canonicalization, asset/profile generations and reload
must agree at every tick. A deterministic solver fed commands in different order is
not a deterministic engine. Debug/Release, sanitizer, compiler, standard library,
ISA and FP contraction differences can also change results even on one OS.

Lockstep and rollback are different promises. Repeatable forward simulation does
not provide a complete state snapshot, restore structural history, define network
authority or recover from a late input. PHY-007.1 names capability tiers only;
PHY-007.2 owns stable ordering/seeds, PHY-007.3 snapshots, PHY-007.4 rollback hooks,
PHY-007.5 replay/hash/divergence tooling and PHY-007.8 final qualification.

## Decision

### 1. Determinism is an explicitly negotiated capability

Horo exposes one typed tier enum and immutable capability result:

```cpp
enum class PhysicsDeterminismTier : std::uint8_t {
    Unspecified,
    SameMachineDiagnostic,
    SameBuildSamePlatform,
    CrossPlatformQualified,
};

struct PhysicsDeterminismCapability {
    PhysicsDeterminismTier maximumQualifiedTier;
    PhysicsDeterminismFingerprint fingerprint;
    DeterminismCompatibilityGroupId compatibilityGroup;
    DeterminismExclusionSet exclusions;
    QualificationEvidenceId evidence;
};
```

The values are ordered by strength for admission, not interchangeable modes that
can be toggled after world creation. The host requests a minimum tier before scene/
world activation. Physics returns the effective supported capability for the exact
run or a typed failure. A config checkbox, command-line flag or upstream build
define cannot elevate `maximumQualifiedTier` without matching evidence.

The capability is immutable for one process/world generation. If build, Physics
profile, package/content, command protocol, platform or another fingerprint input
changes, the host must negotiate a new world/session/replay compatibility result.

### 2. Tier 0: `Unspecified` makes no repeatability claim

`Unspecified` is the ordinary fallback capability. The runtime remains correct and
must satisfy ownership, safety, fixed-step and error contracts, but two runs may
diverge. It is the only tier available when required fingerprint/evidence is absent
or an excluded feature/path participates.

An `Unspecified` world may still use a fixed timestep and produce similar results.
That is not a determinism guarantee. It cannot be used as proof for lockstep,
rollback resimulation, authoritative replay verification or cross-run state hashes.

Unsupported platform/profile, noncanonical structural mutation, variable-step
Physics, unsorted concurrent commands, unqualified internal parallelism, hot native
code replacement, nondeterministic custom callbacks or non-finite state force this
tier or fail a stronger requested capability before activation.

### 3. Tier 1: `SameMachineDiagnostic` is a development check

`SameMachineDiagnostic` compares repeated execution on the same machine, exact
executable/modules, process launch configuration, content and input recording. It is
intended for local double-step/rewind checks, replay harness development and first-
divergence diagnosis.

It requires a fresh equivalent world or complete typed restore, identical FPU state
on every participating thread, fixed tick/order/seed and no excluded path. It may
compare exact canonical state/event hashes for covered fixtures.

This tier is not a shipping compatibility promise across machines or application
builds. A pass can miss CPU/platform/toolchain or long-horizon differences and does
not satisfy `SameBuildSamePlatform` qualification by itself.

### 4. Tier 2: `SameBuildSamePlatform` is Horo's 1.0 support target

`SameBuildSamePlatform` means two runs use:

- byte-identical shipped executable and Physics/gameplay modules;
- one qualified OS/architecture/ABI/ISA/CPU-capability platform class;
- identical determinism fingerprint, content manifest and ordered input/command
  frames;
- identical initial canonical Horo Physics state and tick/origin schedule.

Within that tuple, Horo claims exact canonical Physics checkpoint hashes and exact
discrete Physics outputs for the included contract over the qualified workload and
horizon. Different machines within the qualified platform class are admitted only
when PHY-007.8 evidence covers them. CPU brand/model alone is not identity, but ISA,
FP environment and documented capability class are.

This tier is the planned required 1.0 Physics determinism tier. It is not advertised
as qualified merely by this ADR. Until PHY-007.2, PHY-007.3, PHY-007.5 and
PHY-007.8 gates pass for an exact tuple, the runtime reports at most
`SameMachineDiagnostic` or `Unspecified`.

Debug, Release, Distribution, sanitizer, LTO, compiler/standard-library revision or
Physics module code changes produce different builds unless byte-identical evidence
proves the participating deterministic modules and tuple unchanged. Two builds on
the same OS are not automatically Tier 2 compatible.

### 5. Tier 3: `CrossPlatformQualified` is future and opt-in

`CrossPlatformQualified` groups two or more explicitly listed platform/build tuples
under one immutable `DeterminismCompatibilityGroupId`. Each member must use:

- the same pinned Jolt/Horo Physics source and semantic algorithms;
- `JPH_CROSS_PLATFORM_DETERMINISTIC` enabled and recorded;
- matching Jolt/Horo compile definitions and float/double profile;
- precise FP, contraction disabled, consistent round-to-nearest and qualified
  denormal/flush behavior on all participating threads;
- platform-portable Horo math, sorting, heap and hash operations in the complete
  command/content/state path;
- pairwise replay/hash evidence for every advertised group member.

CanonicalV1's normal shipping build keeps cross-platform determinism disabled and
does not claim Tier 3. A future `CrossPlatformDeterministicV1` profile is a separate
Physics build/artifact/capability with its own performance, platform, content-cook,
query, snapshot and regression qualification. Enabling the upstream define locally
does not join a compatibility group.

Tier 3 remains unsupported/future until an accepted profile and PHY-007.8 evidence
exist. A product can therefore require Tier 2 without paying the future Tier 3
profile cost.

### 6. One exact determinism fingerprint defines compatibility

`PhysicsDeterminismFingerprint` is a versioned Horo digest over canonical fields:

- Horo engine/product build IDs and hashes of participating deterministic modules;
- Jolt tag/commit, source patch digest and every compile definition;
- compiler, standard library, target triple, ABI, build mode, codegen/ISA/CPU
  capability, sanitizer/LTO and FP flags;
- runtime FPU environment policy and Physics owner/private job profile;
- Horo Physics schema, scene-plan, ordering, seed, state-snapshot/hash, shape cooker,
  filter, material, tolerance/solver and origin-rebase algorithm versions;
- fixed timestep rational, substep/iteration policy and capacity settings that can
  affect ordering/behavior;
- exact cooked scene/shape/material/filter/package manifest fingerprints;
- initial world/state snapshot digest and ordered input/command protocol version;
- admitted feature/exclusion bits and compatibility-group identity.

Display names, file paths, timestamps, process IDs, pointers, renderer/audio state
and observability sampling are excluded unless they feed simulation, which is itself
a contract violation. Fields use fixed encodings/order and checked length. Unknown
required fields/versions fail comparison; no partial or string-map comparison is
allowed.

Two Tier 2 runs require identical complete fingerprint bytes. Tier 3 uses distinct
member fingerprints plus one reviewed group manifest that explicitly declares which
allowed platform/build fields differ and proves semantic compatibility. A group ID
is never computed from a vague version range.

### 7. Initial state and every mutation are canonical inputs

Determinism starts from ADR-087's canonical Physics scene plan and complete
published world generation. Bodies, collider/shape/material/filter bindings and
constraints are created in stable authored-ID order. Private native IDs may differ
only if the Horo adapter proves they cannot affect included semantics; CanonicalV1
otherwise assigns them deterministically.

Every simulation-affecting operation enters one tick-indexed Physics command frame
with stable command kind, target Horo IDs, source sequence and canonical tie-break.
PHY-007.2 defines the exact ordering and seed policy. Process event order, worker
completion, pointer/order of unordered containers, wall time and callback arrival
never determine mutation order.

Commands include body/constraint add/remove/change, forces/impulses, kinematic
targets, controller/root-motion inputs, material/filter/shape replacement, origin
shift, streaming cell commit/retire and deterministic gameplay Physics actions. A
late/duplicate/missing command or generation mismatch is a replay/session error, not
silently reordered best effort.

Randomness uses named Horo-owned streams with recorded algorithm/version/seed and
stable consumption ownership. Solver/gameplay code cannot draw from process-global,
time-seeded or thread-scheduled random sources.

### 8. The included output contract is explicit

For qualified Tier 2/3 runs, exact comparison includes:

- canonical Physics state snapshot/hash fields defined by PHY-007.3/.5, ordered by
  stable Horo world/body/constraint identity;
- body pose, linear/angular velocity, sleep/activation, admitted material/filter/
  shape/body mode and constraint state included by that schema;
- discrete contact/overlap/activation results after Horo canonicalization, including
  stable identities, kind and deterministic ordering;
- transform writeback and Physics-owned gameplay outputs committed for the tick;
- command acceptance/rejection result and stable error code;
- origin generation and exact integer global/local-rebase command evidence.

Exact means canonical encoded bits/integers/enums/IDs, not a broad floating-point
epsilon. Tolerance-aware comparisons may be additional quality metrics, but they
cannot make an exact tier pass after the canonical hash diverges.

Private allocator addresses, native IDs, broadphase tree shape, lock schedule,
profiling time, cache hit rate and raw callback order are not compared as outputs.
They must still not influence any included output.

### 9. Queries and callbacks are excluded unless normalized

Raw Jolt broadphase query traversal is not deterministic evidence. Horo's qualified
query path must revalidate candidates against exact body/world bounds where
required, canonicalize filters and sort/cap results by the PHY-004 stable ordering
contract. Narrowphase result arrival order is never exposed directly.

Only a query whose descriptor, captured world/schema generation, execution tick,
staleness policy and complete canonical result are part of the deterministic
command/output protocol may influence simulation in Tier 2/3. Ad hoc debugger,
editor hover, asynchronous stale snapshot and presentation queries are excluded and
must not feed authoritative gameplay.

Native contact/activation/step listeners copy bounded evidence only. Horo sorts and
reduces it after the step using stable body/subshape/material identities. Callback
thread/order, `GetActiveBodies` order and native collector order never determine
events or gameplay commands.

Overflow is deterministic state: the same bounded capacity must overflow at the
same canonical item, produce the same typed result and normally invalidates a
session that required complete authoritative events. Dropping whichever callback
arrived last is forbidden.

### 10. The fixed-step and FP environment are part of the capability

Physics consumes one exact rational fixed delta and configured maximum catch-up
policy. Variable render delta, wall clock, presentation interpolation, pause GUI,
audio/network arrival time and profiler instrumentation do not enter the step.
Inputs are assigned to explicit simulation ticks before the deterministic command
frame closes.

Every thread that performs included Physics/gameplay math installs/verifies the
qualified FPU state before work and restores host policy on exit where required.
Unexpected rounding/denormal state fails the stronger capability or the run; it is
not corrected silently after results exist.

Tier 2 pins all codegen/FP settings through the build fingerprint. Tier 3 additionally
forbids nonportable standard transcendental/sort/heap/hash behavior in the included
path and uses reviewed Horo/Jolt deterministic alternatives. Fast-math, unrecorded
FMA contraction or target-native ISA selection outside the profile is incompatible.

### 11. CanonicalV1 remains serial; parallelism needs a new tuple

ADR-084's initial Physics profile uses one owner thread and serial private job
adapter. This minimizes ordering dimensions and is the only profile eligible for
the first Tier 2 qualification.

Internal parallel stepping, concurrent world mutation or multithreaded gameplay
command generation cannot inherit the serial evidence. A future parallel profile
must define stable task/merge ordering, callback/event reduction, FPU installation,
race-free state and cancellation, then qualify its own determinism fingerprint.

Concurrency outside the included path is permitted only when it publishes immutable
results through a tick-indexed canonical commit. Worker completion order cannot
choose which command, asset generation or streaming cell reaches a tick.

### 12. Streaming, origin shifts, reload and packages are conditional inputs

World/cell streaming is deterministic only when the exact content manifest, cell
request/commit/retire tick and stable cell/provider ordering are recorded inputs.
I/O/job completion can make a candidate ready but cannot choose its authoritative
commit tick. Missing readiness at the recorded tick fails/stalls according to a
declared session policy; it does not commit on the next arbitrary frame.

Origin rebasing uses ADR-026 integer global identity and a canonical tick-indexed
shift vector. All participants commit the same shift at one safe point. Camera-
driven variable-frame threshold checks must first become an authoritative recorded
Physics command before affecting a deterministic run.

Hot scene/component/shape/filter/material/package/native-code reload changes the
fingerprint or initial/command evidence. A replay may include a versioned exact
replacement command only when both sides have identical immutable candidate bytes
and algorithm support. Otherwise the stronger capability ends before reload and a
new world/session begins.

### 13. Determinism does not imply rollback, lockstep or durable native state

Tier 2/3 is a prerequisite for some replay/network strategies, not authorization to
use them. Rollback additionally requires PHY-007.3's complete Horo state snapshot,
PHY-007.4 structural restore/resimulation hooks, retained input/command history,
side-effect suppression/reconciliation and a Network authority/protocol decision.

Jolt `SaveState`/`RestoreState`, `StateRecorder`, BodyIDs and native binary state may
be used privately for diagnostics or a qualified adapter implementation. They are
not durable Horo save/network formats and cannot cross a fingerprint or solver
upgrade. Horo must capture every non-solver state field and structural operation
that can affect replay.

A deterministic forward run can still be unsuitable for client prediction because
of latency, content availability, snapshot size, external gameplay state or side
effects. Products default to authoritative server/local simulation unless their
separate networking/rollback contracts are met.

### 14. Stronger capability failure is fail-closed and observable

Requesting Tier 2/3 returns a typed admission failure for unknown/unqualified tuple,
fingerprint/group mismatch, missing evidence, unsupported feature/path, FP state,
content/initial-state/command divergence, event overflow or stale generation.
Physics never silently starts an `Unspecified` world when the product/session
required a stronger tier.

During a run, invariant failure transitions the determinism session to `Diverged`
or `Invalid` at the first known tick. It records bounded first-divergence evidence,
stops producing authoritative replay/lockstep claims and follows the product's
explicit disconnect/fallback/fatal policy. It cannot relabel already divergent
state as deterministic after a resync.

Diagnostics include tier, fingerprint/group/evidence IDs, tick, stable world/body/
command identities, expected/actual canonical hashes, first differing state field
when available and exclusion/reason code. They never dump native state, raw content,
secrets or unbounded worlds by default.

### 15. Evidence is immutable, tuple-specific and release-gated

`QualificationEvidenceId` resolves to a signed/versioned release artifact containing:

- exact source/build/toolchain/defines/FP/platform/CPU/content fingerprints;
- tier and compatibility-group manifest;
- test corpus versions, seeds, command recordings and expected checkpoint hashes;
- machines/runners, OS/CPU/ISA classes and run counts;
- tick horizons, state/event/query/streaming/origin/rollback coverage;
- first-divergence and determinism-log tooling versions;
- performance/memory impact and known exclusions;
- pass/fail results, review approval, expiry/supersession and rollback build.

Evidence is not a mutable dashboard row or "tests passed" flag. Any fingerprinted
input change invalidates or requires an explicit reviewed equivalence decision and
new evidence. Upstream CI/docs inform review but do not substitute for Horo's full-
stack corpus.

Release/package/session admission verifies that the requested evidence matches the
shipped bytes. Developer overrides can run experiments but report
`SameMachineDiagnostic`/unqualified and cannot stamp shipping manifests.

### 16. Qualification coverage is part of the contract

PHY-007.8 must cover, for every Tier 2 tuple and every pair in a Tier 3 group:

- clean-process/fresh-world repeated runs on representative CPU vendors/models;
- exact scene plan, body/shape/filter/material/constraint creation and native ID
  stability under different source list order;
- fixed-tick input/command ordering, duplicate/late/missing commands and seeds;
- sleeping/islands, CCD, contacts/triggers, friction/restitution, constraints,
  character/controller integration and body add/remove/replacement;
- canonical event/callback reduction and complete overflow behavior;
- qualified immediate/gameplay queries with randomized native arrival order and
  canonical result order;
- streamed cell ready/commit/retire ticks, cancellation, origin rebase and scene
  replacement at recorded boundaries;
- state capture/hash/restore and long-horizon replay under PHY-007.3/.5;
- FP environment perturbation detection on owner/workers and every build/ISA mode;
- active candidate/reload/package mismatch and stronger-tier fail-closed admission;
- headless and interactive composition equivalence with Renderer/audio excluded;
- maximum supported bodies/constraints/events/commands without hidden allocation-
  order effects;
- fault injection after every lifecycle phase, stale completion and shutdown.

Tier 2 uses the exact shipped binary on every covered machine in its platform
class. Tier 3 runs identical recordings pairwise across every group member and
compares checkpoint/state/event outputs at each declared horizon, not only the final
pose or a tolerance image.

Fuzz/property tests vary valid command order before canonicalization and assert one
canonical frame; invalid/unordered inputs must fail consistently. A mismatch stores
bounded first-divergence artifacts and fails the release gate.

### 17. Lifecycle and ownership are explicit

The build/release pipeline owns qualification evidence publication. The application
composition root owns capability negotiation. Each `PhysicsWorld` owns its immutable
effective tier/fingerprint/group/evidence lease. The Physics owner thread owns
command-frame closure, checkpoint/hash production and divergence state.

Replay/network/gameplay consumers borrow typed capability snapshots; they cannot
mutate or upgrade the world tier. Observability consumes bounded copies and cannot
feed simulation. Evidence/package disable or process shutdown closes new session
admission, drains recordings/hash jobs, destroys worlds and releases evidence after
all consumers.

Scene/world replacement negotiates the candidate capability before publication.
Failure preserves the active world under ADR-087. A successful replacement creates
a new determinism session/fingerprint even when the tier and scene identity appear
unchanged. Repeated stop/shutdown and divergence teardown are idempotent.

## Consequences

Horo can make narrow, testable repeatability claims without turning an upstream
solver option into a universal product promise. Shipping 1.0 targets exact same-
build/same-platform replay; cross-platform compatibility remains a separately built
and evidenced future profile. Consumers can fail closed before relying on an
unqualified world.

The cost is strict fingerprinting, canonical ordering/state/output schemas, per-
tuple evidence and conservative invalidation after build/content/profile changes.
Determinism-sensitive products cannot use arbitrary callbacks, hot reloads,
asynchronous commit timing or gameplay queries without bringing them into the
recorded/canonical contract.

## Rejected Alternatives

### Expose one `deterministic=true` option

Rejected because it cannot say which builds/platforms/outputs are compatible or
whether evidence exists. Negotiation uses typed tiers and exact fingerprints.

### Claim cross-platform determinism because Jolt provides a build define

Rejected because Horo gameplay/order/math/query/event/streaming paths and exact
platform tuples must also qualify. The upstream option is one future Tier 3 input.

### Treat fixed timestep as sufficient evidence

Rejected because command order, seeds, codegen/FP, content, callbacks and state
normalization can diverge while the delta remains fixed.

### Compare continuous state with a broad epsilon

Rejected because tolerance can hide the first divergence and later discrete
behavior changes. Qualified tiers compare canonical exact checkpoint state and
track tolerance metrics separately.

### Include raw query/callback/native enumeration order

Rejected because upstream documents nondeterministic traversal/callback order.
Horo revalidates, canonicalizes and sorts the admitted authoritative outputs.

### Make determinism automatically imply rollback networking

Rejected because rollback also needs complete Horo state/structural restore,
history, side-effect and network authority contracts.

### Preserve a tier across hot reload or fingerprint mismatch

Rejected because the proof applies to exact code/content/algorithm inputs. A new
candidate negotiates a new determinism session or fails closed.
