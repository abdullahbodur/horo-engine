# Header Visibility And Ownership

## Purpose

This document defines the enforceable C++ header boundary for Horo's production
targets. It preserves stable `#include <Horo/...>` spelling without allowing a
consumer of one module to discover every header in the repository.

## Classifications

Every header has exactly one classification:

| Classification | Location | Visibility |
|---|---|---|
| SDK/public | `include/Horo/` | The owning target and consumers that link it. |
| Internal-shared | Owning module source tree | Only explicitly named internal targets; never installed or transitively exposed by a public target. |
| Target-private | Owning module source tree | The target implementation only. This is the default for `src/` headers. |

Public placement is a compatibility commitment, not merely a convenient include
path. Moving a source header into `include/Horo/` requires a stable owner, a narrow
contract, Doxygen documentation, migration notes, and consumer coverage.

`Horo/Vfx/VfxQualityPolicy.h` is owned by `HoroVfxApi`. It adds backend-neutral
immutable capability/policy evidence and pure admission decisions; consumers keep
linking `HoroEngine::VfxApi`, and no include spelling or existing caller migrates.

## Android Lifecycle Boundary

`HoroEngine::Platform` owns `Horo/Platform/AndroidLifecycle.h`. The header exposes
only portable lifecycle observations, generations, snapshots, typed results and
the bounded owner-thread controller. GameActivity, JNI, `ANativeWindow`, Vulkan
and OpenXR types remain target-private. The callback-shaped adapter lives under
`src/platform/android` and is never staged through public usage requirements.
There are no prior Android lifecycle callers to migrate; future Android product
composition consumes this contract while portable Runtime consumers remain free
of Android SDK dependencies. The generated standalone Platform public-header
consumer enforces that boundary.

## Build-Tree Contract

`cmake/HoroPublicHeaderOwnership.cmake` assigns each public header to one real
production target. `cmake/HoroTargetBoundaries.cmake` materializes a separate
include view under `build/target-includes/<target>/public` and places only the
owning target's registered headers in that view.

Production targets publish their own view with a build interface. Their declared
`PUBLIC` dependencies publish additional views transitively. Production usage
requirements must not contain the repository-wide `include/` or `src/` roots.
Implementations may read source headers privately, but that path is not inherited
by consumers.

Configure is the first enforcement gate:

- an unowned public `.h`, `.hh`, `.hpp`, `.hxx`, `.inl`, `.ipp`, or `.tpp`
  header is rejected;
- duplicate ownership is rejected;
- a registered path that does not exist is rejected;
- broad source/public include roots are removed from target usage requirements.

With testing enabled, CMake generates one isolated translation unit for every
registered public header. Each generated consumer links only the owning target,
so missing public dependencies or private-header leaks fail during compilation.

## Change Procedure

When adding or moving a public header:

1. Identify the real target that owns the contract.
2. Register the header under that target in
   `cmake/HoroPublicHeaderOwnership.cmake`.
3. Declare every dependency needed by the header as a target `PUBLIC` dependency.
4. Keep backend, GUI, platform-native, and third-party implementation types out
   of the contract unless the owning architecture explicitly permits them.
5. Build the generated public-header consumer target and every affected real
   consumer.
6. Record caller migration when ownership or include spelling changes.

If another production target needs a header currently under `src/`, do not expose
the source root. Either promote a deliberately stable contract to `include/Horo/`
or create a narrow non-installed internal interface with explicit consumers.

## ARC-001.2 Migration Notes

The initial boundary migration keeps all existing `Horo/...` include spellings.
No caller source rewrite is required. The observable change is intentional:
linking an unrelated Horo target no longer makes every public header available,
and linking EditorModel, EditorServices, Gui, InputSdl, or viewport targets no
longer exports the repository `src/` tree.

Callers that previously compiled through accidental include fan-out must link the
actual owning target. White-box tests that need implementation details must use a
narrow test-private include path or an explicit internal interface rather than
depending on production transitivity.

Legacy editor white-box tests use the non-installed `HoroEditorTestInternals`
interface as an explicit migration boundary. It is test-only and may expose the
source root to its listed consumers while their historical `editor/...` include
spellings remain. New tests should prefer a narrower test-private include path;
do not link this interface from production or SDK examples.

`HoroGui` currently exposes Dear ImGui types in several established public
headers, so `HoroThirdParty::ImGui` remains a truthful public usage requirement.
It may become private only after those signatures migrate to Horo-owned types.
`ProjectAssetImportCommitter` remains target-private behind an out-of-line
`AssetImportModal` destructor; do not reintroduce its `src/` include in the public
modal header.

## AUD-001.2 Migration Notes

`HoroEngine::AudioApi` now owns `Horo/Audio/AudioIdentity.h` and
`Horo/Audio/AudioErrors.h`. New audio consumers must link that target instead of
copying untyped integers or depending on a concrete device backend. There are no
existing audio API callers to migrate. The handle registry remains target-private;
only stable IDs and generation-safe client handles cross the public boundary.

## ERR-001.3 Migration Notes

`HoroEngine::Foundation` owns the new
`Horo/Foundation/ErrorCodeRegistry.h` public contract. Consumers that build or
query the host-validated registry link Foundation directly; the header exposes
only Foundation error and module-descriptor identities and does not publish an
application, platform, renderer, or third-party dependency. Existing error
producers keep their textual `ErrorDomainId` and `ErrorCode` values while module
composition migrates descriptor ownership into `ModuleDescriptor::errorDomains`.

## CIN-001.4 Migration Notes

`HoroEngine::CinematicModel` owns the new `Horo/Cinematic/CurveSampling.h`
contract. Cinematic consumers must link `HoroEngine::CinematicModel`, retain the
immutable key storage borrowed by `ScalarCurveView`, and validate once before a
curve enters frame-hot evaluation. Callers replace accumulated forward-only curve
state with direct `Sample(time)` calls and choose explicit clamp, repeat, or
ping-pong behavior for each boundary. Legacy non-finite values, duplicate times,
and non-monotonic cubic tangents are rejected rather than normalized silently.

## CIN-002.3 Migration Notes

`HoroEngine::CinematicRuntime` owns `Horo/Cinematic/SequencePlayer.h`,
`Horo/Cinematic/SequencePlayerErrors.h`, `Horo/Cinematic/SequenceEvaluation.h`,
and `Horo/Cinematic/SequenceEvaluationErrors.h`. Runtime hosts that own sequence-player
registries link this target directly. Model-only asset, cook and curve consumers keep
linking `HoroEngine::CinematicModel`; the runtime state machine does not widen that
lower-level public surface or introduce an Editor/GUI dependency. Detailed call-site
migration is documented in `docs/guides/cinematic-sequence-player-migration.md`.

## CIN-001.5 Migration Notes

`HoroEngine::CinematicModel` owns `Horo/Cinematic/TransformTrack.h`. Consumers
compile a `TransformEvaluationPlan` at activation from exact generation-checked
bindings and retain the immutable scalar-key storage borrowed by its curve views.
Frame and editor-preview evaluation share the same random-access API and caller-
owned output storage. Root tracks require a canonical `WorldCoordinate64` anchor;
children remain local-space, so callers must not subtract the active origin again.
Scene replacement requires a new plan rather than carrying cached bindings across
the scene-generation fence.

## PLS-001.2 Migration Notes

`HoroEngine::PlatformServices` owns
`Horo/PlatformServices/PlatformRequest.h` and
`Horo/PlatformServices/PlatformRequestErrors.h`. It has only the public Foundation
dependency and is deliberately separate from `HoroEngine::Platform`, whose operating-
system adapters do not own online-provider requests. No existing caller is migrated.
Future frontend/provider targets consume this boundary by linking PlatformServices;
they must not duplicate request state, retain a provider object in the handle, or rely
on repository-wide include visibility.

## PLS-002.2 Migration Notes

`HoroEngine::PlatformServices` additionally owns
`Horo/PlatformServices/PlatformServiceInterfaces.h` and
`Horo/PlatformServices/PlatformServicesBackend.h`. These are the first published
backend bundle and service interfaces, so no existing caller signature changes. Future
composition roots must inspect and validate the complete capability snapshot before
calling `Activate`; they consume unavailable services through typed failures instead of
nullable pointers. Private provider adapters implement the Horo interfaces while SDK
types, native handles, allocator ownership, callbacks and credentials stay behind the
ADR-131 extension boundary.

## PLS-003.2 Migration Notes

`HoroEngine::PlatformServices` additionally owns
`Horo/PlatformServices/PlatformStableIdRegistry.h`. Project/cook composition must pass
the root project salt and detached ledger candidate to
`BuildPlatformStableIdRegistry`, publish only a successful immutable snapshot and use
the service-specific typed resolution functions for authored keys. Callers must not
derive IDs with `std::hash`, register provider-native values as aliases, erase
tombstones or retain a mapping after its registry fingerprint/revision changes.
Provider adapters validate mappings through opaque digests; concrete provider values,
SDK types and reverse maps remain private. The generated PlatformServices public-header
consumer compiles the registry header through its sole owning target.

## PLS-003.3 Migration Notes

`HoroEngine::PlatformServices` additionally owns
`Horo/PlatformServices/AchievementDefinitionRegistry.h`. Project and cook composition
build a complete immutable definition snapshot against one captured ADR-132 project and registry
fingerprint. Every active achievement stable ID requires exactly one typed definition;
tombstoned, unknown, duplicate, malformed, incomplete, stale or unbounded candidates
fail before publication with field-level diagnostics. Published authority and progress
semantics are immutable across ordinary replacement, while localization keys and hidden
presentation may change. Provider-native identifiers, SDK values and account identity
remain outside this public definition contract.

## PLS-003.4 Migration Notes

`HoroEngine::PlatformServices` additionally owns
`Horo/PlatformServices/PlatformDefinitionRegistries.h`. The shared
`ProgressionAuthorityMode` now lives in `PlatformServiceInterfaces.h`, its lowest
backend-neutral owner, so achievement, stat and leaderboard definitions use one type
without depending on one another. Project/cook composition builds stats first, then
leaderboards against that immutable stat snapshot, and presence independently; all
three candidates reference the same captured ADR-132 ledger fingerprint. Public
callers consume only typed definitions and immutable spans. Provider-native mapping,
runtime subject/session handles and raw account identities remain private or
generation-scoped and never enter these durable definition registries.

## PLS-003.5 Migration Notes

`HoroEngine::PlatformServices` additionally owns
`Horo/PlatformServices/PlatformProjectConfiguration.h`. GUI, CLI, headless and cook
composition roots now consume the same immutable typed project-policy snapshot rather
than interpreting provider names or capability flags independently. Provider selection
is exact or explicit Null; an unavailable exact provider never falls through to another
installed contribution. Package/trust composition supplies the selected trusted module
identities, and validation admits only bounded inert contributions from those modules.
Constructing or validating policy performs no discovery, registration, lifecycle call,
SDK initialization or ambient-state mutation. Existing callers require no signature
migration because this is the first published project configuration contract.

## PLS-006.2 Migration Notes

`HoroEngine::PlatformServices` additionally owns
`Horo/PlatformServices/PlatformUserSession.h`. `PlatformProviderGeneration` and the
exhaustive `PlatformServiceKind` move to this lower backend-neutral owner so the live
subject and session snapshot can fence service calls without depending on the backend
bundle declaration. `PlatformServiceInterfaces.h` includes the new owner and retains
its existing request signatures.

The provisional aggregate `PlatformSubjectHandle{nonce, generation}` and
`PlatformSessionSnapshot{subject, signedIn}` are intentionally replaced. Callers obtain
a handle only from a validated Active snapshot, branch on `PlatformSessionPhase`, and
revalidate the handle plus `PlatformAccessPolicyRevision` before user-scoped commit.
There is no compatibility `signedIn` boolean, public nonce accessor, serialization or
native account value. Existing provider test fixtures must build detached candidates;
production identity brokers supply cryptographic random nonce evidence and retain the
provider-private authenticated binding outside this public target.

## XRA-001.2 Migration Notes

`HoroEngine::XRApi` owns the four public headers under `Horo/XR/` and depends
publicly only on Foundation. Consumers of typed XR identities, capability evidence,
contract versions, admission results, or error descriptors must link XRApi directly;
linking Runtime, RenderApi, Platform, Input, or a future concrete XR backend does not
implicitly publish this contract. Public-header consumer coverage compiles every XR
header through the staged owner boundary.

There is no production XR caller to migrate. Future XRRuntime and XROpenXR targets
must consume these Horo types without duplicating them, serializing process-local
identity values, or exposing OpenXR headers, handles, result integers, extension
names, or platform-native types through the public boundary.

## TRF-001.2 Migration Notes

`HoroEngine::TerrainApi` owns `Horo/Terrain/TerrainIdentity.h` and
`Horo/Terrain/TerrainErrors.h` with a Foundation-only public dependency. Future
Terrain Runtime, cook, render-extraction, Physics, Navigation and World Streaming
adapters must link TerrainApi explicitly; linking an adjacent subsystem does not
publish Terrain identities transitively. The generated TerrainApi public-header
consumer verifies that no native backend, editor, service-locator or repository-wide
include path leaks through this boundary.

There are no existing production Terrain callers to migrate. Persisted content uses
only the fixed-width stable identity or dataset-plus-tile-coordinate encodings.
`TerrainRuntimeHandle` and `RuntimeFoliageInstanceHandle` are process-local and must
be resolved again after replacement, world unload or shutdown; they are deliberately
excluded from the serialization surface.

## CIN-001.3 Migration Notes

`HoroEngine::CinematicModel` now owns `Horo/Cinematic/SequenceAsset.h` and has the
deliberate public dependency on `HoroEngine::Assets` required by stable external
`AssetId` references. Consumers that parse, validate, or plan sequence assets must
link CinematicModel directly; linking Assets, Runtime, EditorServices, or a future
cinematic runtime does not implicitly publish the sequence schema.

There was no implemented sequence schema caller to migrate. New persisted sources
use exact schema `1.0`; no legacy schematic `SequenceAsset` structure is accepted as
a compatibility path. Older same-major sources require an explicit migration before
parsing, while newer minor or major versions require a compatible engine. Cook and
reload consumers resolve stable asset identities through an exact registry/provider
snapshot rather than persisting paths, runtime handles, or native backend values.

## Audio Backend Contract Boundary

`HoroEngine::AudioApi` owns the public discovery, format, capability, timing and
borrowed planar-block values. `HoroAudioBackendContract` is a separate,
non-installed interface exposing only `src/audio/backend/include`, not `src/`.
Audio control and concrete adapter targets must link it privately when they are
implemented; it is not a public dependency of AudioApi or an SDK extension ABI.
Its current explicit consumer is `HoroAudioBackendContractTests`. AudioApi tests
also assert that this internal header cannot be found through public usage
requirements. No existing production adapter needs a migration yet.

## Physics Identity Boundary

`PhysicsWorld.h` adds explicit process and detached-world lifecycle ownership to the
same target. Its opaque implementation keeps all Jolt types, allocator hooks, serial
jobs and filter objects private. Public consumers require only the existing
Foundation/Assets boundary; no RuntimeScene or graphics dependency is introduced.
`PhysicsWorldSettings.h` owns validated immutable policy and content identity.
There are no production Physics lifecycle callers to migrate; later scene activation
must prepare privately and bind/publish only at its aggregate commit boundary.

`HoroEngine::Physics` owns `Horo/Physics/PhysicsIdentity.h` and
`Horo/Physics/PhysicsErrors.h`, with a Foundation-only public dependency.
World-scoped Physics handles wrap Foundation's zero-based slot identity rather
than exposing solver IDs. Owner preflight checks representation and world only;
the owning registry must still establish occupancy, generation and lifetime.
There are no existing production Physics callers to migrate. Scene activation
retains its own scene-to-world binding rather than introducing a reverse
Physics dependency on RuntimeScene. The initial target implements only these
contracts; native composition and runtime operations remain separate work.

`PhysicsWorldDescriptor.h` and `PhysicsCapabilities.h` extend the same target with
inert world policy, bounded plan capacity and owner-published capability evidence.
Their validation does not construct a world or establish native/determinism
qualification. Requested fixed delta remains a double value after validation;
profile/solver integration must separately qualify its rate and conversion.
Public consumers continue to depend only on Foundation, with no new native or
SceneRuntime include paths.

`PhysicsPose.h`, `PhysicsShapeDescriptor.h`, `PhysicsBodyDescriptor.h` and
`PhysicsConstraintDescriptor.h` add owned, inert runtime values on the same
Foundation-only boundary. They reuse Scene Math, with no native math, scale
multiplier, renderer handle or RuntimeScene dependency. Their registered public
headers are compiled by the standalone Physics consumer. No existing caller needs
migration; future scene authoring keeps stable IDs and candidate plan indexes
separate from these published-world handle requests.

The initial geometry vocabulary is analytic box/sphere/capsule/static plane;
constraint parameters are fixed/distance only. Cooked hull/mesh/height-field/
compound artifact requests and other joint policies remain separate work, never
primitive or fixed-joint fallbacks. Descriptor validation checks representation,
owner identity and common numeric policy, not native availability, handle
liveness, shape/motion compatibility, filter/material admission or publication.
The enclosing operation must retain leases and bind/revalidate origin/schema
generations at its structural safe point. These descriptors are not self-contained
queued commands or complete native creation operations.

Body speed preflight follows the normative Physics architecture's `500 m/s`
limit rather than proposed ADR-084's conflicting `50 m/s` text. Geometry-dependent
size, dynamic-contact radius, local-cluster bounds and native qualification are
not established by common descriptor validation.

`PhysicsCookedShapeDescriptor.h` extends this boundary with exact asset-local
subresource, cook cache key, payload and Physics target digest references. This
adds the deliberate one-way public `Physics -> Assets` dependency for the existing
`AssetId`; it does not duplicate Assets identity in Foundation or import a native
solver/renderer dependency. The ownership registry and dependency policy encode
the same boundary, and the standalone Physics consumer compiles the new header.
No existing caller is migrated. The earlier Foundation-only statements above
describe the initial identity/analytic slice, not this additional reference surface.

`[PHY-009.2]` adds `Horo/Physics/PhysicsMetrics.h` to `HoroEngine::Physics`.
The header exposes only bounded Horo measurement values, exact Physics world/revision
identity and Foundation Telemetry handles already registered by process composition.
It adds no solver SDK, renderer, editor, platform clock, metric store or profiler
backend dependency. Existing Physics producers migrate by supplying one immutable
post-publication snapshot and pre-bound handles; metric dimensions are never resolved
on the fixed-tick path. The generated standalone Physics header consumer verifies the
same ownership boundary.

Reference validation does not read an artifact, recompute a target key or establish
geometry readiness. Full target encoding and envelope verification remain the
owning cook/runtime work; opaque digest equality alone cannot prove a correct
canonical preimage or admit native bytes. Optional digest fields distinguish
missing evidence from an explicitly supplied all-zero digest representation.

`HORO_BUILD_PHYSICS_NATIVE` selects the private pinned Jolt library or an omitted
implementation of the target-private build compatibility check. `Physics` links
the native target privately; no native include paths, types or compile definitions
are public usage requirements. The ordinary Physics test consumer rejects native
SDK visibility at compile time. A separate native-boundary test deliberately
links Jolt to verify binary ABI mismatch rejection and unchanged factory/allocator
state. The check itself never registers types or initializes a world; explicit
activation and world teardown remain the scene lifecycle owner's responsibility.

## Gameplay AI Blackboard Boundary

`HoroEngine::AI` owns `Horo/AI/AIIdentity.h`, `Horo/AI/BlackboardSchema.h`, and
`Horo/AI/BlackboardInstance.h` with a Foundation-only public dependency. The
instance contract reuses the existing strong agent/schema/key identities and typed
schema values. Its public storage, snapshots, write batches, generation fences,
revisions, and diagnostics contain no RuntimeScene, editor, MCP, network, platform,
or backend-native types. The owning Scene adapter creates and tears down instances;
only its `BlackboardSync` safe point applies staged mutations.

`BlackboardInstance.h` is registered to the existing AI target and compiled by the
generated standalone public-header consumer. This slice introduces no production
caller migration and no second schema/value authority. Future AI runtime composition
must consume this contract instead of duplicating string-keyed storage or exposing
mutable instance memory to worker tasks.

## PCG Identity Boundary

HoroEngine::PCG owns Horo/PCG/PCGIdentity.h and Horo/PCG/PCGErrors.h.
The public contract contains only durable authored identities and exact graph-revision
associations, with a Foundation-only dependency. Its canonical encodings cannot
represent pointers, container positions, runtime registry handles, callbacks,
filesystem paths, or backend-native values. Existing callers require no migration
because this is the first published PCG API slice.

`[PCG-1.3]` adds `Horo/PCG/PCGPointSchema.h` to the same Foundation-only owner.
Consumers now use its canonical immutable schema, typed columnar point snapshots and
exact provider-neutral tier limits instead of publishing free-form per-point maps.
The contract uses only Foundation and Horo Scene Math values; Scene, target-owner,
renderer, editor, platform and native backend authority remain outside HoroPCG.
Rejected replacement candidates do not mutate or invalidate the last good snapshot.

## Animation Identity And Component Boundary

`HoroEngine::AnimationApi` owns `Horo/Animation/AnimationErrors.h`,
`Horo/Animation/AnimationIdentity.h`, and `Horo/Animation/AnimationComponents.h`.
Its only public dependencies are Foundation and Assets for typed results/errors,
generation-safe slots, and the canonical persistent `AssetId`. RuntimeScene,
Physics, Render, editor, platform, native animation middleware, pose buffers,
callbacks, and service-locator types remain outside this public boundary.

This ANI-001.2 slice introduces inert identity and component values plus bounded
association validation. It does not create an animation runtime, load an asset,
allocate pose storage, issue a lease, evaluate a graph, consume root motion, or
register a scene component. No production caller requires migration because this
is the first published Animation API slice. Future animation runtime, scene cook,
physics handoff, cinematic, and render-extraction targets must link AnimationApi
explicitly instead of duplicating IDs or persisting process-local handles. The
generated standalone public-header consumer verifies this staged dependency
boundary.

ANI-001.3 adds `Horo/Animation/SkeletonAsset.h` to the same owner. Import, cook,
scene conversion and future runtime evaluators consume one immutable validated
snapshot whose joint order is canonical and parent-before-child. Stable joint and
socket identities remain independent of dense positions and advisory names. Reload
validates the same persistent skeleton identity before publication; cancellation or
shutdown rejects the detached candidate without replacing the last good snapshot.
No Physics, Render, RuntimeScene, editor, filesystem, parser, or native animation
dependency is added to the public boundary.

ANI-001.4 adds `Horo/Animation/SkeletalMeshSkinning.h` to the same owner. It
publishes immutable, bounded skeletal-mesh skinning data and an explicit stable
mesh-joint-to-skeleton-joint remap bound to one skeleton publication generation.
Validation canonicalizes LOD, section, palette, and influence order before
publication and rejects lifecycle, reload, version, stale-generation, range, and
limit failures transactionally. Renderer buffers, backend handles, mutable pose
palettes, import parsers, scene instances, and filesystem state remain outside
the public boundary; existing AnimationApi consumers require no migration.

ANI-001.5 adds `Horo/Animation/PoseStorage.h` to the same owner. Runtime
composition creates one bounded arena for an exact runtime and immutable publication,
then performs allocation-free owner-thread pose mutation and hierarchy evaluation.
External consumers receive move-only immutable leases instead of mutable spans or
recyclable pointers. Frame reset, cancellation, reload, and shutdown preserve exact
runtime, skeleton, frame, slot, and semantic pose generations. The contract adds no
RuntimeScene, Physics, Render, job-system, platform, editor, service-locator, or
backend-native dependency; those future adapters must consume AnimationApi explicitly.

## World Streaming Origin Frame Boundary

`Horo/WorldStreaming/OriginFrame.h` is owned by `HoroWorldStreaming`. It exposes
only Horo Foundation scene-math, strong-identity, result, and World Streaming
error contracts. The header owns canonical-to-local conversion and immutable
frame lease semantics; it has no Scene Runtime, renderer, physics, native,
editor, or GUI dependency. Trigger policy, participant coordination, and backend
adapters remain outside this public identity boundary.

ANI-001.6 adds `Horo/Animation/AnimationClip.h` to the same owner. Asset and runtime
composition replace ad hoc floating-point cursors and untyped wrap flags with exact
nanosecond-tick time, reduced sample-rate metadata, stable generation-fenced clip and
additive-reference identities, and immutable canonical joint tracks. Load/cook may
allocate while validation and canonicalization run; directed traversal and sampling
are bounded, allocation-free, and write only caller-owned pose storage after complete
preflight. The public boundary adds no RuntimeScene, renderer, physics, filesystem,
job-system, codec, middleware, callback, or backend-native dependency.

## Destruction Identity Boundary

`HoroEngine::DestructionApi` owns `Horo/Destruction/DestructibleDescriptor.h`,
`Horo/Destruction/DestructionCommand.h`, `Horo/Destruction/DestructionIdentity.h`,
`Horo/Destruction/DestructionRegistry.h`, `Horo/Destruction/DestructionStateMachine.h`
and `Horo/Destruction/DestructionErrors.h`.
Its public dependencies are limited to
Foundation and Assets for typed results/errors, the shared SHA-256 value and the
path-independent `AssetId`. Physics, Render, RuntimeScene and native provider headers
remain outside the public boundary.

This `[DFR-001.2]` slice introduces identity values and validation only; it does not
create a runtime world, registry, fracture artifact, physics body or render resource.
No production caller requires migration because the destruction target did not
previously exist. Future cook/runtime targets must consume this owner rather than
duplicate identities, expose native handles or infer stable identity from names, paths
or table positions. The standalone Destruction API test consumer verifies the staged
header dependency boundary.

The `[DFR-001.3]` slice adds immutable typed health, behavior, cleanup, replication,
feature-tier and finite-limit descriptors plus allocation-free admission validation.
Consumers migrate from duplicated numeric limits or provider selection to the exact
provider-neutral tier profile and typed failures. The header introduces no Physics,
Render, RuntimeScene, platform or native provider dependency.

The `[DFR-001.4]` slice adds `Horo/Destruction/DestructionStateMachine.h` to the same
owner. DestructionRuntime composition creates the immutable state from an admitted
descriptor and serializes candidate commits at its owner safe point. Producers prepare
generation- and revision-fenced commands against immutable snapshots; they do not
mutate scene components, retain backend handles or publish detached work directly.
Exact retries are idempotent, conflicting command reuse is rejected, and replacement,
cancellation and shutdown preserve the last published snapshot until a legal successor
commits. Existing prototypes with mutable health/state fields must migrate to this
single-owner contract rather than dual-write both representations.

The `[DFR-001.5]` slice adds fixed-size impact, explosion, collision, damage and
script commands with exact authority, capability, generation, revision and fixed-tick
evidence. Callers validate the immutable value before queue admission, then lower it to
the existing state-machine command without changing its identity. Durable terminal
results preserve successful, rejected, cancelled, unsupported and failed dispositions
as closed types. Producers must migrate from provider handles, callback mutation and
message parsing to this contract; rejected or stale private work is discarded and is
never published as a partial fallback.

The `[DFR-001.6]` slice adds the explicit fixed-capacity registry, immutable value
snapshots, bounded queries and capability projections. The composition owner copies
only current backend-neutral publication evidence into the registry; membership never
owns a destructible or extends Scene, Physics, Render, artifact or authority lifetime.
Consumers link `HoroEngine::DestructionApi`, retain snapshots for read-only work and
revalidate generation/state/capability revisions before live operations. Ad-hoc global
registries, mutable record exposure, native handles and silently widened queries have no
compatibility path.
