# Destruction Registry Migration

Code that previously kept destructible pointers, entity slots, backend handles, or
ad-hoc capability booleans must move to the typed destruction registry boundary.

1. The application or runtime composition owner creates one bounded
   `DestructionRegistry`; feature code must not discover a global instance.
2. The DestructionRuntime owner publishes a `DestructionRegistryRecord` only after its
   state and capability publications commit. A record is copied metadata, not a lifetime
   lease.
3. Consumers capture `DestructionRegistrySnapshot` and use exact `DestructionHandle`
   lookup or a bounded `DestructionQuery`. `HasMore` requires an explicit follow-up
   product decision; it is not permission to widen work.
4. Consumers branch on typed `IdentityUnknown`, `StaleGeneration`, invalid-query, and
   capacity failures. They must not parse diagnostic strings or retry against another
   tier implicitly.
5. Capability projections are evidence only. Before mutation, submit the typed command
   through normal authority, capability-revision, generation, and state-revision
   admission.

Snapshots already issued remain immutable and readable during replacement and shutdown,
but any liveness-sensitive use must revalidate against the current owner publication.
