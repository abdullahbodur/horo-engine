# Cross-Cell Dependency Policy Migration

## Purpose

This guide helps world-cook producers migrate authored cross-cell references to
the explicit hard co-load and soft deferred-reference policy owned by
`WorldDependencyPlan`.

## Prerequisites

- A validated `WorldSpatialAssignment` containing the source objects.
- Stable object addresses and exact admitted revisions for every resolved endpoint.
- Non-zero `WorldDependencyPlanLimits` selected by the host.

## Workflow

### 1. Classify each relationship

Represent a relationship exactly once. Use `WorldDependencyKind::Hard` when both
objects must be resident together. Use `WorldDependencyKind::Soft` when the target
may be absent and resolution can be deferred.

Hard-only cycles are valid and collapse into one canonical co-load bundle.
Soft-only cycles are valid because they remain deferred. Do not add a soft edge
between objects already connected through the hard graph; that mixed policy is
ambiguous.

### 2. Build the inert plan

Pass the immutable assignment snapshot, dependency candidates, and mandatory
limits to `WorldDependencyPlan::Create`. Plan creation does not resolve soft
references, change residency, or mutate ambient state.

### 3. Verify the result

Publish the returned bundles and soft-reference table only when `Create` succeeds.
The error `world_streaming.dependency_plan.ambiguous` means a resolved soft edge
contradicts the transitive hard co-load policy. Remove that soft edge or break the
hard path so exactly one policy owns the relationship.

## Troubleshooting

| Symptom | Check | Recovery |
| --- | --- | --- |
| Ambiguous dependency-plan error | Whether the soft endpoints share a transitive hard path | Remove the soft edge or reclassify the authored hard path |
| Missing hard-target error | Whether the exact hard target is present in the assignment | Admit the target before planning or author the relationship as soft |
| Stale-revision error | Whether each resolved endpoint matches the assignment revision | Rebuild candidates from the same immutable assignment snapshot |

## Limitations

The plan is an in-memory cook projection, not a persistent wire format, residency
owner, or runtime resolver. Unresolved soft targets remain valid for a later owning
resolution contract.

## Validation Record

Validated on 2026-09-11 with the world-streaming unit target on macOS. Coverage
includes direct and transitive mixed-policy ambiguity plus valid soft-only cycles.

## References

- [World Streaming Architecture](../architecture/runtime/world-streaming-architecture.md)
- [World Spatial Object Descriptor Migration](./world-spatial-object-descriptor-migration.md)
- [Repository Working Contract](../../AGENTS.md)
