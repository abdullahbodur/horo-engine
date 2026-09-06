# ADR-173: Render Queue, Submission and Fence Contract

- **Status**: Proposed
- **Date**: 2026-09-06
- **Supersedes**: None
- **Scope**: Backend-neutral queue roles, deterministic submission, GPU completion and CPU wait policy
- **Issue**: [RND-003.3](https://github.com/abdullahbodur/horo-engine/issues/298)
- **Jira**: [HORO-298](https://horo-engine.atlassian.net/browse/HORO-298)
- **Normative document**: [Rendering Architecture](../architecture/runtime/rendering-architecture.md)

## Context

The renderer already orders passes inside `RenderExecutionPlan` and render-graph
passes declare graphics, compute or transfer intent. It does not yet define how
those roles resolve to logical queues, how submissions across queues retain a
deterministic order, what constitutes GPU completion, or when a CPU wait is legal.
Leaving those choices to each backend would make a single-queue OpenGL path and
explicit multi-queue APIs observably different, invite normal-frame stalls, and
make resource retirement depend on native handles.

This decision defines the common value model and policy boundary. It does not
select native queue families, expose native semaphore/fence objects, synthesize
barriers, compile render graphs, or implement a concrete backend.

## Decision

### 1. Logical roles resolve to frontend queue identities

`Graphics`, `Compute`, and `Transfer` are workload roles, not promises of three
native queues. After device admission, the frontend publishes a finite effective
mapping from each supported role to a non-zero `RenderQueueId`. A single-queue
backend maps every supported role to the same identity. A multi-queue backend may
publish distinct identities only when capabilities and the selected product plan
admit them. Missing role support is a typed admission failure; it never silently
changes a pass category or selects another backend.

Queue identities are process-local frontend values. Native queue families,
indices, pointers, Objective-C objects and API-specific capability flags remain
private to the concrete backend.

### 2. Submission order is global and deterministic

Every admitted queue submission receives a non-zero, strictly increasing
`RenderSubmissionOrder` from one frontend/device lifetime. The compiled plan
stores submissions in that order. Backends consume that order exactly; they do
not regroup work by native queue, completion timing, pointer value, or driver
enumeration order.

The total order is a deterministic CPU admission order, not an instruction to
serialize independent GPU work. Cross-queue concurrency remains available where
explicit timeline waits permit it. Within one logical queue, native submission
order preserves the same sequence. Sequence exhaustion fails before wraparound;
values are never reused within a device lifetime.

### 3. Completion uses per-queue monotonic timeline points

A `RenderTimelinePoint` is a logical queue identity plus a non-zero, strictly
increasing value. Each submission signals exactly one point on its own queue.
GPU dependencies are lists of explicit points waited by the consuming
submission. A same-queue wait must precede the submission's signal. Cross-queue
waits name the producing queue directly; frame indices and CPU completion flags
are not substitutes for GPU completion.

Backends may realize this model with native timeline primitives, monotonically
numbered fence objects, or serial queue completion records. Native representation
must preserve the Horo ordering and lifetime semantics. Timeline exhaustion,
device loss and invalid/foreign points produce typed failures and trigger the
owning recovery policy; counters never wrap or reset inside a live device.

Completion points protect resources referenced by submitted work. Resource
retirement may occur only after every recorded point that pins the generation is
complete. CPU owner release, command encoding completion and frame presentation
do not prove GPU completion.

### 4. GPU waits are normal; CPU waits are exceptional and finite

Normal frame ordering uses GPU-side waits embedded in submissions. The render
owner never blocks the CPU to order ordinary passes, uploads, presentation or
resource reuse. Completion polling is non-blocking and uses a zero timeout.

Every blocking CPU observation declares one `RenderCpuWaitPurpose` and a positive
finite timeout:

- bounded readback explicitly requested by a caller;
- deterministic tests;
- teardown after new submissions are closed;
- documented device-loss recovery.

Timeout, device loss and unsupported native behavior return typed results.
Teardown remains bounded and may escalate to the backend's recovery/destruction
policy; it is not permission for an unbounded device-idle wait. GPU waits never
become CPU waits merely because a backend uses one physical queue.

### 5. Ownership and migration

The frontend owns logical queue identities, submission order and admitted
timeline points. The compiled plan owns the wait/signal records until synchronous
backend admission completes. The backend owns native execution and completion
tracking, and reports completion through Horo values. Shutdown first closes
admission, retires or cancels bounded pending work, performs only policy-authorized
finite waits, then invalidates the device's queue/timeline identity domain.

`RenderExecutionPlan::orderedPasses` remains the transitional single-submission
execution surface. Render-graph compilation will produce ordered
`RenderQueueSubmission` records when RND-009.3/RND-009.5 land, and the backend
execution boundary will migrate once rather than maintaining pass order and
submission order as competing authorities. Existing Null, OpenGL and Metal paths
initially map supported roles to their one effective graphics queue.

## Consequences

- Graph compilation and barrier synthesis have one native-free target model.
- Single-queue and multi-queue backends share observable ordering and completion
  semantics without pretending their native topology is identical.
- Resource retirement can name precise GPU completion evidence.
- Ordinary frames cannot acquire accidental CPU/GPU synchronization points.
- Frontends and backends must detect counter exhaustion and preserve the device
  identity domain across every completion query.
- Multi-queue support adds explicit waits and qualification work; it cannot be
  enabled by merely discovering another native queue.

## Rejected Alternatives

### Expose native queue and fence handles

Rejected because native types would cross `RenderApi`, couple graph compilation
and resource lifetime to one backend, and prevent Null from being a parity peer.

### Use frame indices as completion fences

Rejected because presentation, queue completion and resource retirement are
distinct events. A frame number cannot prove that every queue stopped using a
resource generation.

### Maintain only per-queue submission order

Rejected because cross-queue plans would lack a deterministic total admission
order for diagnostics, replay, tests and recovery. Explicit waits alone do not
provide stable CPU-side ordering of independent submissions.

### Require dedicated compute and transfer queues

Rejected because the baseline must support one physical queue and because queue
availability is a capability fact, not a product-wide guarantee. Role aliasing is
explicit and preserves the same contract.

### Permit unbounded CPU or device-idle waits

Rejected because a hung or lost device would hang frame delivery and shutdown.
Polling and every exceptional blocking purpose remain explicit and finite.
