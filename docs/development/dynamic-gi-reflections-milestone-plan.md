# Dynamic GI And Reflections Milestone Plan

This plan operationalizes `docs/architecture/dynamic-gi-and-reflections-architecture.md` into implementation slices for issue chain `#165+`.

## Milestones

## M1: Capability + Settings Surface (`#165`)

Deliverables:

- backend-neutral settings structs for GI/reflections quality and update budgets
- renderer capability fields for GI/reflection support and denoise/history support
- configuration serialization defaults for tier selection

Exit criteria:

- runtime/editor can query one typed capability surface
- no backend-specific branching leaks into scene/editor systems

## M2: Pass Graph Integration (`#166`)

Deliverables:

- explicit ordered pass slots:
  - `ReflectionTrace`
  - `GITrace`
  - `GIDenoiseTemporal`
  - `ReflectionDenoiseTemporal`
  - `CompositeLighting`
- pass dependency validation in renderer orchestration

Exit criteria:

- pass ordering remains deterministic under enabled tiers
- pass skips are capability/tier-driven only

## M3: Fallback Ladder (`#167`)

Deliverables:

- deterministic fallback chain from full GI/reflections to direct-lighting-only mode
- diagnostics surfacing active fallback reason
- minimal debug HUD/status exposure for active mode

Exit criteria:

- unsupported capabilities do not silently no-op
- users can identify why a tier was reduced

## M4: Temporal History + Denoise (`#168`)

Deliverables:

- per-feature history resources with reset policy (camera cut/resize/scene barrier)
- temporal denoise integration points and budget controls
- quality-tier-specific history behavior

Exit criteria:

- history invalidation events are deterministic
- temporal path remains stable across resize and camera cuts

## M5: Tier UX + Runtime Controls (`#169`)

Deliverables:

- quality tier control in runtime config/editor
- explicit unavailable-tier messaging
- persistence for tier selection

Exit criteria:

- tier selection and observed pass behavior match documented contract
- unsupported tiers fail or degrade with actionable messaging

## M6: Validation And Hardening (`#170`)

Deliverables:

- architecture/docs test coverage updates for GI/reflections docs discoverability
- renderer/editor tests for fallback and tier behavior
- CI/local command guidance for GI/reflection validation paths

Exit criteria:

- required checks cover capability, fallback, and pass order contracts
- milestone chain can continue to backend-specific optimization tasks safely

## Dependency Order

`M1 -> M2 -> M3 -> M4 -> M5 -> M6`

Rules:

- no milestone should bypass typed capability checks introduced in M1
- M3 fallback behavior must be in place before high-cost tier defaults are enabled
- M6 test coverage must assert documented fallback policy, not implementation-specific internals
