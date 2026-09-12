# Navigation Bake Input Migration

NAV-003.2 introduces the canonical composition boundary between immutable navigation
source geometry and later tiled voxelization. It does not add a scheduler, cache,
publisher or provider dependency.

## Producer migration

Canonical right-handed Y-up metre sources require no source change. Other producers
set `NavigationSourceContributionInput::coordinates` to their exact axis convention
and finite positive metres-per-unit value. Capture converts coordinates before the
existing `localToCanonicalMeters` transform and reverses left-handed triangle winding.
Do not pre-swizzle vertices while also declaring a noncanonical convention.

Invalid geometry errors now include stable producer and contribution values in the
message while retaining the existing typed error code. Callers continue branching on
the code, not on message text.

## Tile-builder migration

Create `NavigationBakeInputSnapshot` from one validated `NavigationAreaRegistry`, the
complete available grounded-profile table, explicit surface/profile/source bindings,
modifier volumes and an rvalue `NavigationSourceGeometrySnapshot`. The geometry object
is moved only after validation succeeds. Consume `Partitions()`, `Triangles()` and
`Modifiers()`; do not re-resolve authoring registries or mutable producer data in a
worker.

The capture sorts profiles, referenced areas, surface bindings and modifiers by stable
typed identity. It resolves query filters before emitting triangles, transforms
modifier AABBs to canonical space, retains producer provenance and exposes a canonical
SHA-256 fingerprint. All configured count, work and owned-byte limits fail closed
without partial output.

Before publishing a cook result, call `ValidatePublication` with the retained request
generation, the complete current revision set, the complete current source observation
set and the owning operation state. Only `Ready` with exact revision/digest equality may
cross the adoption barrier. Cancelled, failed, superseded and shutdown attempts preserve
the prior published generation.
