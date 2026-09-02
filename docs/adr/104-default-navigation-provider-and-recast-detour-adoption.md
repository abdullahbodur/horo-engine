# ADR-104: Default Navigation Provider and Recast-Detour Adoption

- **Status**: Proposed
- **Date**: 2026-09-02
- **Supersedes**: None
- **Scope**: First production grounded NavMesh provider, exact dependency pin, module selection, licensing, native encapsulation, threading, determinism, tile-cache/crowd policy, non-ground navigation boundary, upgrades and migration
- **Issue**: [NAV-001.4](https://github.com/abdullahbodur/horo-engine/issues/1227)
- **Jira**: [HORO-1227](https://horo-engine.atlassian.net/browse/HORO-1227)
- **Related**: [ADR-005](005-submodule-compatibility.md), [ADR-010](010-job-waiting-and-operation-store-ownership.md), [ADR-012](012-world-streaming-partition-authority-and-subsystem-boundaries.md), [ADR-016](016-navigation-target-ownership-and-dependency-boundary.md), [ADR-022](022-ai-fixed-tick-order-authority-and-simulation-budget.md), [ADR-026](026-large-world-precision-and-floating-origin-strategy.md), [ADR-109](109-avoidance-crowd-and-renderer-independent-budget.md)
- **Normative documents**: [Navigation and AI Architecture](../architecture/runtime/navigation-and-ai-architecture.md), [Build System](../architecture/delivery/build-system.md), [Asset Pipeline](../architecture/runtime/asset-pipeline.md), [World Streaming](../architecture/runtime/world-streaming-architecture.md), [Concurrency and Jobs](../architecture/foundation/concurrency-and-jobs.md)
- **Upstream references**: [Recast Navigation](https://github.com/recastnavigation/recastnavigation), [pinned source](https://github.com/recastnavigation/recastnavigation/commit/9f4ce64458dfae86e1239c525ddc219c4e9e06f1), [zlib license](https://github.com/recastnavigation/recastnavigation/blob/9f4ce64458dfae86e1239c525ddc219c4e9e06f1/License.txt), [development roadmap](https://github.com/recastnavigation/recastnavigation/blob/9f4ce64458dfae86e1239c525ddc219c4e9e06f1/Docs/_99_Roadmap.md), [O3DE RecastNavigation Gem](https://github.com/o3de/o3de/tree/development/Gems/RecastNavigation), [Godot NavigationServer3D](https://github.com/godotengine/godot/tree/master/modules/navigation_3d), [ROS 2 Nav2](https://github.com/ros-navigation/navigation2)

## Context

ADR-016 established backend-neutral NavigationApi/NavigationRuntime boundaries and
a private `NavigationRecastDetour` provider target, but intentionally did not
ratify a source revision, module set or production support policy. The architecture
already assumes tiled grounded NavMesh baking, runtime polygon queries, optional
dynamic carving and an optional crowd seam. A provider decision is required before
those contracts become code and cooked artifacts.

The choice is not only a pathfinding algorithm. It fixes voxelization and polygon
generation behavior, private tile/reference representation, query scratch and
threading constraints, native serialization compatibility, license inventory and
the amount of crowd/tile-cache behavior Horo may safely claim. Exposing those native
details would make replacement and artifact migration unbounded.

Recast Navigation is an active, dependency-free game-navigation project under the
zlib license. Recast builds polygonal navigation meshes from rasterized geometry;
Detour loads tiled meshes and performs runtime queries. Its repository also contains
DetourTileCache for compressed tiles/dynamic obstacle rebuilding and DetourCrowd for
path following/local steering/avoidance. The current upstream release tag is
`v1.6.0`, while active `main` includes later fixes and CMake/toolchain work.

Other maintained projects solve adjacent but less suitable problems. Godot and
O3DE provide complete engine integrations whose public/server/bus types are tied to
their hosts; O3DE itself wraps Recast/Detour. ROS 2 Nav2 is a production robotics
stack organized around costmaps, ROS nodes/actions/plugins, planners and controllers,
not a small game-runtime grounded NavMesh kernel. A Horo-owned implementation would
avoid a vendor dependency but require Horo to build and qualify voxelization,
contouring, polygonization, tiled connectivity, queries and dynamic updates before
shipping any production navigation.

This ADR chooses the first provider and its support envelope. It does not define the
authored/cooked asset ownership, bake transaction/cache or query-snapshot contracts;
their focused NAV-002/NAV-003/NAV-004 decisions consume this provider boundary.

## Decision

### 1. Recast/Detour is the first production grounded NavMesh provider

Horo adopts upstream `recastnavigation/recastnavigation` at exact commit
`9f4ce64458dfae86e1239c525ddc219c4e9e06f1` (2026-02-27), a descendant of
`v1.6.0`. The dependency identity is the full commit, not the upstream CMake
`LIB_VERSION` value, a tag, `main`, a semver range, a system package or a mutable
download URL.

The implementation change declares the immutable commit centrally in
`cmake/Dependencies.cmake`, registers it in Horo's dependency manifest/fingerprint
and verifies the resolved revision. This ADR does not add the dependency before the
provider targets exist. A source archive may replace Git acquisition only with a
reviewed immutable URL plus SHA-256 while retaining the same source commit identity.

`HoroEngine::NavigationRecastDetour` is the default real provider for the initial
desktop/headless qualification matrix. `NavigationNull` remains the explicit
feature-absent provider and navigation may be omitted. There is no runtime provider
download, provider hot swap or string-based native backend selection after scene
activation.

### 2. Horo contracts own all public semantics and identity

ADR-016 remains normative. Public headers, authored scene data, gameplay/EQS
requests, cooked neutral envelopes, paths, obstacles, crowd commands, errors,
diagnostics and handles expose only Horo types. They contain no `rc*`, `dt*`,
`dtPolyRef`, `dtTileRef`, `dtNavMesh`, `dtNavMeshQuery`, `dtTileCache`, `dtCrowd`,
vendor allocator/context/filter structures, macros, headers or binary layout.

The provider privately maps generation-checked Horo world/topology/tile/poly/query/
crowd identities to native values. A native reference is valid only under its exact
provider instance and topology generation; it is never serialized as public asset,
save, network or script identity. Replacement/reuse invalidates the mapping before
native storage retires.

All conversion, coordinate/winding normalization, status mapping and native lifetime
remain under `src/runtime/navigation/backends/recast_detour/`. Vendor headers and
usage requirements are PRIVATE. Provider factory/descriptor headers expose Horo
interfaces only, and isolated public-header/link/export scans enforce the boundary.

### 3. Required and optional upstream modules are explicit

The selected source repository does not mean every upstream module is a baseline:

| Upstream module | Horo status | Product role |
|---|---|---|
| `Detour` | Required production runtime baseline | Immutable tiled topology loading, polygon queries, pathfinding, nearest-point/raycast/corridor operations and direct tile add/remove |
| `Recast` | Required bake/cook baseline only | Offline/editor/CLI/CI grounded NavMesh generation from validated owned geometry; absent from runtime-only/server packages that do not bake |
| `DetourTileCache` | Optional `DynamicTileCarving` capability | Compressed local tile layers and bounded obstacle-driven rebuilds; not World Streaming, asset residency or canonical authored state |
| `DetourCrowd` | Optional `DetourLocalAvoidance` capability | Best-effort safe-velocity/local avoidance behind `INavigationCrowdBackend`, with private corridor caching only; not required for path queries or 1.0 deterministic simulation |
| `DebugUtils` | Not shipped as a public/runtime dependency | Horo extracts bounded neutral debug geometry; Renderer/editor never consume vendor draw interfaces |
| `RecastDemo`, examples and upstream test application | Excluded from product builds/packages | May inform development; Horo adapter/contract tests own qualification |

Private runtime and bake source groups preserve this split. A runtime-only
composition links Detour and only explicitly requested optional Detour modules; it
does not rely on dead stripping to hide Recast builder/demo/debug code. Bake hosts
add Recast privately. Packages and SBOM/notices record the modules actually linked.

Direct Detour tile add/remove is sufficient for ADR-012 cell streaming. Selecting
DetourTileCache does not authorize a second loader, residency budget or artifact
store. It rebuilds a staged local topology candidate from already admitted data;
World Streaming and NavigationRuntime still own publication and generations.

### 4. One locked native build profile prevents silent ABI/behavior drift

The initial Horo build profile fixes these upstream options:

| Option | Value | Reason |
|---|---:|---|
| `BUILD_SHARED_LIBS` | `OFF` | Private static implementation; no exported vendor ABI |
| `RECASTNAVIGATION_DEMO` | `OFF` | No SDL/demo/render surface in engine products |
| `RECASTNAVIGATION_EXAMPLES` | `OFF` | Samples are not production dependencies |
| `RECASTNAVIGATION_TESTS` | `OFF` in product builds | Horo contract tests own integration; a separate dependency qualification job may enable upstream tests |
| `RECASTNAVIGATION_DT_POLYREF64` | `ON` | Private larger tile/poly address envelope for streamed worlds |
| `RECASTNAVIGATION_DT_VIRTUAL_QUERYFILTER` | `OFF` | Horo typed filters map through the private non-virtual path; no provider C++ extension ABI |
| `RECASTNAVIGATION_ENABLE_FAST_MATH` | `OFF` | Avoid an extra uncontrolled numeric profile |

The provider target uses the repository's no-exception/no-RTTI-compatible build
without propagating its compiler flags or macros to Horo consumers. Upstream asserts
map to a bounded Horo diagnostic/failure policy in developer/qualification builds;
they do not terminate arbitrary shipping hosts through an unreviewed callback.

Any change to the commit, native options, compiler/architecture/FP mode or selected
modules creates a new provider build fingerprint. Private optimized cooked sections
and golden output evidence include that fingerprint and are rejected/recooked on a
mismatch.

### 5. Horo owns threading; provider instances expose no thread pool

Upstream's roadmap explicitly states that Recast has no specific threading support.
Horo therefore adds no vendor-owned executor. All bake, query, topology and optional
crowd work is admitted through Foundation `JobSystem` and the NavigationRuntime
budgets/lifecycle from ADR-010/ADR-022.

Thread-safety rules are conservative and explicit:

- Recast bake parallelism is across independent tiles/jobs. Each job owns its
  `rcContext`, intermediate structures, allocator domain and input/output buffers;
  no mutable build object or temporary allocator is shared.
- A query job exclusively leases one `dtNavMeshQuery` plus node pools/scratch. It
  reads one pinned immutable `dtNavMesh` snapshot; sliced query state is never
  shared or resumed by another job.
- Topology publication is copy/stage/commit. A mutable Detour mesh or tile-cache
  candidate has one writer and is never queried until published immutable.
- One `dtTileCache` and its compressor/allocator/process objects have one exclusive
  owner during an update batch.
- One `dtCrowd` has one exclusive owner during update. Agent selection/order and
  result publication remain Horo coordinator policy, not worker completion order.

Alloc/free/assert hooks install once per linked vendor-library instance before any
provider object and outlive every native allocation/job. Their routing is
thread-safe and retains the allocating Horo memory domain for cross-thread frees.
No scene/job swaps process-global hooks.

### 6. Determinism is qualified, never inferred from the provider name

Recast/Detour uses floating-point geometry/search and publishes no universal
cross-platform bit-identical contract. Horo does not claim one. Reproducibility
identity includes the exact source commit, native options, Horo adapter/cook schema,
compiler/architecture/FP mode, coordinate conversion, canonical sorted geometry,
tile/build settings, area/filter table, topology publication order and query budget.

Bake input triangles, areas, off-mesh links and tile coordinates are canonicalized
and stably ordered before provider calls. Tiles build independently and publish in
stable coordinate/layer order, never job-completion order. Horo normalizes provider
output into the versioned neutral artifact; private native bytes remain derived
cache sections. Golden/cross-run tests compare exact discrete topology where
qualified and declared numeric envelopes where float behavior is not exact.

Initial Detour query qualification may claim only `SameBuildSamePlatform` semantic
repeatability after evidence for a fixed topology/filter/request/budget. Stable
request ordering and declared-tick publication are still required. A node/output
budget overflow returns a typed partial/capacity result; it cannot choose a result
based on wall time.

`DetourLocalAvoidance`, runtime tile-cache carving and cross-platform/cross-build
lockstep are excluded from the initial deterministic fixed-tick tier. A product
requiring deterministic navigation either uses prebuilt immutable topology plus the
qualified Detour query profile or fails capability selection. Future qualification
may add a distinct fingerprint/profile; it cannot silently broaden the baseline.

### 7. Tile-cache use is bounded derived topology, not world authority

When composed, `DetourTileCache` stores private compressed layers and processes a
bounded stable obstacle command batch into an isolated topology candidate. Horo
owns logical obstacle identity, command ordering, work/memory limits, cancellation,
conservative pending-blocker behavior, topology revision and safe-point publication.

Tile-cache time-slicing is expressed as deterministic provider work units/node/tile
budgets. A frame wall-clock threshold may stop only best-effort work at Horo-owned
boundaries. Queue/obstacle/touched-tile overflow returns a typed result and retains
last-good topology; it never drops an authoritative blocker silently.

World Streaming loads canonical Horo cell payloads and owns residency/eviction.
DetourTileCache cannot read assets, request cells, publish a tile, retain unbudgeted
evicted data or interpret a compressed native layer as portable authored truth.

### 8. DetourCrowd is optional local avoidance, not gameplay authority

DetourCrowd may consume Horo-owned preferred velocities and immutable corridor/
neighbor/obstacle facts to compute safe desired velocities for a bounded agent
batch. NavigationRuntime owns route/corridor authority, logical agents/handles,
fixed-tick admission, grouping, stable order, project profiles and desired-velocity
publication. Character/Physics owns final movement/collision/transform authority.

Detour local-corridor caches, crowd state, proximity grids, path queues, filters and
agent indices are private and transient optimizations, not path authority. They are
not serialized, replicated or used as ECS identity.
Save/network state records Horo semantic intent and reconstructs optional provider
state. A provider failure or absence returns `Unsupported`/safe-stop policy; it does
not fall back to an unbounded Horo loop inside the adapter.

Formation tactics, corridor lane formation and density policy are Horo Gameplay/AI
orchestration above the provider. DetourCrowd does not become their state authority.
Traffic/lane-network routing is a separate graph domain under Section 9.

### 9. The baseline is grounded surface navigation only

Recast rasterizes walkable geometry into polygonal surfaces for grounded agents.
Agent profiles may vary radius, height, climb and slope for humans, creatures or
ground vehicles. Off-mesh links represent explicit transitions such as jump,
ladder, door or teleport between grounded surfaces; they do not create continuous
3D free-space navigation.

Flying, swimming, space and other volumetric agents require a future typed volume/
voxel/graph provider, artifact and query contract. They are not encoded as an
ordinary Recast agent profile with permissive slope/height values and cannot consume
grounded NavMesh availability as proof of volumetric support.

Road/rail/traffic/lane navigation requires a future directed lane graph with lane
identity, direction, width, turn/connectivity rules, signals and vehicle constraints.
It is not a Detour polygon area flag, crowd formation setting or off-mesh-link
convention. Grounded NavMesh, volumetric navigation and lane graphs may later share
Horo world coordinates and high-level route composition while retaining separate
capabilities, artifacts, providers and results.

### 10. Licensing, notices and supply-chain evidence are release gates

The selected source is used under its zlib license. Horo retains the exact upstream
license text and origin/commit metadata, marks any source modifications, and includes
required notices in every source distribution and generated third-party notice/SBOM
for binaries containing linked Recast/Detour modules.

Dependency acquisition verifies the immutable revision/archive digest. License,
source inventory and vulnerability/dependency scanning run on the selected source;
missing/changed license or unresolved source blocks configure/release rather than
using a system copy. Demo assets/fonts and unrelated upstream content are not
shipped merely because they exist in the repository.

The permissive license does not grant architectural trust. Malformed/untrusted
cooked data, allocation failure and provider invariant/status errors are validated
and translated into Horo typed failures before publication.

### 11. Upgrades and provider replacement are explicit migration events

An upstream upgrade must:

1. select an immutable reviewed commit/archive and record its relation to the prior
   pin/release;
2. review license, build files/options, transitive content, API/data-format magic/
   versions, allocator/assert behavior and platform/compiler support;
3. diff Recast bake, Detour query, TileCache and Crowd behavioral changes relevant
   to every composed module;
4. rebuild adapter/header-boundary/platform qualification and fuzz malformed native/
   neutral tile inputs;
5. recook/invalidate all provider-derived artifacts and caches whose fingerprint or
   semantics changed, retaining prior artifacts for rollback;
6. compare canonical worlds for topology, reachability, paths, partial results,
   dynamic obstacles, performance and memory; and
7. update dependency manifest, notices/SBOM, migration report and rollback pin.

Replacing Recast/Detour with a Horo-owned or other provider preserves Horo authored
identities and neutral API where semantically compatible, but is not zero-cost.
Every NavMesh must be recooked, provider-private sections/caches discarded, path/
topology golden evidence requalified and unsupported capability differences surfaced
to projects. Saved games/network protocols cannot depend on native refs, so they need
no vendor-ID rewrite; semantic differences may still require a project migration.

Runtime provider hot swap is unsupported. A provider change occurs through a new
artifact/build and host/scene reconstruction with explicit compatibility validation.

### 12. Qualification covers the selected support envelope

Required automated evidence includes:

- exact commit/archive digest, no floating/system source, locked CMake options,
  module/link inventory, zlib notice/SBOM and license drift;
- Api/Runtime/Null consumers with no vendor include path plus negative direct,
  transitive, forward-declaration, exported-symbol and native-layout leak fixtures;
- Recast input canonicalization, tile-order independence, malformed/non-finite/
  oversized geometry, settings boundaries, allocation failure and golden topology;
- Detour nearest-point/raycast/path/partial/buffer/node-limit results, 64-bit private
  refs, stale topology/handles and no native identity in public/cooked/save/network
  data;
- concurrent immutable queries with exclusive query objects, candidate topology
  mutation, tile publication/retirement and race/sanitizer coverage;
- direct tile streaming under ADR-012 with no TileCache composition, then optional
  TileCache obstacle queue/work/memory/time-slice/rollback/eviction cases;
- optional DetourCrowd capacity/order/safe-stop/cancellation/shutdown and proof it
  does not own transforms, formation/lane policy or deterministic-tier admission;
- SameBuildSamePlatform query repeat runs, changed worker completion order and
  explicit rejection of unqualified cross-platform/crowd/carving determinism;
- grounded profiles/off-mesh links and explicit rejection of flying/swimming/
  volumetric and traffic/lane requests as unsupported distinct capabilities;
- macOS/Windows/Linux desktop and headless/server build, bake, load, query and
  lifecycle matrix; unqualified platforms fail required-provider admission; and
- upgrade/replacement rehearsal, complete recook/cache invalidation, behavior/
  performance/memory comparison and rollback to the prior pin.

## Consequences

### Positive

- Horo starts from a maintained, game-focused and permissively licensed bake/query
  implementation instead of funding a complete NavMesh stack before first use.
- Exact pin/options/module policy makes native behavior and artifacts reviewable.
- Strict private mapping preserves Horo identity and bounds later provider migration.
- Optional TileCache/Crowd capability prevents convenient modules from silently
  becoming world-streaming, movement or deterministic authority.
- Grounded, volumetric and lane navigation no longer share an ambiguous agent-profile
  fiction.

### Costs

- Horo must build a substantial adapter, neutral artifact conversion and lifetime/
  threading/qualification layer around a C-style native API.
- The chosen post-release commit requires Horo to review and fingerprint the exact
  source rather than relying only on the older `v1.6.0` release label.
- Provider upgrades may change generated topology/path behavior and require broad
  recook/golden/performance migration evidence.
- Crowd and dynamic carving are not automatically available or deterministic merely
  because their upstream modules exist.

## Rejected Alternatives

### Implement the initial stack entirely in Horo

Rejected for the first production provider because voxelization, contouring,
polygonization, tiled connectivity, robust spatial queries and dynamic updates would
delay usable navigation and create a larger unproven correctness/security burden.
Horo retains the semantic contracts needed for a future private replacement.

### Adopt Godot NavigationServer code as the provider

Rejected because the maintained MIT code is a complete Godot server implementation
coupled to Godot RIDs, templates, synchronization, resources and engine lifecycle.
Extracting it creates a long-lived fork and still requires a Horo adapter; it is not
a smaller independent grounded NavMesh dependency than Recast/Detour.

### Adopt ROS 2 Nav2

Rejected because Nav2 is a maintained robotics autonomy framework centered on ROS 2
nodes/actions/plugins, sensor-derived costmaps, planners and controllers. Its process,
dependency, environment and motion semantics do not match an embedded game NavMesh
kernel, even though individual algorithms are valuable references.

### Adopt O3DE's RecastNavigation Gem

Rejected because it is an O3DE-specific Apache/MIT/zlib integration using O3DE Gems,
buses/components and provider seams around the same Recast/Detour source. Horo should
integrate the upstream library directly behind its own already-defined contracts.

### Pin only upstream `v1.6.0`

Rejected because the 2023 tag omits years of maintained fixes present in the exact
reviewed 2026 commit. Horo pins the full newer commit and treats every later move as
an explicit upgrade; it does not pretend the upstream library version changed.

### Make DetourTileCache and DetourCrowd mandatory

Rejected because static/streamed pathfinding requires neither. Mandatory composition
would add mutable state, memory/work, serialization temptation and determinism claims
to products that only need immutable topology queries.

### Expose native refs or blobs to avoid conversion

Rejected because native layout/version/options are provider fingerprints, not durable
Horo identity. Exposure would lock assets, saves, scripts and gameplay to one pin and
make replacement unsafe.

### Represent flying or traffic lanes as special grounded profiles

Rejected because free-volume reachability and directed lane rules have different
topology, cost, update and query semantics. Overloading Recast settings or Detour area
flags would hide unavailable capabilities and corrupt authoring intent.
