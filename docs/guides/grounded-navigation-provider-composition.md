# Grounded Navigation Provider Composition

## Purpose

This guide describes the initial host composition seam for the pinned Recast/Detour
grounded query provider. The public contract contains Horo values only; it is not a
durable asset format and must not be serialized as one.

## Prerequisites

- Select `HORO_BUILD_NAVIGATION_RECAST_DETOUR=ON` for a real desktop or headless
  navigation composition. `OFF` keeps the Horo factory surface but returns typed
  `OperationUnsupported` without fetching or linking Detour.
- Capture one exact `NavigationWorldId` and `NavigationGeneration` before provider
  creation.
- Supply finite canonical-metre vertices and counter-clockwise convex polygons with
  three to six vertices. Up to 64 distinct stable Horo area identities are mapped
  deterministically to private Detour area slots.

## Workflow

1. Fill `RecastDetourProviderCreateInfo` with borrowed topology and project-lowered
   node, result, query-lease, search-distance, and owned-byte ceilings.
2. Call `CreateRecastDetourNavigationQueryBackend`. The call validates and copies all
   input, creates native topology and fixed query scratch transactionally, then returns
   the neutral `INavigationQueryBackend` interface.
3. Create a bounded `NavigationWorldLifecycle`, then `Stage` the owned interface with the
   exact Scene incarnation, Scene generation, world, and topology identities. Publish it
   only through `CommitAtSafePoint` during `CommitDeferredLifecycleChanges`. A stale Scene
   generation or saturated retirement bound leaves the previously active world untouched.
4. Acquire a `NavigationWorldReadLease` on the lifecycle owner thread before dispatching
   worker work. The lease pins the provider and exposes cooperative cancellation; workers
   must reject result publication when `IsRevoked()` becomes true.
5. Use `Pause`/`Resume` only for unchanged-world admission. Replacement, `Unload`, and
   `BeginShutdown` revoke admission and cancel outstanding leases. Call `CollectRetired`
   at owner safe points until shutdown reaches `Closed`; it never blocks for a worker.

## Troubleshooting

- `CapabilityDescriptorInvalid`: a composition limit, identity, extent, or setting is
  zero, non-finite, or outside the qualified ceiling.
- `ProviderFailed`: polygon data is corrupt, non-finite, non-manifold, unrepresentable in
  the selected quantization, or rejected by the pinned native builder.
- `CapacityExceeded`: preparation or query scratch cannot fit the declared budget.
- `AdmissionRejected`: all fixed query leases are in use; the caller must retain normal
  scheduling/back-pressure policy rather than block.
- `InvalidWorld` / `StaleSnapshot`: the request belongs to another scene incarnation or
  topology generation. There is no silent fallback.

## Limitations

This delivery provides immutable grounded path queries plus transactional per-Scene provider
publication and retirement. Durable tiled artifact serialization, runtime tile streaming,
Recast bake/cook, dynamic carving, and crowd behavior remain owned by their focused NAV
tickets.

## Validation Record

The provider contract suite covers successful paths, malformed/corrupt data, allocation
budgets, cancellation, foreign/stale requests, bounded concurrent leases, and repeated
construction/destruction. Linux, macOS, and Windows CI remain the platform authority.

## References

- [Navigation and AI Architecture](../architecture/runtime/navigation-and-ai-architecture.md)
- [ADR-016 Navigation Target Ownership](../adr/016-navigation-target-ownership-and-dependency-boundary.md)
- [ADR-104 Default Navigation Provider](../adr/104-default-navigation-provider-and-recast-detour-adoption.md)
