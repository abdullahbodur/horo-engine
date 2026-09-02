# ADR-084: Canonical Physics Solver, Units and Tolerances

- **Status**: Proposed
- **Date**: 2026-09-02
- **Supersedes**: None
- **Scope**: Initial canonical 3D physics solver, dependency pinning, Horo/native ownership boundary, world units, coordinate and matrix conventions, precision mode, numeric tolerance profile, scale envelope, fixed-step and jobs, platform support, licensing, determinism, serialization, observability, upgrade policy, unsupported cases, and qualification
- **Issue**: [PHY-001.1](https://github.com/abdullahbodur/horo-engine/issues/837)
- **Jira**: [HORO-837](https://horo-engine.atlassian.net/browse/HORO-837)
- **Parent**: [PHY-001](https://github.com/abdullahbodur/horo-engine/issues/828)
- **Related**: [ADR-005](005-submodule-compatibility.md), [ADR-008](008-error-model-exception-boundary-and-registry.md), [ADR-010](010-job-waiting-and-operation-store-ownership.md), [ADR-026](026-large-world-precision-and-floating-origin-strategy.md)
- **Normative documents**: [Physics Architecture](../architecture/runtime/physics-architecture.md), [Runtime Lifecycle](../architecture/runtime/runtime-lifecycle.md), [Scene Math](../architecture/foundation/scene-math.md), [Coordinate Precision and Origin Rebasing](../architecture/runtime/coordinate-precision-and-origin-rebasing.md), [Developer Environment](../architecture/delivery/developer-environment.md)
- **Upstream references**: [Jolt Physics](https://github.com/jrouwe/JoltPhysics), [Architecture and conventions](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Docs/Architecture.md), [Release notes](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Docs/ReleaseNotes.md), [API changes](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Docs/APIChanges.md), [MIT license](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/LICENSE)

## Context

Horo's Physics architecture already assigns each active runtime scene a physics
world, advances it in fixed ticks, uses generation-checked Horo handles and keeps
solver-native details private. It does not choose the initial solver or define the
numeric and distribution profile that downstream body, shape, constraint, query and
character-controller contracts must assume.

A generic runtime solver registry now would freeze the wrong abstraction. Solvers
differ in shape construction, body locking, filters, job graphs, callbacks, contact
caches, serialization and precision modes. Horo needs stable semantic contracts,
not lowest-common-denominator native pass-throughs or runtime hot swapping.

The solver choice also intersects ADR-026. Global storage is integer-millimeter
`WorldCoordinate64`, while Physics executes in a local floating-origin cluster.
Units, coordinate handedness, scale envelope and tolerances must be explicit so
importers, controllers, tests and authoring tools do not compensate differently.

Jolt Physics aligns with the required right-handed Y-up/SI model, is available
under MIT, supports Horo's initial desktop toolchains and provides fixed-step rigid
bodies, constraints, queries, characters, deterministic-oriented jobs and optional
large-world position precision. Horo can adopt it privately without exposing Jolt
types or promising multiple interchangeable solvers.

## Decision

### 1. Jolt Physics v5.6.0 is the one initial canonical 3D solver

Horo pins upstream tag `v5.6.0` and commit
`e77f175595e64cb44218cc9d9d56fc365ad0e36a`. Fetch/build inputs use the immutable
commit, never `master`, a floating semver range, a system package selected by
environment, or an unverified binary.

`HoroEngine::Physics` owns the backend-neutral public API and one private Jolt
implementation target. Applications compose Physics as present or omitted; they do
not select `jolt`, `bullet`, `physx` or another string at runtime. There is no
`IPhysicsBackend`, plugin ABI, provider registry, native-device handoff or hot-swap
contract in the initial baseline.

Replacing Jolt remains possible by rewriting the private implementation against the
Horo contract and migrating artifacts/tests. It is not promised as a binary-
compatible user extension point.

### 2. Horo owns semantic identity and lifetime

The public/scene/gameplay boundary contains only Horo value types, descriptors,
generation-checked `PhysicsWorldId`, `BodyHandle`, `ShapeHandle`,
`ConstraintHandle`, filters, commands, events, queries and errors. It never exposes
`JPH::PhysicsSystem`, `BodyID`, `Shape`, `Ref`, `RVec3`, `Quat`, `JobSystem`,
listeners, allocators, locks or serialized binary state.

One scene-owned `PhysicsWorld` privately owns its Jolt system, body/shape/
constraint maps, broadphase/filter adapters, contact listener, temporary allocator,
job adapter and step buffers. Horo handles map to native IDs under world and slot
generations; stale/cross-world handles fail before native access.

Shape sharing and reference counts remain private implementation details. ECS and
gameplay never retain native pointers or borrow callback memory. World destruction
closes command/query/event admission, joins owned step work, removes native bodies,
releases shapes/constraints/listeners/allocators and destroys Jolt state before
scene component storage disappears.

### 3. Horo uses SI units and one scene coordinate convention

The canonical semantic units are:

| Quantity | Unit |
|---|---|
| position, distance, shape dimensions | meter (`m`) |
| time | second (`s`) |
| mass | kilogram (`kg`) |
| angle and angular velocity | radian (`rad`, `rad/s`) |
| linear velocity/acceleration | `m/s`, `m/s²` |
| force/impulse | newton (`N`), newton-second (`N·s`) |
| torque/angular impulse | newton-meter (`N·m`), newton-meter-second (`N·m·s`) |
| density | `kg/m³` |

Horo scene/physics space is right-handed, `+Y` up and `-Z` default forward.
Matrices are column-major and transform column vectors; transforms use the Scene
Math order. Jolt also uses right-handed Y-up and column-vector multiplication, so
the private adapter performs no handedness/up-axis reflection. Forward is a Horo
gameplay/render convention and does not become solver identity.

The default gravity is `(0, -9.81, 0) m/s²`. A world may set a finite typed gravity
vector, but changing up-axis-dependent character/vehicle contracts requires their
explicit configuration. Importers normalize foreign units/axes at the asset
boundary; Physics never guesses centimeters or applies per-body hidden scale.

Authoring UI may display alternate units, but serialized/runtime values remain SI
with explicit conversion at presentation boundaries.

### 4. Canonical v1 uses float positions in a local rebased cluster

Jolt builds with `JPH_DOUBLE_PRECISION=OFF`. Positions, rotations, velocities and
solver calculations use the solver's ordinary float profile inside the ADR-026
local physics cluster. Global persistence, streaming and replication stay in
`WorldCoordinate64`; the adapter converts only relative to the current
`OriginGeneration`.

The physics hard local half-extent remains ADR-026's default `8192 m`. The qualified
high-fidelity dynamic-contact envelope is a radius of `4096 m` about the active
physics origin. Admission outside that envelope requires an explicit world-streaming
sleep/transfer policy; it cannot silently continue as ordinary high-quality dynamic
simulation. Static geometry may span up to the scale bounds below while remaining
inside the hard cluster and broadphase budget.

Origin rebasing runs only at the declared safe point, subtracts the exact admitted
local delta from native positions/proxies and preserves velocities, impulses,
contacts and sleep state. If the Jolt API cannot shift required state atomically for
the pinned version, preparation rejects the rebase; Horo does not rebuild a subtly
different world as an invisible fallback.

Double-precision Jolt is deliberately unsupported in CanonicalV1. Enabling it
changes ABI/defines, memory/performance, query base-offset and qualification inputs
and requires a new physics profile/decision rather than a local build flag.

### 5. The admitted physical scale envelope is explicit

The CanonicalV1 authoring/runtime validation envelope is:

| Property | Baseline |
|---|---|
| ordinary dynamic characteristic length | `0.1 m .. 10 m` |
| ordinary static characteristic length | `0.1 m .. 2000 m` |
| ordinary dynamic speed | `0 m/s .. 500 m/s` |
| gravity magnitude for qualified defaults | `0 m/s² .. 20 m/s²` |
| positive mass | finite `0.001 kg .. 1.0e9 kg` |
| density | finite `0.001 kg/m³ .. 1.0e7 kg/m³` |

These bounds are validation/qualification policy, not clamping instructions.
Smaller/larger/faster content returns a typed unsupported-scale or validation
result unless a later qualified profile explicitly admits it. Static infinite
planes, astronomical bodies, relativistic/space-scale velocities, arbitrary
non-uniform dynamic scaling and zero/negative mass for dynamic bodies are outside
CanonicalV1.

Static/kinematic zero inverse mass is represented through body mode, not a fake
large mass. Ratios within an interacting island must stay within the qualified
profile; the initial recommended dynamic mass ratio is at most `1.0e4`. Validation
warns before authoring/cook and runtime rejects non-finite/unsafe values according
to the scene policy.

### 6. CanonicalV1 owns one named numeric tolerance profile

`PhysicsToleranceProfileId::CanonicalV1` fixes semantic and mapped solver values:

| Setting | Value | Meaning |
|---|---:|---|
| collision distance tolerance | `1.0e-4 m` | GJK/contact proximity baseline |
| manifold plane tolerance | `1.0e-3 m` | points admitted to one contact plane |
| speculative contact distance | `2.0e-2 m` | near-future contact generation |
| penetration slop | `2.0e-2 m` | permitted positional sinking |
| maximum correction per iteration | `2.0e-1 m` | bound on one position correction |
| warm-contact preserve distance | `1.0e-2 m` | retain prior contact impulse identity |
| point-velocity sleep threshold | `3.0e-2 m/s` | body-at-rest velocity criterion |
| time before sleep | `5.0e-1 s` | continuous eligibility duration |
| velocity solver iterations | `10` | canonical default step count |
| position solver iterations | `2` | canonical default step count |

The private adapter maps these to the pinned Jolt settings/constants and asserts the
assumed upstream defaults in adapter tests. A Horo profile explicitly overrides a
mapped setting where the native default is not settable. Upstream default changes
do not silently change CanonicalV1.

Tolerances are not a universal epsilon. Identity, handle/revision equality, layer
bits, event kinds/order, serialization and integer-millimeter coordinates use exact
comparison. Unit vectors/quaternions, query fractions, positions and velocities use
operation-specific finite checks and documented absolute plus relative tolerances.
Tests cannot replace these with one broad `epsilon` that masks drift.

Authoring values below collision tolerance or materially smaller than penetration
slop receive actionable validation; the solver does not promise meaningful contact
geometry at an arbitrary scale.

### 7. Fixed-step and threading remain Horo lifecycle policy

Physics advances only from Runtime Lifecycle fixed ticks and never measures wall
time. Canonical fixed delta is supplied by the game/runtime profile; the default
qualification rate is `60 Hz` (`1/60 s`). Substepping, CCD/motion quality and
iteration overrides are typed world/body policies whose admitted combinations must
be qualified; variable render delta never enters `PhysicsSystem::Update`.

Initial CanonicalV1 has one Horo physics owner thread. A private serial Jolt job
adapter executes all solver work synchronously within the fixed tick. This avoids a
solver-owned global thread pool and establishes ordering/lifetime before parallel
qualification.

Future internal parallelism may map bounded Jolt jobs/barriers onto the process
`JobSystem`, but it remains private and must capture only world-owned generation
leases, join before physics results publish, propagate cancellation/failure and
satisfy unload/shutdown. Gameplay/ECS access is never performed from Jolt workers.
Enabling parallelism changes the qualification profile, not the public API.

### 8. Native callbacks are captured, normalized and bounded

Contact/filter/activation callbacks execute under Jolt's lock/thread rules and may
only copy fixed/bounded native evidence into world-owned step storage. They do not
call gameplay, ECS, DataBus, Renderer, editor UI, logging sinks that may block, asset
loading or user callbacks.

After the step, the owner thread maps native IDs to generation-checked Horo entity/
body identities, canonicalizes pair/order/normal/point summaries, applies event
filters and publishes the bounded tick result. Missing/stale mappings and overflow
produce typed counters/diagnostics; they never expose native pointer lifetime.

Immediate queries run on the owner thread outside a step. Snapshot/async queries
use Horo-owned immutable acceleration/query evidence under their separate contract;
callers cannot retain Jolt collectors, locks or shape pointers.

### 9. Determinism claims are profile bounded

Horo does not claim universal cross-platform bit-identical physics. Reproducibility
identity includes engine build, pinned solver commit, all Jolt compile definitions,
compiler/architecture/FP mode, tolerance/solver profile, fixed delta, thread profile,
initial cooked scene and ordered input/command stream.

Within one qualified tuple, body/constraint insertion order, layer/filter tables,
command/event sorting, seeds and job mode are stable. Tests compare exact discrete
events/identities and declared numeric envelopes for continuous state. A native
upstream determinism statement does not override Horo's narrower qualification.

Networking does not replicate or persist opaque solver state as authoritative truth.
Projects needing rollback/lockstep require a future explicit state capture,
quantization and resimulation contract; ordinary CanonicalV1 is authoritative
server/local simulation with tolerance-aware validation.

### 10. Solver serialization is not a durable Horo format

Scene/project/save/network formats store Horo descriptors and semantic state, never
Jolt `SaveBinaryState`, native IDs, shape subtype enums, pointer graphs or raw
memory. Runtime activation reconstructs private native state from validated Horo
cooked descriptors.

Private derived collision-shape/cache artifacts, if later introduced, include exact
solver commit, compile-profile, Horo schema, target architecture, endianness and
semantic source digests. A mismatch invalidates and recooks the cache. It is not
migrated as save data or trusted across solver upgrades.

Hot reload rebuilds a candidate world by default. Any policy preserving velocity,
sleep or constraint state uses Horo typed values keyed by stable body identity and
validates them before atomic activation; it never moves native objects between
systems.

### 11. Platform support is a Horo qualification matrix

Upstream build support is necessary but not sufficient. Initial CanonicalV1 support:

| Horo target | Status | Required evidence |
|---|---|---|
| macOS 14+ arm64/x86_64, AppleClang | Qualified target | build, unit/contract, deterministic tuple, play/headless smoke |
| Windows 11 x86_64, MSVC 2022 | Qualified target | build, unit/contract, deterministic tuple, play/headless smoke |
| Ubuntu 24.04 x86_64, GCC 13+ | Qualified target | build, unit/contract, deterministic tuple, headless and display-capable smoke |
| Android, iOS, WebAssembly | Upstream-capable or planned, Horo Physics unqualified | physics-required package admission fails until a target profile and CI evidence exist |
| 32-bit desktop, RISC-V, LoongArch, PowerPC, consoles | Unsupported in initial Horo matrix | no shipped Physics component or compatibility claim |

Headless/dedicated-server Physics uses the same CPU solver and Horo contract; it has
no Renderer/GUI dependency. `PhysicsOmitted` is a valid product composition. A
project requiring Physics cannot silently substitute an omitted/null world; package
admission returns explicit unavailable/unsupported capability.

CPU code generation is pinned per qualified architecture. x86_64 baseline must not
emit instructions beyond the declared product CPU baseline; optional ISA-specific
builds require separate artifact/qualification identity. ARM64 uses its admitted
toolchain/NEON baseline. Client and library compile definitions must match exactly.

### 12. Build configuration excludes unrelated native surfaces

Only the Jolt library sources needed by Horo Physics are built. Samples,
TestFramework, JoltViewer and upstream install/examples are disabled. GPU compute,
hair and native render integrations are disabled, including Jolt DX12, Vulkan,
Metal and CPU-compute presentation/example switches. Physics never gains a Renderer
or platform graphics dependency through upstream options.

RTTI and exceptions remain disabled consistently with upstream/Horo target policy.
Custom allocation/assert/trace hooks, if enabled, are private adapters with bounded
error/observability behavior. Global Jolt registration/init occurs once through the
application composition root before any world and unregisters only after all worlds
and jobs retire; no static initialization side effects own lifecycle.

### 13. MIT licensing and notices are release inputs

Jolt is used under its MIT license. The exact upstream `LICENSE` text, name/version/
commit and required copyright notice are retained in source dependency metadata and
generated third-party notices for every shipped binary/package containing Physics.

Dependency acquisition verifies the pinned source/archive digest. License scanning
and notice generation are release gates; a missing or changed license blocks the
upgrade/package rather than silently omitting attribution. Horo's license does not
rewrite or remove upstream notices.

Optional samples/assets/test data are not shipped merely because upstream contains
them. Any future third-party content introduced through an upgrade receives its own
inventory and license review.

### 14. Solver upgrades are explicit compatibility events

An upgrade uses a dedicated reviewed change that:

1. pins a released tag and immutable commit/digest;
2. reviews upstream Release Notes, API Changes and license/dependency changes;
3. diffs all compile definitions, platform/CPU/toolchain support and defaults;
4. maps changed numeric behavior to a new or explicitly unchanged Horo profile;
5. rebuilds/invalidates every solver-derived artifact and cache;
6. runs contract, behavior, stress, fuzz, deterministic-tuple and platform matrix;
7. compares canonical scenes for contacts, sleep, CCD, constraints, characters,
   queries, rebasing, performance and memory;
8. records migration/rollback and updates third-party notices/SBOM.

A simulation-affecting change such as friction/contact/solver defaults changes the
solver fingerprint even when public C++ compilation succeeds. CanonicalV1 does not
silently inherit it. Published save/scene Horo descriptors remain readable when
their semantic schema is compatible; derived native caches recook.

Rollback restores the prior pinned source and matching caches/artifacts. Rewriting
published project/save data solely to follow an upstream native format is forbidden.

### 15. Errors, observability and failure containment are Horo-owned

Results follow ADR-008 with stable codes for unsupported platform/profile/scale,
invalid units/value/shape/filter/constraint, non-finite state, stale world/handle/
origin generation, capacity, step/job/cancellation, native invariant, upgrade/
artifact mismatch and shutdown. Native result enums/messages are mapped privately
with bounded context and never become serialized API identity.

Invalid candidate data fails before world publication. A non-finite/solver-invariant
failure during step quarantines the affected world and suppresses publication of a
partial tick; product policy chooses last-good pause, scene recovery or fatal exit.
It never continues with half-applied transforms/events.

Metrics use Horo world/profile/phase and bounded reason dimensions: step/
broadphase/narrowphase/solver time, bodies/islands/pairs/contacts/constraints,
commands/events/jobs, temporary/native memory high-water, sleep/CCD counts and
failures. No pointer/native ID/entity name or unbounded shape key is a metric label.
Debug extraction copies a bounded Horo snapshot after step; Renderer never reads
Jolt storage.

### 16. Verification is part of the contract

Required coverage includes:

- dependency tag/commit/archive digest, build-option lock, header/library version
  match, no system/floating source and third-party notice/SBOM presence;
- public-header/dependency scans proving no `JPH`/native types or graphics/Jolt
  sample/test-framework dependencies cross Horo Physics;
- SI conversions, right-handed Y-up/-Z-forward adapter mapping, column-vector
  transforms, gravity, radians and foreign-import normalization;
- every CanonicalV1 tolerance/default mapping and upstream-default drift detection;
- dynamic/static/speed/mass/density envelope edges, non-finite/unsafe ratios,
  sub-tolerance geometry and hard/high-fidelity local-cluster bounds;
- static/kinematic/dynamic authority, shapes, constraints, layers, CCD, sleep,
  contact/query ordering and exact stale/cross-world handle rejection;
- `60 Hz` fixed step across render rates, pause/single-step, serial jobs, bounded
  callbacks/events, cancellation and no gameplay/ECS access during native callbacks;
- origin rebase position shift with velocity/contact/sleep preservation and atomic
  preparation failure/rollback;
- deterministic tuples across repeat runs and qualified compiler/CPU modes with
  exact discrete and declared numeric continuous comparisons;
- world create/unload/reload/partial-init/repeated-shutdown, late commands/jobs and
  native invariant/non-finite containment;
- macOS arm64/x86_64, Windows x86_64 and Ubuntu x86_64 matrix; explicit admission
  failure for every unqualified required-Physics target;
- upgrade rehearsal from prior pin, API/default/simulation drift, cache invalidation,
  performance/memory regression, rollback and no native save-state dependency.

## Consequences

Downstream Physics work has one concrete, modern solver and exact units/tolerance/
platform assumptions while Horo contracts remain solver-neutral in representation.
Jolt aligns with Horo scene coordinates and SI units, and its MIT license permits
source integration with straightforward attribution.

The choice intentionally trades runtime solver interchangeability for a narrower,
testable architecture. Mobile/web/consoles, double precision, parallel stepping and
solver upgrades require explicit qualification instead of inheriting upstream
claims or build flags.

## Rejected Alternatives

### Define a multi-solver backend ABI before implementing Physics

Rejected because body locks, filters, jobs, callbacks, shapes and serialization do
not share a stable lowest-common-denominator ABI. Horo semantic APIs remain narrow
without promising runtime substitution.

### Use Bullet as the initial canonical solver

Rejected for this baseline because Jolt more directly matches the selected modern
multicore-oriented C++ integration, coordinate/unit conventions and planned Horo
job/lifecycle model. This is not a claim that Bullet is unsuitable generally.

### Use PhysX as the initial canonical solver

Rejected because Horo does not need its broader vendor/native ecosystem to establish
the initial open source CPU solver contract, and selecting it would not remove the
need for the same private Horo boundary and qualification work.

### Build a custom rigid-body solver

Rejected because collision/constraint/character correctness and platform
qualification would delay the engine while creating substantially greater safety
and maintenance risk.

### Enable Jolt double precision by default

Rejected because ADR-026 already localizes Physics, and the ABI/performance/query
changes need a distinct qualified profile. CanonicalV1 keeps one consistent float
build.

### Serialize Jolt binary state as Horo save/cook authority

Rejected because upstream API/state changes and compile options would make durable
project/save compatibility depend on a private native format.

### Treat every upstream-supported platform as Horo-supported

Rejected because buildability does not prove Horo lifecycle, determinism, packaging,
performance or product qualification on that target.
