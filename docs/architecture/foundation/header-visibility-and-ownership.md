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
