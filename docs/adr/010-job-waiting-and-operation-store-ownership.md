# ADR-010: Job Waiting and Operation Store Ownership

- **Status**: Proposed
- **Date**: 2026-08-28
- **Supersedes**: None
- **Scope**: Job execution, waiting, low-level records, user-facing operation aggregation, observation and submission-captured context
- **Issue**: [#1828](https://github.com/abdullahbodur/horo-engine/issues/1828) ([JOB-001.1])
- **Jira**: [HORO-1784](https://horo-engine.atlassian.net/browse/HORO-1784)
- **Normative document**: [Concurrency And Job System](../architecture/foundation/concurrency-and-jobs.md)

## Context

`docs/architecture/foundation/concurrency-and-jobs.md` separates low-level scheduled jobs from user-facing operations. `JobSystem` executes bounded work and owns durable job records; an application coordinator projects selected jobs into an authoritative bounded `OperationStore`; GUI, CLI and MCP observe snapshots rather than scheduler internals. Submission must freeze diagnostic and configuration context, while synchronous waiting is restricted by thread ownership.

The current implementation has a sound structured-concurrency baseline but does not yet implement the complete ownership and waiting contract:

- `HoroEditor` composes one process-owned `JobSystem` with a fixed worker count and bounded FIFO queue. `JobSystem::State` owns workers, queued callbacks and an `unordered_map<JobId, shared_ptr<JobRecord>>` that acts as the current embedded `JobStore`.
- Successful submission creates exactly one record; queue rejection creates none. `JobHandle` is move-only, while the scheduler retains every accepted record independently of handle lifetime.
- Terminal records are never removed from the embedded jobs map, so queue capacity is bounded but record retention is not. `JobSnapshot` exposes only ID, state and optional error; phase, progress, timing, task-group identity and revision notification are absent.
- `TaskGroup` is non-copyable/non-movable, rejects children after closure, propagates parent cancellation, joins accepted children in deterministic spawn order and makes repeated `Join()` calls stable. Destruction cancels and synchronously joins remaining children.
- `JobHandle::Wait()` and `TaskGroup::Join()` wait without timeout, thread-role detection, helping, continuation pumping, dependency checks or a published wait reason. Main/editor, render-capable and transport-owner callers can therefore violate the normative waiting rules.
- Worker execution catches exceptions at the scheduler boundary and restores `Telemetry::OperationContext` through RAII. `JobRecord` captures active operation and diagnostic log context on the submitting thread, and observability regression coverage proves parent/correlation forwarding.
- `JobDescriptor` carries only parent cancellation. It does not capture an immutable `ConfigurationSnapshotRef`, configuration revision or task-group correlation at submission.
- `OperationStore` is a bounded, thread-safe Foundation type with independent active/recent limits, monotonic same-phase progress, terminal retention, race-safe cancellation and owned `SnapshotIfChanged()` copies. It is composed by the host and exposed to the editor through narrow query/control interfaces.
- There is no explicit application `OperationCoordinator`. Application, runtime and editor workflows currently call `OperationStore::Begin()`/`Update()` directly, so aggregation policy and mutation authority are distributed.
- The Operations pane polls `IOperationQuery::SnapshotIfChanged()` and never becomes state authority. The documented coalescible `OperationStoreRevisionChangedEvent` is not implemented.
- `LoggingOperationHistorySink` consumes terminal operation copies outside the store lock. OBS-001D telemetry spans and OBS-001E persistent history are derived observability projections; neither is the live job or operation authority.

[JOB-001.1] requires one decision that fixes these ownership, waiting, observation and context boundaries before the later scheduler tickets extend the implementation.

## Decision

**Foundation `JobSystem` owns scheduling and the authoritative low-level `JobStore`. The application composition owns one authoritative `OperationStore` and gives exclusive mutation authority to an application `OperationCoordinator` (or feature-local coordinator conforming to the same contract). Observability and presentation receive immutable derived data only. Every blocking wait is checked against an explicit thread role and wait policy. Every accepted submission freezes its diagnostic, operation, cancellation and configuration context before enqueue.**

### Ratify-or-revise outcomes

| Area | Current state | Outcome |
|---|---|---|
| Process scheduler ownership | `HoroEditor` composes one `JobSystem`; workers are owned and joined | **Ratified.** Every host composes at most one process job system and destroys it in dependency order. |
| Queue baseline | Fixed-capacity FIFO queue rejects overflow with a typed error | **Ratified as the baseline.** Priority classes and overload policies remain [JOB-001.4]; resource admission remains [JOB-001.5]. |
| Embedded job records | `JobSystem::State::jobs` is the effective store but retains all accepted records | **Revised.** It becomes the explicit scheduler-owned `JobStore` contract with bounded terminal retention under [JOB-001.2]. |
| `JobHandle` | Move-only shared reference; dropping it neither cancels nor deletes scheduler state | **Ratified.** Handles are observation/control leases, not record authority. Progress, timing and immutable terminal results are completed by [JOB-001.2]. |
| `TaskGroup` structure | Owned scope, deterministic join order, stable repeated join, cancellation on destruction | **Ratified with waiting enforcement.** Child lifecycle remains structured; owner-thread destruction with unfinished children is an invariant violation, not permission for an illegal wait. |
| Cancellation and terminal state | Cooperative token and single terminal transition exist; cancellation/failure classification has partial coverage | **Ratified as a baseline, revised by [JOB-001.3].** Cancellation never becomes a generic failure compatibility path. |
| Exception boundary | Worker adapter catches and converts callback exceptions to typed job failure | **Ratified.** Exceptions never cross the job/thread boundary. |
| Diagnostic/operation context | Submission captures `Telemetry::OperationContext`; workers bind and restore it | **Ratified.** OBS-001D owns span/context correlation, not job or operation state. |
| Configuration context | No snapshot or revision is captured | **Revised.** Configuration-dependent jobs capture an immutable snapshot explicitly at submission under [JOB-001.2]. |
| Waiting | `Wait()` and `Join()` are unbounded and thread-role agnostic | **Revised.** The enforcement contract below is mandatory for handle/task-group work in [JOB-001.2] and shutdown work in [JOB-001.5]. |
| `OperationStore` | Bounded authoritative records and revisioned snapshots exist | **Ratified as the user-facing state authority.** Foundation owns the storage type; application composition owns its instance and lifetime. |
| Operation aggregation | Multiple feature/runtime/editor callers mutate the store directly | **Revised.** An application coordinator is the sole aggregation/mutation seam; vertical slices migrate without creating peer stores. |
| Presentation observation | `IOperationQuery`/`IOperationControl` are narrow; editor polls by revision | **Ratified as the query/control boundary, revised to add lightweight coalescible revision notification.** |
| Terminal history | Store emits an immutable terminal copy outside its lock; telemetry history persists bounded derived records | **Ratified as consumer-only behavior.** OBS-001E may persist/recover history but never updates or replaces live store state. |

### Authoritative ownership

| Concern | Owner | Allowed consumers |
|---|---|---|
| Worker pool, queues, admission and execution | Process `JobSystem` | Application coordinators and internal schedulers through typed submission APIs |
| Low-level accepted job records (`JobStore`) | `JobSystem` | `JobHandle`, task groups, application coordinator and developer diagnostics through bounded query/control APIs |
| Job-to-operation selection and aggregation policy | Application `OperationCoordinator` | Feature application services submit meaningful operation descriptors and child-job relationships |
| User-facing active/recent operation records (`OperationStore`) | Application composition; mutated only by its coordinator | GUI, CLI, MCP, observability adapters and tests through snapshots |
| Cancellation intent | Application coordinator maps operation cancellation to owned task groups/jobs | GUI/CLI/MCP receive only narrow `IOperationControl` |
| Span/log correlation (OBS-001D) | Observability runtime | Jobs and operations attach IDs/context; telemetry does not decide lifecycle state |
| Bounded persistent history (OBS-001E) | Observability consumer | Receives terminal immutable projections; recovered history is not reinserted into the live store |
| Revision notification | Application coordinator after a successful store mutation | Engine data bus coalesces; consumers re-query the store |

`JobStore` and `OperationStore` are deliberately different authorities. Internal jobs are not automatically user-visible. An operation may aggregate zero, one or many jobs, and one job may be correlated to an operation without transferring record ownership.

`Telemetry::OperationId` identifies a span/correlation lineage; `Horo::OperationId` identifies a user-facing store record. Their current integer representations do not make them interchangeable. The coordinator records an explicit correlation edge and never relies on numeric equality or shared allocation order.

The current direct `OperationStore` writers are migration points. Application services may perform coordinator duties locally until a shared coordinator API lands, but runtime backends and presentation code must not invent independent aggregation rules or complete operations on behalf of the application owner.

### Waiting enforcement

Every host-owned thread installs one typed role: `MainEditor`, `RenderOwner`, `TransportOwner`, `Worker`, `IoService` or `ExternalUnknown`. Scheduler workers install `Worker`; host adapters install their owner role before accepting work. Unknown threads use the safest policy and cannot perform an unbounded wait.

Every blocking API accepts a `JoinOptions`/`WaitPolicy` value and checks the
caller role plus the awaited job's declared continuation/resource requirements
before blocking. These requirements are target contract, not implemented
metadata: [JOB-001.2] adds immutable typed continuation-affinity and resource
requirement fields to `JobDescriptor`, captures them at successful submission and
exposes them to wait validation. An absent or unknown requirement is treated
conservatively and never grants an owner-thread wait. [JOB-001.5] defines the
admission and shutdown semantics for each declared resource class.

| Caller role | Rule |
|---|---|
| Worker | May join only through task-group primitives with bounded timeout plus helping or deadlock/capacity detection. A descriptor that declares a synchronous nested child join requires one additional worker; a single-worker pool rejects submission of that parent job before it enters the queue. An undeclared attempt is rejected by `Join()` with a capacity-deadlock `WaitError` before blocking. |
| Main/editor | Ordinary job waits are forbidden. `MainThreadPumpAllowed` may pump only bounded typed continuations with explicit time/count budgets and must reject a dependency cycle. |
| Render owner | Cannot wait for work that may require render/GPU-owner continuation or completion. Bounded teardown/readback waits follow [Rendering Architecture: Threading And Synchronization](../architecture/runtime/rendering-architecture.md#threading-and-synchronization) and the ordered [Runtime Lifecycle shutdown path](../architecture/runtime/runtime-lifecycle.md#shutdown). |
| Transport owner | Cannot synchronously wait for application jobs; it retains request state and completes asynchronously. |
| I/O service | Cannot wait for work that requires the same I/O service slot; other waits remain bounded and cancellation-aware. |
| External/unknown | Blocking wait is rejected unless an explicitly registered test/shutdown scope supplies a bounded policy. [JOB-001.2] introduces the test scope; [JOB-001.5] introduces the shutdown scope. |

The test/shutdown exception is a host-installed, thread-local
`ThreadWaitScope` (or project-standard scope guard) containing purpose, thread
role, deadline and any continuation-pump budget. Only the composition root,
shutdown coordinator or test harness may install it. Nesting cannot widen the
outer deadline or permissions, and an ambient process-global bypass flag is not
allowed.

Every public wait primitive, including `JobHandle::Wait()` and
`TaskGroup::Join()`, returns a `Result<WaitOutcome, WaitError>` (or the
project-standard equivalent) and is `[[nodiscard]]`. `WaitOutcome` is produced
only after the awaited job or group reaches its defined terminal condition.
`WaitError` distinguishes at least forbidden thread role, dependency cycle,
capacity deadlock risk and timeout. Rejecting a wait must therefore never return
the same value as successful completion or allow a caller to observe an
unfinished job as complete.

A forbidden or deadlock-prone wait returns the stable typed error in release
builds and may also assert in developer builds. Permitted waits publish a bounded
wait reason before blocking, clear it after completion and emit thresholded
timing/telemetry. The wait reason is projected through the coordinator into
`OperationStore`; it is not stored in a log-only side channel.

`TaskGroup` destruction remains a last-resort lifetime safety net: it closes admission, requests cancellation and accounts for every child. Normal owner-thread code must arrange asynchronous completion or explicitly join from an allowed executor before destruction. Destroying a group with unfinished children on a forbidden role is an invariant violation. Shutdown uses the separately ordered and bounded [JOB-001.5] drain path rather than pretending to be an ordinary permitted wait.

### Submission-captured context

Context is frozen after admission succeeds and before the callback becomes visible to a worker:

- owned diagnostic/log context and OBS-001D operation/span lineage;
- explicit application operation and task-group correlation IDs when present;
- cancellation ancestry;
- immutable `ConfigurationSnapshotRef` and revision for configuration-dependent work;
- bounded request, project and asset identities already admitted by the operation boundary.

The submitting application supplies the configuration snapshot; `JobSystem`
does not discover `ConfigurationService` or read ambient global state. Under
[JOB-001.2], callbacks receive a read-only `JobExecutionContext` containing the
captured `ConfigurationSnapshotRef`; the job-system target has no dependency on
the live configuration service. [JOB-001.2] adds architecture dependency
coverage for that boundary, while developer builds reject ambient
current-snapshot reads from a `Worker` role. Its regression coverage submits
against one revision, publishes a new revision before execution and proves that
the callback still observes the captured revision. A descriptor explicitly marks
configuration-independent work instead of silently reading the latest snapshot
on a worker. `TaskGroup` freezes its parent submission context at construction;
children inherit it and may add
narrower diagnostic fields, but they cannot silently switch configuration
revision.

Workers bind the owned context for exactly one callback and restore the previous worker context through RAII on success, failure, cancellation and exception paths. Completion observers receive the same captured identities. Reused workers never inherit fields from a previous job.

### Operation observation seam

After every accepted `OperationStore` mutation, the application coordinator publishes a lightweight `OperationStoreRevisionChangedEvent` containing revision, optional operation ID and coarse state. The event uses merge/coalescing backpressure and may be dropped; it carries no complete snapshot, logs, output or persistence payload.

GUI, CLI and MCP react to the notification by calling `IOperationQuery::SnapshotIfChanged(lastRevision)`. They remain correct when notification delivery is delayed or dropped because the bounded store is authoritative. Polling by revision remains a valid fallback for a visible editor panel or non-bus test host. Presentation may request cancellation through `IOperationControl`, but it never writes progress or terminal state.

OBS-001D attaches spans and correlation to the same work. OBS-001E consumes terminal immutable projections asynchronously and persists a bounded restart-readable history. A slow, disabled or failed observability sink cannot block job execution, hold a store lock or change a live operation result.

### Migration boundaries

- [JOB-001.2] owns explicit `JobStore`, durable handle records, progress/timing/results, job/operation/task-group correlation, typed continuation/resource requirement metadata, submission context, configuration capture, the test `ThreadWaitScope` and ordinary wait APIs.
- [JOB-001.3] owns cancellation-versus-failure state-machine completion and race coverage.
- [JOB-001.4] owns priority queues, backpressure and overload observability under the waiting rules decided here.
- [JOB-001.5] owns resource admission and ordered bounded shutdown/drain behavior.
- Application vertical slices migrate direct `OperationStore` mutation behind the coordinator seam while preserving one store instance.

No follow-up may make `OperationStore` a scheduler detail, expose every internal job to presentation, use observability persistence as live authority, perform an owner-thread unbounded wait or fetch configuration after worker execution begins.

After acceptance, implementation tickets do not amend this ADR. A later change
to one boundary is recorded in a narrow follow-on ADR. If it conflicts with an
accepted decision here, the replacement explicitly carries forward the unchanged
decisions and supersedes ADR-010 as a whole, following the repository ADR
convention.

## Consequences

- The bounded queue, move-only handle, structured task group, cancellation ancestry and telemetry-context forwarding remain valid foundations.
- Low-level job retention becomes bounded and queryable without coupling internal scheduler records to user-facing UI.
- One application-owned operation projection serves GUI, CLI and MCP; revision events remain hints rather than state transport.
- Waiting violations become deterministic typed failures instead of platform-dependent stalls.
- Long-running work uses the configuration revision and diagnostic context chosen at submission, even after later host changes.
- Observability may be disabled, slow or recovering history without changing job or operation correctness.

## Rejected Alternatives

- **Let `JobSystem` own and populate `OperationStore`.** Rejected: scheduler internals would define the user-facing operation model and every micro-job would leak into application contracts.
- **Allow each feature or presentation adapter to maintain its own operation list.** Rejected: progress, cancellation and terminal results would have competing authorities across GUI, CLI and MCP.
- **Use polling or revision events as the authoritative result.** Rejected: both are observation mechanisms; immutable store records own the state.
- **Treat telemetry span IDs as operation-store IDs.** Rejected: observational lineage and user-facing lifecycle have different retention and cardinality.
- **Permit owner-thread waits when they seem short.** Rejected: duration does not prove absence of an affinity/dependency cycle and creates nondeterministic frame or transport stalls.
- **Read the latest configuration inside worker callbacks.** Rejected: queued work would change meaning across revisions and tests could not reproduce the submission context.
- **Make persistent history a second live `OperationStore`.** Rejected: recovery/retention failures would become application-state failures and violate single authority.
