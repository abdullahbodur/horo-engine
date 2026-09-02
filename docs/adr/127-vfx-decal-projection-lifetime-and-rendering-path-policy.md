# ADR-127: VFX Decal Projection, Lifetime and Rendering Path Policy

- **Status**: Proposed
- **Date**: 2026-09-02
- **Supersedes**: None
- **Scope**: Decal box/oriented-box placement, scene projection data, permanent/timed/event-driven lifetime, count admission, fade/removal authority, deferred-default and forward fallback policy
- **Issue**: [VFX-006.1](https://github.com/abdullahbodur/horo-engine/issues/1754)
- **Jira**: [HORO-1711](https://horo-engine.atlassian.net/browse/HORO-1711)
- **Related**: [ADR-011](011-vfx-effect-ownership-simulation-domain-and-renderer-boundary.md), [ADR-012](012-world-streaming-partition-authority-and-subsystem-boundaries.md), [ADR-026](026-large-world-precision-and-floating-origin-strategy.md), [ADR-034](034-gpu-memory-and-residency-ownership.md), [ADR-036](036-raster-render-path-and-quality-architecture.md), [ADR-126](126-vfx-graph-compilation-and-runtime-representation-convergence.md)
- **Normative documents**: [VFX and Particles Architecture](../architecture/runtime/vfx-and-particles-architecture.md), [Rendering Architecture](../architecture/runtime/rendering-architecture.md)

## Context

ADR-011 already owns the live boundary: scene-owned `VfxWorld`/`DecalManager` owns
logical decal instances, fades and atlas requests; Renderer owns physical atlas/image
storage, graph passes and deferred retirement. ADR-036 maps a deferred decal request to
an admitted forward-decal substitute when the active raster recipe is not Deferred.

The remaining model is underspecified. The VFX architecture shows one descriptor with
`BoxProjection`, `OrientedBox`, a lifetime float and a permanent bool, which cannot
represent event removal, owner scope, clock/fade semantics or invalid combinations.
It also does not define world-versus-owner-local placement, admission for permanent
and event-driven instances, or when deferred-to-forward remapping is compatible rather
than silent visual degradation.

This decision specializes projection and lifetime only. It deliberately does not add
a scene component owner, renderer-owned logical decal world or independent decal
scheduler beside ADR-011.

## Decision

### 1. ADR-011 ownership remains unchanged

An authored scene decal component is an inert placement descriptor. Scene conversion
submits it to the owning scene incarnation's `DecalManager`; the component does not own
a live renderer object. Effect-spawned decals enter the same logical store and use the
same identity, placement, lifetime, admission and rendering-path contracts.

`DecalManager` is the only live logical owner. It creates generation-safe `DecalId`,
advances timed fades, accepts authorized removal commands, closes admission and emits
immutable `DecalRenderBatch` values. Renderer consumes those values, owns native
resources/passes and acknowledges retirement. Neither editor components nor render
backends maintain a second authoritative lifetime/count registry.

### 2. Projection and placement are typed separately

The compiled descriptor uses:

```cpp
enum class DecalProjectionMode : uint8_t {
    Box,
    OrientedBox
};

enum class DecalPlacementSpace : uint8_t {
    WorldLocked,
    OwnerLocal
};

struct DecalPlacementDescriptor {
    DecalProjectionMode projection;
    DecalPlacementSpace space;
    Transform64 localOrWorldTransform;
    Vec3 positiveHalfExtents;
    DecalReceiverMask receivers;
    float normalFadeCosine;
    float distanceFadeStart;
    float distanceFadeEnd;
};
```

`Box` is axis-aligned in the declared placement space: its rotation is identity after
canonicalization. `OrientedBox` admits a normalized orthonormal rotation plus
translation and positive half extents. Neither mode permits shear, perspective,
nonfinite values, degenerate extents or an arbitrary mesh/frustum. Scale is folded into
half extents during cook/conversion.

`WorldLocked` captures canonical ADR-026 world position/orientation and does not follow
a source entity after spawn. `OwnerLocal` retains a generation-safe scene entity/bone
owner and local transform; SceneRuntime supplies the resolved immutable world transform
at extraction. A stale/destroyed owner removes the decal at the owner boundary. Raw ECS
pointers, renderer transforms and native handles are forbidden.

Spatial-cell decals also carry ADR-012 cell/partition fences. Cell unload closes their
admission and logical visibility; renderer resources retire asynchronously after all
snapshots/fences complete. Rebase changes only extracted render-relative transforms,
not durable decal identity or lifetime.

### 3. Lifetime is one tagged policy, never bool-plus-float combinations

```cpp
struct PermanentDecalLifetime {};

struct TimedFadeDecalLifetime {
    VfxClockDomain clock;
    float visibleSeconds;
    float fadeSeconds;
    VfxCurveId fadeCurve;
};

struct EventDrivenDecalLifetime {
    DecalRemovalChannelId channel;
    DecalRemovalKey key;
};

using DecalLifetimePolicy = std::variant<
    PermanentDecalLifetime,
    TimedFadeDecalLifetime,
    EventDrivenDecalLifetime>;
```

Exactly one variant is present. Cook rejects negative/nonfinite time, invalid curves,
unknown clock domains, missing event channel/key and legacy combinations such as
`isPermanent=true` with a finite expiration.

### 4. Each lifetime mode has one removal authority

| Lifetime | Visibility/fade | Removal authority |
|---|---|---|
| `Permanent` | Full authored opacity while its scene/entity/cell owner scope is active; distance/angle rendering fade may cull but does not age it | `DecalManager` removes on explicit authorized scene edit/delete, owning entity/cell/scene retirement or project/runtime teardown |
| `TimedFade` | Full opacity for `visibleSeconds`, then sampled authored fade curve for `fadeSeconds` on the declared clock; zero-length visible or fade is valid immediate boundary behavior | `DecalManager` advances the logical clock and removes exactly at completed fade or earlier owner-scope retirement |
| `EventDriven` | Full opacity until one matching admitted removal command; optional rendering distance/angle fade remains visual only | `DecalManager` validates owner/channel/key/generation at its command cutoff and removes once; owner-scope retirement remains unconditional |

Pause and time scale follow the declared VFX clock. Wall time, render cadence, view
count and GPU completion never advance lifetime. Renderer receives only the current
immutable fade factor and cannot retain, expire or resurrect a logical decal.

Removal is idempotent for the same current `DecalId`/command identity: the first
accepted removal closes publication; duplicate/stale generations return typed status
and do not target a reused slot. Event commands arriving during a cutoff apply at the
next eligible owner boundary and never mutate an extracted snapshot.

### 5. Permanent and event-driven do not bypass finite capacity

Every compiled effect/scene descriptor declares maximum concurrent decals and per-
spawn upper bounds. The active `VfxQualityPolicy` supplies finite per-effect, owner/
cell and aggregate limits; the existing desktop aggregate default remains 256 decals.
Permanent and event-driven instances consume a slot for their full owner lifetime.

`TrySpawnDecal` admits identity, logical slot, result/retirement record, material/atlas
reference and renderer cost before publication. Capacity exhaustion rejects the
incoming decal with `DecalCapacityExceeded`; it does not evict an existing permanent,
oldest or farthest instance and does not grow a pool in the frame path. Optional effect
logic may observe rejection and follow a separately authored cosmetic response.

Gameplay-required outcomes cannot depend on decal visibility or successful visual
admission. Decals remain visual; a bullet-hit/gameplay fact exists independently on
the CPU authority path. Reserved gameplay VFX work cannot be consumed by permanent
cosmetic decal growth.

### 6. Deferred projection is the default preference

Compiled decals declare backend-neutral path intent:

```cpp
enum class DecalPathPreference : uint8_t {
    PreferDeferred,
    RequireDeferred,
    PreferForward,
    RequireForward
};

enum class ResolvedDecalPath : uint8_t {
    DeferredProjection,
    ForwardProjection
};
```

Absent/legacy path intent migrates to `PreferDeferred`. The asset stores semantic
requirements and compatible material variants, not GBuffer attachments, draw APIs,
native blend state or backend names.

`DeferredProjection` is eligible only when the resolved ADR-036 recipe is Deferred,
admits the required deferred-decal capability/GBuffer semantics and has complete
material/resource/budget variants. It executes in the dedicated deferred-decal pass
after GBuffer production and before affected lighting; it does not write scene depth.

`ForwardProjection` uses the recipe's dedicated bounded forward-decal path/list and
compatible material variant. It is available on Forward/Clustered Forward+ tiers and
may be explicitly selected in a hybrid Deferred frame. Its exact receiver/light/list
capabilities and limits must be present; "forward" is not permission for an unbounded
per-object scan or immediate draw.

### 7. Path fallback is explicit, compatible and resolved with the raster recipe

Resolution uses the same immutable recipe/capability/budget snapshot as rendering:

| Preference | Resolution |
|---|---|
| `PreferDeferred` | Admitted deferred path; otherwise admitted authored forward variant; otherwise typed unavailability |
| `RequireDeferred` | Admitted deferred path or typed recipe/capability/variant/budget failure; no forward remap |
| `PreferForward` | Admitted forward path; otherwise admitted authored deferred variant when the active recipe supports it; otherwise typed unavailability |
| `RequireForward` | Admitted forward path or typed failure, including on Deferred recipes without the required hybrid path |

An authored compatibility record binds both material variants, receiver semantics,
normal/distance fade, color/normal/roughness/emissive channel expectations and a finite
visual-difference envelope. Missing channels/features cannot be silently discarded.
Optional cosmetic decals may instead declare complete suppression as a fallback;
required visual content fails/suspends its owning effect.

Resolution records requested/resolved path, raster recipe, capability/policy/material
generations, failed predicates, fallback rule and reserved cost. It occurs before
activation or during an explicit separately admitted safe-point replacement. A path
change does not reinterpret an active native resource or render both paths in one view.

Forward-only tiers therefore use the authored forward projection when admitted;
Deferred remains the default preference, not an engine-wide requirement. Missing
deferred capability never switches the selected graphics backend or raster recipe just
to preserve one decal.

### 8. Logical lifetime is independent of rendering availability

View culling, receiver-mask rejection, distance/angle fade, missing optional path and
temporary renderer suspension do not advance or remove a logical decal. `DecalManager`
continues the selected lifetime policy under its owner clock and exposes typed visual
status. A later compatible render generation may display the still-live instance.

A required-path failure at initial admission creates no logical instance. A runtime
device/path loss follows the owning effect's authored suspend/stop/substitute policy;
it cannot silently mark a timed decal expired. Logical removal immediately closes new
extraction, while old snapshots/native atlas references remain leased and charged
until renderer acknowledgement.

### 9. Material/atlas and rendering ownership stay below the snapshot seam

The compiled descriptor references Horo `MaterialId`/resource requirements and logical
atlas content. `DecalManager` emits bounds, resolved immutable transform, fade factor,
receiver mask and semantic path/material IDs in `DecalRenderBatch`. It never packs a
native atlas, binds a GBuffer, chooses a pipeline or submits a pass.

RenderFrontend validates the batch against the resolved recipe and admitted resources.
Renderer owns physical atlas packing/upload, target-specific formats, graph resources,
native synchronization and deferred destruction under ADR-034. Atlas replacement
charges old/new overlap; a logical removal is not evidence that GPU readers finished.

### 10. Failures are typed and generation-safe

Cook/conversion failures include `DecalProjectionInvalid`, `DecalLifetimeInvalid`,
`DecalPathVariantMissing`, `DecalReceiverContractInvalid` and checked size/count/cost
overflow. Admission/runtime failures include `DecalCapacityExceeded`,
`DecalPathUnavailable`, `DecalMaterialUnavailable`, `StaleDecalGeneration`,
`DecalRemovalUnauthorized` and renderer/resource causes.

Results retain decal/effect/owner/cell/material, requested/resolved path, lifetime,
clock, recipe/capability/policy generations and failed predicate. Required failures do
not produce partial logical/native state. Optional suppression/rejection is explicit,
not successful rendering with missing channels or a silently changed path.

### 11. Qualification covers placement, lifetime, capacity and paths

Required implementation evidence includes:

- Box and OrientedBox in WorldLocked/OwnerLocal spaces, nonfinite/zero/negative/sheared
  transforms, owner/bone destruction, large-world rebase and stale cell fences;
- each lifetime at zero/exact/one-tick-beyond boundaries, pause/time scale, duplicate/
  stale/unauthorized event removal and owner/cell/scene teardown;
- per-effect/owner/cell/aggregate limits at 255/256/257 desktop-default cases, permanent
  and event-driven saturation, incoming rejection and no implicit eviction/growth;
- every path-preference row across Forward, Clustered Forward+ and Deferred recipes,
  missing capability/material/channel/budget and explicit suppression;
- identical semantic receiver/fade fixtures for qualified deferred/forward variants
  within their declared visual envelope, with no depth write or double rendering;
- renderer suspension/device loss independent from timed lifetime, logical removal
  closing extraction and old snapshot/atlas leases retiring after fences; and
- backend-neutral public/snapshot data scans plus native image tests per shipped path.

## Consequences

### Positive

- Projection volume and placement semantics are unambiguous and generation-safe.
- Every lifetime mode has one clock/removal owner and finite admission behavior.
- Deferred is the clear default while forward-only tiers have an explicit compatible path.
- Scene and effect decals share one live owner, pool, batch and retirement contract.
- Renderer-native atlas/pass details remain private.

### Costs

- Legacy bool/float lifetime and path fields require cooked-schema migration.
- Permanent/event-driven content must reserve finite worst-case concurrent capacity.
- Deferred and forward variants need authored compatibility data and image qualification.
- Path replacement/atlas reload may temporarily charge both generations.

## Rejected Alternatives

### Give scene components and effects separate live decal managers

Rejected because ADR-011 already assigns one logical owner; separate stores would
duplicate identity, limits, fades, extraction and retirement.

### Keep `isPermanent` plus `lifetimeSeconds`

Rejected because contradictory values cannot represent event authority, clock domain
or fade boundaries. One tagged lifetime policy is exhaustive and validated.

### Evict the oldest or farthest decal when the pool is full

Rejected as an implicit policy that can remove permanent/event-owned content and make
results view-dependent. Reject incoming work unless a future explicit policy says otherwise.

### Treat deferred decals as mandatory on every renderer tier

Rejected because Forward/Clustered Forward+ recipes may lack a compatible GBuffer.
Default preference resolves to an authored bounded forward variant or typed failure.

### Silently remap `RequireDeferred` to forward

Rejected because required appearance/capability intent would be lost. Only preference
edges with declared compatible variants may fall back.

### Let Renderer expire decals or own logical placement

Rejected because render cadence/device lifetime cannot author scene/effect state.
Renderer consumes immutable batches and owns only physical resources/passes.
