# ADR-106: Navigation Bake Ownership, Transaction and Cache

- **Status**: Proposed
- **Date**: 2026-09-02
- **Supersedes**: None
- **Scope**: Navigation bake application-service ownership, GUI/CLI/MCP/cook entry points, immutable revision capture, latest-wins supersession, structured concurrency, workspace locking, dependency-aware cache identity, staged publication, last-valid rollback, terminal states, crash recovery and shutdown
- **Issue**: [NAV-003.1](https://github.com/abdullahbodur/horo-engine/issues/1244)
- **Jira**: [HORO-1244](https://horo-engine.atlassian.net/browse/HORO-1244)
- **Related**: [ADR-005](005-submodule-compatibility.md), [ADR-008](008-error-model-exception-boundary-and-registry.md), [ADR-010](010-job-waiting-and-operation-store-ownership.md), [ADR-023](023-world-index-and-cell-format-architecture-decision.md), [ADR-105](105-navigation-asset-and-scene-ownership-boundary.md)
- **Normative documents**: [Navigation and AI Architecture](../architecture/runtime/navigation-and-ai-architecture.md), [Asset Pipeline](../architecture/runtime/asset-pipeline.md), [Concurrency and Job System](../architecture/foundation/concurrency-and-jobs.md), [Platform Abstraction](../architecture/foundation/platform-abstraction.md), [Editor Panel Host](../architecture/editor/editor-panel-host.md), [CLI Architecture](../architecture/interfaces/cli-architecture.md), [MCP Architecture](../architecture/interfaces/mcp-architecture.md)

## Context

ADR-105 separates authored navigation intent, immutable bake input, cooked
grounded NavMesh artifacts and runtime topology. It deliberately leaves the
operation that connects the first three unspecified. An Editor panel, CLI command,
MCP tool and release cook all need to request the same work without implementing
their own snapshot, cache, lock or publication rules.

Navigation baking is long-running, parallel and revision-sensitive. A Scene or
geometry asset may change while tiles are building, repeated editor changes may
produce more requests than useful results, and another Horo process may target the
same cooked output. A thread-safe in-process mutex alone cannot protect a shared
workspace, and holding the authored-project mutation lease for the whole bake would
unnecessarily block normal editing.

The generic Asset Pipeline already owns cache storage, output staging, manifests
and atomic generation publication. Foundation owns jobs, application operation
projection, resource scheduling and native durable file locks. Recast/Detour owns
none of those authorities. This ADR defines the Navigation-specific application
operation and its use of those shared contracts.

## Decision

### 1. One application service owns every navigation bake operation

The application layer owns one `NavigationBakeService` per process. The
composition root injects:

- the application `OperationCoordinator`/authoritative `OperationStore`;
- Foundation `JobSystem`, cancellation and resource-admission services;
- project/session, Scene and Asset Registry immutable snapshot capabilities;
- a host-owned backend-neutral project snapshot/mutation coordination capability;
- Platform `DurableFileSystem`/`ExclusiveFileLock`;
- the host-owned AssetCook cache/staging/publication capability; and
- one selected `INavigationMeshBuilder` plus its immutable descriptor/fingerprint.

The service admits a typed `NavigationBakeRequest` and returns either an
`OperationId` plus join/supersession disposition, or a typed admission error with no
operation record. The request names project/session identity, definition AssetId,
bake scope, grounded profiles, target/cook profile, required output closure and
bounded policy. It carries no arbitrary output path, provider pointer or UI object.

GUI, CLI, MCP and release cook are adapters over this exact admission seam:

- Editor menus, inspectors and navigation panels submit, observe and request
  cancellation; panel closure does not cancel or destroy accepted work.
- `horo-engine navigation bake` parses arguments, submits the request and renders
  the same operation/result to text, JSON or JSONL.
- the MCP navigation-bake tool validates capability and project scope, submits the
  same request and returns operation-aware progress/result data.
- play/release/package cook invokes the same service with a required closure and
  non-interactive policy; it does not call a builder directly.

Transport threads, CLI handlers and GUI callbacks never execute bake work or write
operation progress. The service/coordinator is the only writer of its operation
record. Adapters may observe through `IOperationQuery` and request cooperative
cancellation through `IOperationControl`.

### 2. Builders are pure bounded strategies, not operation owners

`INavigationMeshBuilder` receives an invocation-bounded immutable
`NavigationBakeInputSnapshot`, one tile/profile work descriptor, cancellation,
memory/scratch limits and host-owned output/diagnostic writers. It may compute
provider-private intermediate data and neutral logical output. It cannot:

- open a Scene, registry, source file, cache or output path;
- create a thread pool, task group or user-visible operation;
- acquire a project/workspace lock;
- publish a cache entry, artifact generation or runtime topology;
- choose another provider/target/profile; or
- retain input/output views after the invocation.

The service owns orchestration, stable tile order, fan-out/fan-in, progress,
supersession and cancellation. AssetCook owns cache bytes, staging and publication.
The builder returns a bounded typed result only. A provider exception/status or
allocation failure is translated at the private adapter boundary and cannot escape
or partially publish.

### 3. Admission captures desired identity; execution captures immutable input

Each accepted request receives a monotonically increasing project/workspace
`NavigationBakeRequestGeneration`. The service validates the project session,
definition/scope/profile IDs, target/provider capability, limits and authorization
before creating work.

Execution then captures the complete ADR-105 `NavigationBakeInputSnapshot` from
one coherent revision boundary. It pins project/persistent-contract version, Scene
document revision, definition/source digest, canonical contributor set, exact
geometry artifact identities/digests, Asset Registry and package-lock revisions,
settings/coordinate/tile schemas, cooker catalog and provider fingerprint. Workers
hold only owned immutable snapshot data and leases.

The service computes a canonical `NavigationBakeInputFingerprint` before any cache
lookup or builder call. Capture failure, revision ambiguity or bounds failure ends
the operation without staging. Source editing may continue after capture; it cannot
mutate the accepted attempt. Every completion carries operation ID, request
generation and input fingerprint.

Immediately before publication adoption, the owner rechecks the desired request
generation and every authoritative source revision/digest. A mismatch makes the
candidate stale or superseded. Holding a lock, completing every job or producing
valid bytes does not make stale input publishable.

### 4. Latest-wins supersession is project/scope/target keyed

The coalescing key is the canonical tuple of project identity, definition AssetId,
bake scope, grounded profile set, cook target and cook profile. Requests with a
different key may execute concurrently under global resource budgets and distinct
publication authority.

For one key:

1. a request with the same complete desired input fingerprint joins the existing
   operation and returns its `OperationId`;
2. a request with newer/different desired input creates a successor operation,
   advances the desired request generation and requests cancellation of the active
   attempt;
3. at most one active attempt and one pending successor exist; another newer
   request supersedes the pending successor before execution; and
4. only the operation whose generation still equals the desired generation may
   cross the publication-adoption barrier.

Supersession is cooperative. Tile jobs check cancellation at bounded work-unit
boundaries and retain no output authority. A candidate already built for an older
generation is discarded or quarantined and ends `Superseded`; it never enters the
active manifest. The latest successor starts after accepted children and private
staging for the prior attempt are accounted for.

Once an operation passes its final adoption check and begins the bounded atomic
pointer publication, a later request does not rewrite its history as superseded.
The committed operation ends `Succeeded`; the later request becomes the next
desired generation and may replace it normally. Latest-wins prevents known-stale
publication, not a claim that two valid generations can never be active in
sequence.

### 5. Structured concurrency and resource order are mandatory

One accepted operation owns one `TaskGroup`. Tile/profile jobs use the process
`JobSystem`; there is no navigation executor. Work is bounded by profile limits for
input bytes, tiles, profiles, vertices/polygons, scratch/resident/staging bytes,
parallel builders, diagnostics and output bytes.

Resources follow the Foundation order:

1. project/tool or publication serialization authority;
2. memory reservation;
3. filesystem/archive I/O;
4. external tool if a future admitted builder requires one;
5. transport; and
6. GPU/render handoff, which the grounded CPU baseline does not use.

No main/editor, transport or render owner blocks waiting for bake jobs or a lock.
Contention appears as an `OperationStore` `Waiting` phase with a bounded reason.
Workers release earlier resources before a bounded retry when the next resource is
unavailable. Shutdown uses ADR-010's explicitly permitted ordered drain, not an
ordinary owner-thread wait.

### 6. Unique staging and an OS-held publication lock protect the workspace

Every operation writes only beneath a host-chosen unique same-filesystem staging
directory identified by unguessable operation identity. Inputs, cache and active
generation paths are read-only to the builder. Staging rejects symlinks, traversal,
unexpected files and escape from the canonical cooked root.

The publication key is derived from the canonical active selector
(`current.json`) and generation namespace chosen by AssetCook. Any requests that
could replace the same selector derive the same key even when their definition,
scope or profile differs. Before an operation may assemble or publish that
generation, it acquires both the process resource lease and Platform
`ExclusiveFileLock` for that key. Lock text contains bounded diagnostic
owner/operation metadata only; the native handle is authority. All cooperating
GUI, CLI, MCP and cook hosts use the same lock derivation and protocol.

The lock is cancellation-aware and bounded. A busy lock reports `WaitingForLock`;
timeout ends `TimedOut`, and filesystem/permission failure ends `Failed`. Process
termination releases the native lock. The service never deletes another owner's
lock file/staging merely because metadata looks old.

The authored-project mutation lease is not held across voxelization or tile build.
Source capture uses a brief coordinated immutable snapshot; publication uses its
separate derived-workspace authority and a final revision check. When a transaction
also updates project-owned metadata, the short project mutation lease is acquired
before the publication lock in the globally documented order and released after
the metadata transaction. A lock proves exclusive writing, not input freshness.

Operations for distinct publication keys may share immutable cache reads and build
concurrently within budgets. They cannot publish into the same generation/current
pointer concurrently, even from different processes. After acquiring the lock, an
incremental operation pins and validates the then-current base manifest, carries
forward unchanged entries, applies its complete requested replacement closure and
validates the resulting full generation. A changed base is never overwritten using
a manifest assembled before the lock.

### 7. The cache key includes every byte- or meaning-affecting input

Navigation uses a versioned dependency-aware key; it must not masquerade as the
dependency-free generic `CacheKeyV1`. `NavigationBakeCacheKeyV1` contains:

- source definition AssetId/type, exact source/metadata digest and schema revision;
- project/persistent-contract version, Scene/bake-scope revision and canonical
  stable contributor identity/payload digest;
- canonically ordered geometry/source artifact identity, type, revision and digest;
- exact grounded profile, area, filter, modifier and off-mesh-link tables;
- build settings, tile/partition schema, units, coordinate/origin and numeric policy;
- Asset Registry, package lock and relevant registered schema/catalog revisions;
- navigation cooker/builder contribution ID and semantic algorithm version;
- exact ADR-104 provider source/options/compiler/architecture/FP fingerprint for
  every provider-dependent output;
- typed cook target/profile, neutral/provider payload format versions and standard
  artifact-envelope version; and
- canonical dependency-closure digest and cache-key schema version.

Canonical encoding is fixed-width or length-delimited, versioned and stably sorted.
Source paths, filenames, timestamps, editor selection/camera, panel state, request
origin, progress messages, operation/job IDs, thread count and worker completion
order are excluded.

Cache entries are immutable and content-addressed. Profile/tile granularity may be
used only when each entry includes its complete dependency subset and the final
artifact manifest covers the full requested closure. A hit is accepted after the
same envelope, requested-key, digest, format, bounds and semantic validation as
fresh builder output. Corrupt, truncated, oversized, symlinked or wrong-key entries
are quarantined/ignored according to typed policy and never become active output.

Competing writers stage unique cache files and publish without overwriting an
existing key. The loser verifies byte-identical valid existing content before
discarding its candidate. Cache presence is neither operation success nor artifact
publication authority.

### 8. Artifact publication is one last-valid transaction

After all required tiles/profiles are built or verified cache hits, the service and
AssetCook perform:

1. deterministic neutral/provider payload and dependency-closure validation;
2. final desired-generation and source-revision revalidation;
3. publication-lock acquisition, current-base-manifest capture and revalidation
   after any wait;
4. complete candidate generation assembly in unique same-filesystem staging,
   carrying forward verified unchanged entries only;
5. deterministic manifest creation and verification of every envelope/key/digest,
   output bound, path and required cell/package projection;
6. durable flush of files/directories required by platform policy;
7. content-addressed generation-directory publication without overwriting
   different bytes; and
8. atomic replacement of `current.json` last, followed by parent-directory sync.

`current.json` is the sole active-generation selector. No partial tile/profile
publication is visible. Failure or cancellation before its atomic replacement
leaves the prior pointer and immutable provider generation unchanged. After a
successful replacement, existing runtime readers keep the old generation alive by
lease until Scene/cell replacement and queries drain.

Release/package consumers pin and reverify the published manifest. They do not read
staging or accept a cache entry as a published generation. A required release cook
fails rather than packaging the last-valid generation when it does not match the
requested input fingerprint.

### 9. Operation phases and terminal states are explicit

The observable domain phases are `Queued`, `Capturing`, `WaitingForResources`,
`WaitingForLock`, `CheckingCache`, `Building`, `Validating`, `Staging`,
`Publishing` and `Finalizing`. Progress is monotonic within a phase and may reset
only on phase advance. A latest-wins successor is a new operation, not a silent
change to an earlier operation's captured inputs.

Every accepted operation reaches exactly one immutable terminal state:

| Terminal | Meaning | `OperationStore` projection |
|---|---|---|
| `Succeeded` | The requested fingerprint is the active verified generation; all-cache-hit is still success | `Succeeded` |
| `Failed` | Validation, builder, cache policy, I/O, permission, capacity or publication failed | `Failed` with typed error |
| `Cancelled` | Explicit/shutdown cancellation won before adoption/publication | `Cancelled` |
| `Superseded` | A newer desired generation made this operation ineligible to publish | `Cancelled` with `navigation.bake.superseded` |
| `TimedOut` | A declared resource/lock/phase deadline expired | `Failed` with `navigation.bake.timed_out` |

Admission rejection creates no operation. A joined identical request receives the
existing operation rather than another terminal record. Cancellation after the
atomic publication point is an idempotent already-committed outcome, so the
operation remains `Succeeded`. Terminal result includes input fingerprint, active
manifest/artifact digest when successful, bounded diagnostics, cache/build counts,
and recovery guidance where applicable.

### 10. Recovery never guesses an active artifact

Startup/retry recovery runs before admitting a writer for the same publication key
and under its publication lock:

- incomplete unique staging with no committed receipt is validated then deleted or
  quarantined according to retention policy;
- a fully published content-addressed generation not referenced by `current.json`
  is an inactive orphan, never selected by timestamp or directory order;
- if `current.json` and its manifest validate, they remain authoritative regardless
  of newer orphan/staging content;
- if `current.json` is missing, malformed or points to invalid content, the service
  reports `navigation.bake.current_generation_invalid` and does not silently choose
  an older directory; and
- explicit repair may restore a prior generation only from a retained successful
  publication receipt after full key/envelope/manifest validation, or may recook the
  requested source. Repair itself is a locked operation with a receipt.

The last valid generation and its leases are retained until the new pointer is
durably committed. Garbage collection runs separately after locks/read leases and
rollback retention allow it; it cannot remove current, staged-by-live-owner or
leased generations. Cache cleanup likewise cannot mutate artifact publication.

A retry captures current source revisions and starts a new operation. It does not
resume provider stack/scratch or trust unverified partial tile output from the
failed process. Verified immutable cache entries may be reused normally.

### 11. Shutdown preserves terminal truth and workspace safety

Shutdown closes bake admission, marks pending successors `Cancelled`, requests
cooperative cancellation for active task groups, stops further cache/staging
writes, drains accepted children through the host shutdown policy, and releases
output/cache/source leases before builder/provider destruction.

An operation that did not cross the publication point becomes `Cancelled` unless a
prior typed failure already won. An operation whose `current.json` replacement
committed remains `Succeeded`; shutdown cannot relabel committed output. Owned
incomplete staging is removed or left with bounded recovery metadata only after all
writers close. OS locks are released last for that workspace. No callback,
operation writer or builder input may outlive the service/coordinator/provider it
references.

### 12. Qualification covers concurrency, failure and recovery

Required evidence includes:

- equivalent GUI, CLI, MCP and release-cook requests reaching one service and
  producing the same fingerprint/result/error semantics;
- panel/transport/client closure without operation lifetime loss and cancellation
  only through the operation control seam;
- identical-request joining, active and pending latest-wins supersession, bounded
  cancellation latency and no stale generation publication;
- source/Scene/registry/package/settings changes during capture, build, lock wait,
  validation and immediately before adoption;
- deterministic output/cache keys under reordered contributors, tiles and worker
  completion, plus invalidation for every enumerated semantic input;
- fresh build versus cache-hit byte identity, mixed tile hits/misses, corrupt/wrong-
  key/oversized/symlink cache entries and competing cache writers;
- same-process and cross-process publication contention, lock timeout, owner death,
  cancellation while waiting and proof that no two writers mutate one workspace;
- failure/cancellation injection before and after every staging, flush, rename and
  `current.json` step with the last valid generation retained;
- interrupted staging, inactive orphan, invalid pointer/manifest, explicit verified
  rollback/recook and garbage collection under live leases;
- exact terminal-state race precedence, immutable terminal snapshots and typed
  recovery guidance; and
- bounded memory/output/parallelism, allocation failure, provider error, shutdown
  during every phase and sanitizer/race coverage.

## Consequences

### Positive

- All product surfaces share one reproducible navigation bake and operation model.
- Immutable revisions plus latest-wins prevent known-stale editor work from
  publishing while allowing authoring to continue.
- OS-held locking and unique staging protect local and cross-process workspaces.
- Complete keys make cache reuse safe across profiles, tiles, provider builds and
  source changes.
- Atomic pointer publication and explicit recovery preserve the last valid artifact
  through cancellation, crashes and partial writes.

### Costs

- The application service must coordinate source snapshots, operation state,
  task-group work, resource admission and AssetCook transactions.
- Cross-process locks and durable publication add platform-specific failure and test
  cases.
- Exact dependency/fingerprint capture increases manifest and diagnostic data.
- Rapid edits may consume some bounded work before cooperative supersession reaches
  a tile boundary.

## Rejected Alternatives

### Let the Navigation panel own bake jobs

Rejected because panel closure, layout restoration and multiple editor surfaces
would control or duplicate operation lifetime. CLI, MCP and release cook would then
need different implementations.

### Let Recast/Detour read files and publish output

Rejected because a provider has no project, cache, migration, security, staging or
generation authority. It would couple artifacts and operation lifetime to one
backend.

### Hold the authored-project mutation lock for the entire bake

Rejected because immutable capture and final revision validation make long source-
write exclusion unnecessary. Blocking saves/imports for voxelization duration would
harm editor responsiveness without proving cross-process cooked-output safety.

### Use only an in-process mutex

Rejected because CLI, Editor, CI and MCP hosts may be separate processes sharing the
same project/build root. Native OS-held locking is required for publication.

### Queue every edit and publish in arrival order

Rejected because obsolete intermediate revisions would consume unbounded work and
briefly replace newer intent. One active plus one latest pending successor bounds
the queue and prevents known-superseded adoption.

### Treat a cache hit as an active artifact

Rejected because cache identity does not prove complete generation closure,
publication, target compatibility or current-pointer authority. Hits pass normal
validation and the same publication transaction.

### Overwrite tiles or `current.json` incrementally

Rejected because readers could observe mixed source revisions and failure could
destroy the last valid topology. One immutable generation and final atomic pointer
replacement are the only activation boundary.

### Recover by choosing the newest directory

Rejected because timestamps and directory order are not commit evidence. Recovery
uses the validated current pointer, a retained successful receipt or an explicit
recook.
