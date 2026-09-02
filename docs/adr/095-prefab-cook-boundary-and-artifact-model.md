# ADR-095: Prefab Cook Boundary and Artifact Model

- **Status**: Proposed
- **Date**: 2026-09-02
- **Supersedes**: None
- **Scope**: Prefab source, effective-resolution candidate, expanded-scene and spawnable-template artifact boundaries; AssetCook ownership; deterministic cache inputs; generation publication; migration, hot reload, shipping and failure retention
- **Issue**: [PFB-007.1](https://github.com/abdullahbodur/horo-engine/issues/1066)
- **Jira**: [HORO-1066](https://horo-engine.atlassian.net/browse/HORO-1066)
- **Related**: [ADR-017](017-prefab-role-ownership-and-capability-tiers.md), [ADR-093](093-prefab-override-property-identity-and-delta-operations.md), [ADR-094](094-prefab-nested-composition-and-variant-inheritance.md)
- **Normative documents**: [Prefab Architecture](../architecture/runtime/prefab-architecture.md), [Asset Pipeline](../architecture/runtime/asset-pipeline.md), [Project Versioning and Migration](../architecture/foundation/project-versioning-and-migration.md), [Scene Runtime](../architecture/runtime/scene-runtime.md)

## Context

Horo needs the same prefab source to support static scene placement and dynamic
runtime spawning. Static instances should become ordinary scene definitions before
runtime activation, while dynamic roots need immutable `CookedPrefab` artifacts.
Treating both as one artifact would either ship unnecessary templates or force
runtime to parse authoring data. Letting Scene Cook and Prefab Cook each implement
nested/variant expansion would create two authorities with potentially different
identity, override and error behavior.

The generic Asset Pipeline already owns cooker contributions, dependency-aware
cache identity, staging and immutable generation publication. Project migration
owns durable source upgrades. ADR-094 owns effective prefab resolution. The missing
contract is the boundary among those owners and the exact artifact installed or
shipped for each use.

Cook and hot reload are fallible and asynchronous. A cancelled job, corrupt cache
entry, stale source revision or failed `current.json` replacement must not remove a
working generation. Conversely, recovery must not guess that an arbitrary inactive
directory is valid. This ADR fixes the artifact and generation rules before
implementation.

## Decision

### 1. Four representations have distinct ownership

| Representation | Durable | Owner | Consumer | Standard release |
|---|---:|---|---|---:|
| `PrefabDocument` plus identity sidecar | Yes, project source | Project migration and Prefab authoring | Editor, Prefab resolver | No |
| `EffectivePrefabCandidateV1` | No, immutable derived candidate | Prefab domain resolver | Viewport, Scene Cook, Prefab Cook | No |
| Expanded `RuntimeSceneDefinition` inside a cooked scene | Yes, cooked generation | Scene Cook contribution through AssetCook | Scene Runtime | Yes when scene is packaged |
| `CookedPrefab` (`core.prefab`) | Yes, cooked generation | Prefab Cook contribution through AssetCook | Runtime prefab spawn | Only for dynamic spawn closure |

`PrefabDocument` is canonical project data governed by `ProjectVersion`. It stores
stable IDs, semantic graph edges and override records, not runtime handles or
platform payloads. The source and sidecar move through the project migration
transaction before any cook snapshot can reference the new version.

`EffectivePrefabCandidateV1` is ADR-094's fully resolved, bounded hierarchy plus
dependency/provenance evidence. It is pinned to exact registry, source, project,
schema and resolver revisions. It may be memoized under its complete fingerprint,
but it is neither an Asset Registry record nor a second authoring source. It cannot
be edited, independently published, packaged or loaded by runtime.

Scene Cook consumes the candidate for each static placement, applies the stable
scene placement scope and emits ordinary entity/component definitions into the
scene's `RuntimeSceneDefinition`. No prefab graph or override interpreter remains
in the cooked scene.

Prefab Cook consumes the candidate for a dynamic root and emits one immutable
`CookedPrefab`. Its flat hierarchy uses dense internal tables while retaining the
stable source evidence needed for diagnostics. Runtime allocates fresh scene entity
IDs on spawn; cooked slots are not runtime handles.

### 2. One resolver prevents duplicate expansion authority

The Prefab domain owns source parsing, semantic validation, ADR-093 override
application, ADR-094 graph traversal, scoped identity construction, canonical
hierarchy order and `EffectivePrefabCandidateV1`. Scene Cook and Prefab Cook invoke
that same versioned resolver contract. They cannot re-walk nested sources, reapply
variants or invent fallback values.

Scene Cook owns scene placement identity, scene-wide collision checks and encoding
the final `RuntimeSceneDefinition`. Prefab Cook owns the `CookedPrefab` schema,
dense table encoding and runtime-template compatibility header. Neither owns job
scheduling, cache paths, generation activation or release archives.

Generic AssetCook owns bounded scheduling, immutable input views, contribution
selection, cache lookup/validation, output writers, manifest construction, staging,
atomic generation publication and rollback. It treats Prefab/Scene domain outputs
as bounded typed contribution data and never interprets prefab hierarchy semantics.

Editor viewport projection may use the same effective candidate directly. It does
not read a partially written cooked artifact and does not become a third cooker.
Packaged Scene Runtime and prefab spawn never resolve source graphs.

### 3. Phase order and snapshots are explicit

A cook operation proceeds as follows:

1. Project open/release preparation completes any authorized source migration and
   publishes the durable project transaction. Cook never migrates or rewrites
   source files.
2. AssetCook captures one immutable Asset Registry snapshot, project version,
   package lock, contribution catalog, typed cook profile/target and cancellation
   generation.
3. The Prefab resolver validates and resolves every requested root against that
   snapshot. Any missing revision, graph error, conflict, orphan required by cook or
   bound failure rejects the candidate.
4. Scene Cook and Prefab Cook transform accepted effective candidates into their
   distinct logical outputs through host-owned bounded writers.
5. AssetCook validates envelopes, complete dependency closure, expected keys,
   payload digests, sizes and target compatibility.
6. The host stages the whole requested generation, validates its deterministic
   manifest and atomically replaces `current.json` last.
7. Packaging reads only the pinned published generation and verifies its manifest
   again before constructing release chunks.

Worker results carry the operation ID and every captured revision. Completion that
does not match the active operation snapshot is stale and discarded. A source,
sidecar, package lock, settings or contribution change starts a new operation; it
does not mutate the inputs of work already running.

### 4. Cache identity covers every semantic input

Prefab-derived outputs use Asset Pipeline's dependency-aware canonical key. The
key contains at least:

- root `AssetId`, exact source/content digest, source metadata digest and
  `ProjectVersion`;
- canonically ordered transitive prefab and resource `AssetId`, type, revision and
  artifact/source digests;
- ADR-094 graph/resolver schema version, ordered edge kinds, placement IDs and
  canonical override-set digests;
- component/property/schema registry and persistent migration contract revisions;
- cooker contribution ID/version, Prefab or Scene output schema/version and
  standard artifact-envelope version;
- typed target, effective cook profile/settings and feature/capability fingerprint;
- for expanded scenes, the scene source revision, placement scope and complete
  scene conversion schema;
- for `CookedPrefab`, `cookedFormatVersion` and dynamic-spawn policy version.

Paths, timestamps, UI state, selection, preview camera, diagnostics order, native
handles and worker scheduling are excluded. Canonical bytes use schema-defined
ordering and fixed encodings. Identical complete inputs must produce byte-identical
logical payloads and manifests on supported hosts.

A dependency-free key rejects prefab-derived work with semantic dependencies. Cache
hits are accepted only after envelope identity, requested-key equality, payload
digest, bounds and target compatibility validation. A corrupt or mismatched entry
is a cache miss plus diagnostic, never publishable input.

### 5. Publication retains the last valid generation

Fresh and cache-reused outputs enter a new immutable candidate generation. The
active generation remains readable while jobs run and while the candidate is
verified. Cancellation, cooker failure, missing dependency, corruption, stale
completion, disk exhaustion, manifest failure or publication failure deletes or
quarantines candidate staging and leaves the prior `current.json` and provider
snapshot unchanged.

Replacing `current.json` is the only activation point. A successful replacement
selects the complete new manifest; readers already holding leases keep the prior
immutable generation alive until release. Inactive generation directories are not
implicitly current and are never selected by newest timestamp or directory order.
If startup cannot validate `current.json` and its manifest, it reports a typed
recovery error; it does not guess an older generation.

Release publication is another host-owned transaction over a pinned cooked
generation. Failure preserves the previous release output and never edits the cook
generation. Project source migration, cooked-generation publication and release
publication are separate transactions with explicit receipts; success in one does
not imply success in the next.

### 6. Hot reload installs by artifact role

Development hot reload prepares and validates a complete replacement cooked
generation before provider activation. Runtime never receives source-change events
as permission to parse, migrate or cook.

- A newly activated `CookedPrefab` generation affects later spawn requests. Entities
  already spawned retain their current components and lifecycle; replacing them
  requires an explicit Scene/Gameplay transaction.
- A changed static prefab produces a new cooked scene candidate. An active scene is
  replaced only through the normal scene reload/activation transaction; prefab hot
  reload cannot patch its expanded entities behind Scene Runtime's ownership.
- Editor documents and viewport candidates follow ADR-094 source re-resolution and
  Editor transactions independently of runtime artifact activation.

Cancellation or activation failure keeps the previous provider and scene. Late
loads pin their captured provider generation and cannot publish into a newer scene
or masquerade as the new artifact revision.

### 7. Shipping is reachability- and profile-driven

Standard release profiles never ship raw `.prefab`, identity sidecars, editor
provenance tables or `EffectivePrefabCandidateV1`. A developer source bundle may
include source explicitly, but runtime still uses only published cooked artifacts.

Every packaged scene includes its statically expanded prefab content inside the
cooked scene artifact. A separate `CookedPrefab` is included only when a declared
dynamic-spawn root reaches it through the locked release dependency graph. Static-
only placement does not force a duplicate runtime template. The dynamic closure
includes all resource dependencies required by the flattened template; it does not
ship transitive raw prefab sources.

Missing required closure, unresolved override data or incompatible runtime template
format fails package construction. Packager cannot silently omit a required dynamic
template, fall back to source or substitute the prior target's bytes.

### 8. Qualification covers phase and failure boundaries

| Category | Scenario | Expected outcome |
|---|---|---|
| Valid | Same prefab is static in a scene and a dynamic spawn root | Scene embeds ordinary definitions; one separate `CookedPrefab` serves dynamic spawn |
| Valid | Static-only nested/variant prefab graph | Cooked scene ships flattened content; no redundant runtime template/source ships |
| Valid | Dynamic variant root with transitive resources | One flat `CookedPrefab` and complete resource closure publish under exact revisions |
| Boundary | Maximum accepted graph/object/payload sizes | Exact bounds produce deterministic artifacts without unbounded allocation |
| Boundary | Next dependency/output byte exceeds a limit | Candidate fails before manifest activation; prior generation remains current |
| Malformed | Source graph conflict, orphan, cycle or incompatible revision | Resolver rejects both consumers consistently; no artifact publishes |
| Malformed | Cache envelope key or payload digest differs | Entry rejected; recook or typed failure, never cache publication |
| Lifecycle | Cancel/fail after some artifacts are staged | Staging is inactive; prior `current.json` and provider remain unchanged |
| Lifecycle | Source/registry/settings change while cook runs | Stale completion is discarded; new operation captures new snapshot |
| Lifecycle | `current.json` replacement or release publication fails | Prior cook/release generation remains authoritative |
| Reload | New `CookedPrefab` activates with existing spawned entities | Future spawns use new lease; existing entities are not implicitly patched |
| Reload | Static source changes while scene is active | New scene candidate requires scene activation transaction |
| Shipping | Standard release is inspected | No raw prefab/sidecar/effective candidate; only reachable cooked artifacts |
| Determinism | Same complete inputs cook on supported hosts | Byte-identical payload, key and manifest |

## Consequences

- Scene and spawnable-template outputs share one semantic resolver without sharing
  artifact roles or runtime ownership.
- Static-only prefabs do not create redundant shipped templates, while declared
  dynamic roots remain spawnable without authoring data.
- Complete cache keys and immutable generation activation make cancellation,
  corruption and hot reload fail-safe.
- Source migration, cook publication, release publication and runtime scene reload
  remain separate explicit transactions.
- Cook implementations must expose the resolver and both output schemas as narrow
  typed contributions rather than embedding expansion logic in generic Assets.

## Alternatives Considered

### Ship every prefab as both scene-expanded data and `CookedPrefab`

Rejected because static-only content would be duplicated and release reachability
would no longer describe runtime requirements.

### Let Scene Cook and Prefab Cook expand independently

Rejected because identity, precedence, error handling and cache invalidation could
diverge for the same source snapshot.

### Publish successful artifacts individually

Rejected because runtime and packaging could observe a mixture of source revisions
and partial dependency closure.

### Fall back to the newest inactive generation after corruption

Rejected because directory presence or timestamp is not proof of a valid committed
manifest. Recovery requires an explicit verified authority.

### Hot-patch existing entities when a template reloads

Rejected because spawned and static scene entities belong to Scene Runtime and
Gameplay lifecycles, not the Asset provider.
