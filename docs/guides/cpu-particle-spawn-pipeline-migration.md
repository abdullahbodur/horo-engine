# CPU Particle Spawn Pipeline Migration

## Purpose

Replace mutable emitter PRNGs, per-birth allocations and ad-hoc expiry scans with the
bounded `CpuParticleSpawnPipeline`. The pipeline composes deterministic continuous and
burst spawning, descriptor-range initialization and slot-safe kill bookkeeping over
`CpuParticleBuffer`; forces, collision and render extraction remain separate stages.

## Prerequisites

- A `ParticleSystemDescriptor` admitted through the normal validation boundary.
- Stable `EffectSystemId` activation and `ParticleBufferId` identities.
- A cooked/replay seed and product-lowered burst, delta and storage limits.
- One simulation owner thread that serializes preparation, stepping and shutdown.

## Workflow

### 1. Prepare once

Call `CpuParticleSpawnPipeline::Create` during activation, not from a frame update.
Creation copies immutable descriptor data and allocates the SoA plus dense handle
table. Treat a typed identity, limit or allocation failure as activation rejection;
never retry by growing the pool in the active frame.

### 2. Submit a frozen step

Pass the selected-clock finite delta and the burst count accepted at the tick cutoff
to `Advance`. The pipeline combines them with its fractional continuous-rate carry.
Births beyond available capacity are reported in `dropped`; existing particles are
not evicted. A cancelled, malformed or over-limit request fails before carry, age,
identity or particle state changes.

The pipeline initializes canonical emitter-local geometry, velocity, size, opacity,
rotation and lifetime using version-one semantic counter-hash channels. Identical
descriptor, activation, seed and ordered steps reproduce the same sequence without a
shared mutable PRNG. Apply host transforms and later Forces/Integrate/Collide work at
their declared ADR-123 stage boundaries.

### 3. Kill and inspect

Finite particles expire after age reaches their sampled maximum. To request an
explicit death, obtain the current stable handle with `HandleAtDenseIndex` and call
`SignalKill`; the next `Advance` consumes the private flag. Never retain a dense index
across a spawn or kill. Use `View` only within the owner operation and do not expose
its spans to gameplay or rendering.

### 4. Shut down

After dependent readers quiesce, call `Shutdown` on the owner thread. Repeated calls
succeed, live handles become stale and later steps are rejected. A replacement scene
or activation prepares new generation-safe identities.

## Troubleshooting

| Symptom | Check | Recovery |
| --- | --- | --- |
| `ParticleSpawnStepInvalid` | Delta is finite/in range and burst is within the prepared ceiling | Correct the frozen step; no state was changed. |
| `ParticleSpawnStepCancelled` | Owner cancellation fence was set before the call | Preserve the last committed state and schedule only a new eligible tick. |
| Non-zero `dropped` | Requested births exceeded currently available slots | Apply the authored cosmetic overload policy; do not grow or overwrite. |
| `ParticleHandleStale` | The referenced particle expired or its slot was reused | Discard the old handle and resolve only a current live particle. |
| `ParticleBufferThreadViolation` | Call originated away from the simulation owner | Dispatch the complete stage sequence to the owner safe point. |

## Limitations

The canonical unit shapes do not contain host transforms or authored vector/color
curves; those require versioned descriptor/runtime contracts. This pipeline does not
implement forces, collision, extraction, GPU work, gameplay output or sub-emitter
delivery. Floating shape math follows ADR-123's qualified numeric policy rather than
claiming cross-fingerprint bit identity.

## Validation Record

The contract is covered by continuous-carry, burst/capacity, four-shape, identical-seed,
expiry, explicit-kill, stale-handle, cancellation, shutdown, owner-thread and 10,000-step
allocation-free churn regressions. The public-header consumer verifies target ownership.

## References

- [VFX and Particles Architecture](../architecture/runtime/vfx-and-particles-architecture.md)
- [CPU Particle Buffer Migration](./cpu-particle-buffer-migration.md)
- [ADR-123: VFX CPU Stage Order, Determinism and Gameplay Coupling](../adr/123-vfx-cpu-stage-order-determinism-and-gameplay-coupling.md)
- [ADR-128: VFX Spawn Event Mapping, Pooling and Budget Enforcement](../adr/128-vfx-spawn-event-mapping-pooling-and-budget-enforcement.md)
