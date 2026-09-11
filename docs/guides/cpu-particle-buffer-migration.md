# CPU Particle Buffer Migration

## Purpose

Migrate CPU VFX simulation from per-particle containers or ad-hoc freelists to the
bounded `CpuParticleBuffer` contract. This guide covers preparation, owner-thread use,
stale-handle behavior and immutable CPU-to-GPU extraction; it does not define emitter
spawn policy or simulation kernels.

## Prerequisites

- A valid generation-safe `ParticleBufferId` owned by the scene's VFX world.
- An admitted non-zero particle capacity and finite custom-float channel count.
- A simulation owner thread or staging role that serializes buffer mutation.
- A separately budgeted immutable extraction destination for renderer upload.

## Workflow

### 1. Prepare storage

Call `CpuParticleBuffer::Create` before playback with the exact buffer incarnation,
fixed capacity, compiled custom-float channel count and product-lowered byte ceiling.
Creation may allocate; treat any typed identity, limit, overflow or allocation error as
an admission failure. Do not retry from a frame update by growing the capacity.

### 2. Spawn, update and kill

Issue non-zero `ParticleSimulationId` values in strictly increasing order. Retain the
returned `CpuParticleHandle` when another owner needs to refer to that logical particle;
never retain a dense index. `Spawn`, `Kill`, `ResolveDenseIndex`, `View`, `Clear`,
`Shutdown` and `Statistics` belong to the buffer's creation thread.

Use `View` only for the current owner operation. Its spans cover the packed live prefix
and become invalid for semantic use at the next spawn, kill, clear or shutdown. Kill
may move the last dense particle into the removed position, so resolve a retained
handle again instead of caching its previous index.

### 3. Extract for rendering

At the declared extraction safe point, copy or pack the live spans into an immutable,
backend-neutral frame slice. The renderer owns subsequent upload, native resources and
retirement. Do not publish mutable spans, CPU particle handles, raw pointers or SoA
indices as renderer or gameplay capabilities.

### 4. Retire or replace

Call `Shutdown` on the owner thread after scheduled simulation and extraction readers
have quiesced. Shutdown is idempotent, invalidates all live handles and rejects later
admission. A replacement buffer receives a new `ParticleBufferId`; old handles cannot
resolve against it.

## Troubleshooting

| Symptom | Check | Recovery |
| --- | --- | --- |
| `ParticleBufferCapacityExceeded` | Active and available statistics | Reject the newest birth or apply the separately authored overload policy. |
| `ParticleSimulationIdentityInvalid` | Identity is non-zero and greater than the last accepted identity | Repair the simulation-owned monotonic issuer; do not derive identity from a slot. |
| `ParticleHandleStale` | Particle was killed, cleared, or its slot was reused | Discard the handle and obtain a handle only from a new successful spawn. |
| `ParticleBufferThreadViolation` | Calling thread matches the preparation owner | Schedule the operation at the VFX simulation owner safe point. |
| `ParticleBufferInvalid` | Capacity, channel count and byte ceiling are finite and within hard limits | Lower the admitted layout or reject the effect before playback. |

## Limitations

The public storage contract is CPU-only and backend-neutral. It does not expose GPU
buffers, dispatch, synchronization, emitter policy or gameplay output. Custom channels
remain private simulation data until another typed contract explicitly projects them.
The buffer is owner-thread affine rather than internally synchronized.

## Validation Record

On 2026-09-11, the contract was validated on macOS with the VFX API unit suite and
public-header consumer. Coverage includes aligned streams, swap-remove preservation,
50,000 slot-reuse cycles, zero frame-hot allocations, bounded creation failures,
foreign and stale handles, owner-thread rejection, clear and idempotent shutdown.
Cross-platform CI remains the authoritative Windows and Linux validation.

## References

- [VFX and Particles Architecture](../architecture/runtime/vfx-and-particles-architecture.md)
- [ADR-011: Effect Ownership, Simulation Domain Policy and Renderer Boundary](../adr/011-vfx-effect-ownership-simulation-domain-and-renderer-boundary.md)
- [ADR-123: VFX CPU Stage Order, Determinism and Gameplay Coupling](../adr/123-vfx-cpu-stage-order-determinism-and-gameplay-coupling.md)
- [ADR-128: VFX Spawn Event Mapping, Pooling and Budget Enforcement](../adr/128-vfx-spawn-event-mapping-pooling-and-budget-enforcement.md)
