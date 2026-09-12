# World Streaming Velocity Prefetch Migration

`[WST-002.8]` adds a typed, allocation-free projection contract for bounded
camera and gameplay prefetch. Existing hosts that extrapolate floating-point
positions or mutate source registries directly should migrate to
`StreamingPrefetchPolicy` and `EvaluateStreamingVelocityPrefetch`.

## Host integration

1. Publish one validated policy with a stable `StreamingPrefetchPolicyId` and an
   exact `StreamingPrefetchPolicyRevision`.
2. Capture canonical position, signed millimeter-per-second velocity and
   unscaled sample time together with the exact `StreamingSourceDescriptor`.
3. Supply immutable policy, partition epoch, lifecycle and source-admission
   evidence to the evaluator.
4. For `Path`, pass the returned `StreamingSourcePathVolume` through the ordinary
   source-range, priority and budget pipeline. For `Inactive`, publish no
   predicted path.

The evaluator does not register, replace or remove a source. The authority that
provided `StreamingSourceAdmissionContext` owns that mutation and must revalidate
the returned admission kind at its safe point. Cancellation, shutdown, policy or
partition replacement, expired samples, source capacity and coordinate overflow
remain typed failures; there is no stationary, zero-velocity or alternative-shape
fallback.

Predicted camera demand is local presentation optimization. It cannot grant
gameplay authority, create network-owned actors, pin cells or bypass the shared
streaming ledger.
