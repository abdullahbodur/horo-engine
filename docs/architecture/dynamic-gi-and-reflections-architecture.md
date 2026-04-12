# Dynamic GI And Reflections Architecture

## Intent

Define the backend-neutral runtime architecture for dynamic global illumination (GI) and reflections so follow-up implementation issues can ship in layered milestones without reworking renderer ownership boundaries.

## Scope

This document defines:

- pass ordering for the dynamic GI and reflections pipeline
- resource ownership and lifetime boundaries
- capability-driven fallback policy
- quality tiers exposed to runtime/editor
- execution contract for milestone issues `#165+`

This document does **not** lock one GI algorithm forever. It locks integration seams.

## Pipeline Pass Ordering (Frame Graph Contract)

The frame graph order for lit runtime rendering should be:

1. **DepthPrepass** (optional per quality tier, required for high tiers)
2. **GBuffer/Opaque** (or forward equivalent where deferred path is unavailable)
3. **DirectLighting**
4. **ReflectionTrace**
5. **GITrace**
6. **GIDenoiseTemporal**
7. **ReflectionDenoiseTemporal**
8. **CompositeLighting** (direct + indirect diffuse + indirect specular)
9. **Transparent/ForwardAdditive**
10. **PostProcess + UI**

Rules:

- `ReflectionTrace` runs before `GITrace` so rough specular can reuse depth/normal/motion data already prepared for GI.
- Denoise passes are distinct logical passes even if a backend fuses kernels internally.
- `CompositeLighting` is the only pass allowed to consume both GI and reflection history for final lighting resolve.
- Passes may be skipped by capability or quality tier, but ordering must remain deterministic for active passes.

## Resource Ownership And Lifetime

### Engine-owned descriptors

Engine-facing systems own only backend-neutral descriptors/handles:

- `GiSettings` (quality, bounce count clamp, probe/update budget)
- `ReflectionSettings` (trace mode, roughness threshold, temporal weights)
- `LightingHistorySettings` (history length, reset policy)
- stable `RenderTargetHandle` / `BufferHandle` identities

### Backend-owned payloads

Backends own API-native resources for:

- GI radiance reservoirs / probe volumes / screen-space history textures
- reflection history + hit data targets
- acceleration structures or tracing bindings (when supported)
- denoise intermediate resources

Higher layers never cache native backend ids/pointers.

### Frame lifetime buckets

- **Persistent**: history textures/buffers, probe state
- **Per-resize**: resolution-dependent radiance/denoise surfaces
- **Per-frame transient**: scratch trace targets and reductions

Destruction and recreation boundaries are explicit at:

- backend swap / renderer shutdown
- viewport resize generation bump
- hard history reset events (camera cut, scene streaming barrier)

## Capability + Fallback Policy

All GI/reflection behavior is capability-driven; no silent OpenGL/Vulkan branching in scene/editor code.

Capability groups:

- `supportsDynamicGI`
- `supportsDynamicReflections`
- `supportsGiDenoise`
- `supportsReflectionDenoise`
- `supportsGiHistory`
- `supportsReflectionHistory`

Fallback policy:

- **required**: feature must execute at selected tier or tier selection fails with diagnostic
- **fallback**: feature degrades to a documented lower-cost path
- **deferred**: feature unavailable; surfaced in debug HUD/settings UI and logs

Initial fallback ladder:

1. Full dynamic GI + reflections
2. Dynamic reflections + baked/probe-only GI
3. SSR-only reflections + baked/probe-only GI
4. Direct lighting only (explicitly marked reduced mode)

## Quality Tiers

Expose one shared quality enum used by runtime, editor preview, and tests:

- **Low**
  - no temporal GI history
  - SSR-only reflections
  - reduced resolution tracing buffers
- **Medium**
  - single-bounce dynamic GI with temporal accumulation
  - mixed SSR + probe reflections
  - simplified denoise budget
- **High**
  - full dynamic GI target path for selected backend
  - temporal GI + reflection denoise
  - full-resolution history path
- **Ultra** (optional per backend)
  - backend-dependent advanced path (e.g. hardware RT acceleration)
  - may be unavailable and must report capability failure cleanly

Tier contracts must map to deterministic pass enable/disable masks.

## Milestone Sequencing Contract (`#165+`)

- **M1 (`#165`)**: Data model and capability surface
  - add settings structs/enums and typed capability fields
  - no heavy rendering behavior changes yet
- **M2 (`#166`)**: Frame graph pass plumbing
  - introduce ordered GI/reflection pass slots and transient resource scheduling
- **M3 (`#167`)**: Baseline fallback implementation
  - implement deterministic fallback ladder and diagnostics
- **M4 (`#168`)**: Temporal history + denoise integration
  - add history reset policy and temporal validation hooks
- **M5 (`#169`)**: Quality tier wiring + editor/runtime UX
  - tier selection, debug HUD reporting, config persistence
- **M6 (`#170`)**: Validation expansion
  - architecture/docs assertions + targeted renderer/editor tests per tier/fallback

Exact issue numbers may shift, but sequencing dependencies should remain equivalent.

## Review Checklist

- Does the change keep GI/reflection resources backend-owned behind renderer seams?
- Are pass order and dependency rules explicit and deterministic?
- Are quality tiers mapped to concrete pass and fallback behavior?
- If a backend lacks support, is fallback/deferred behavior explicit and testable?
- Does the change move milestone `#165+` work forward without coupling scene/editor code to backend internals?
