# ADR-156: PCG Scale Budgets, Trust and Release Scope

- **Status**: Proposed
- **Date**: 2026-09-02
- **Supersedes**: None
- **Scope**: PCG graph, point, attribute, dependency, memory, work, time and queue limits; overload behavior; untrusted inputs; diagnostics privacy; qualification workloads; M5/1.0 core and post-1.0 hierarchical/custom-provider boundary
- **Issue**: [PCG-7.1](https://github.com/abdullahbodur/horo-engine/issues/2095)
- **Jira**: [HORO-2049](https://horo-engine.atlassian.net/browse/HORO-2049)
- **Related**: [ADR-008](008-error-model-exception-boundary-and-registry.md), [ADR-010](010-job-waiting-and-operation-store-ownership.md), [ADR-018](018-command-registration-permissions-threading-and-packaged-build-policy.md), [ADR-028](028-renderer-capability-limits-and-product-profiles.md), [ADR-041](041-backend-neutral-renderer-diagnostics-model.md), [ADR-051](051-renderer-benchmark-and-regression-gates.md), [ADR-054](054-extension-and-package-authority-boundary.md), [ADR-143](143-terrain-foliage-scale-budgets-observability-and-feature-boundary.md), [ADR-150](150-pcg-graph-source-cooked-plan-cache-and-runtime-ownership.md), [ADR-151](151-pcg-ownership-authority-tier-and-lifecycle.md), [ADR-152](152-pcg-spatial-input-snapshot-and-node-library-ownership.md), [ADR-153](153-pcg-pure-evaluation-commit-and-generated-output-ownership.md), [ADR-154](154-pcg-cross-system-authority-readiness-and-commit-boundary.md), [ADR-155](155-pcg-graph-document-preview-bake-and-undo-ownership.md)
- **Normative documents**: [Procedural Generation Architecture](../architecture/runtime/procedural-generation-architecture.md), [Metrics and Profiling](../architecture/observability/observability-performance.md), [Testing Architecture](../architecture/delivery/testing-architecture.md), [Asset Pipeline](../architecture/runtime/asset-pipeline.md), [World Streaming Architecture](../architecture/runtime/world-streaming-architecture.md), [Extension System](../architecture/extensions/plugin-system.md)

## Context

The PCG decisions establish ownership, immutable inputs, pure evaluation, generated
output commit and editor workflows. The architecture still uses backend-oriented labels
and indicative graph/point limits without fixing complete admission envelopes or
overload behavior. “Supports large graphs” and “runtime PCG” are therefore not
measurable claims, while unknown work could be admitted as zero and exceed memory,
frame or queue budgets after mutation has started.

PCG source, cooked plans, dependencies, packages, spatial attributes and remote/save/
network reconstruction references are untrusted structured inputs. A malformed graph
can request explosive point multiplication, deep dependency expansion, oversized
attributes or high-cardinality diagnostics even without arbitrary native code. Trusted
node modules also need finite declared costs; code trust does not grant resource or
privacy exemption.

The release boundary is equally important. The core M5/1.0 product must qualify a
bounded built-in single-graph CPU path across editor, packaged, headless and server
compositions. Hierarchical graph composition and versioned custom node/provider
extensions are valuable but add recursion, ABI/sandbox, trust and compatibility work.
They must not become hidden dependencies of the M5 parent acceptance criteria.

This ADR defines operational tier ceilings, observability/privacy, protected
qualification workloads and the explicit 1.0 versus post-1.0 boundary. PCG-7.2 through
PCG-7.6 implement and qualify the 1.0 evidence. PCG-7.7 and PCG-7.8 are post-1.0 and do
not gate M5.

## Decision

### 1. Three provider-neutral operational tiers are supported

Version 1 defines `PCGBaseline`, `PCGStandard` and `PCGHigh`. These are immutable
Horo-owned operational profiles, not renderer APIs, hardware detection results,
authority classes or qualification workload names. The product requests an ordered
allowed set; host composition resolves one effective profile against installed
capabilities and global owner budgets before plan admission.

The selected profile is captured in source/cook identity, cooked plan, spatial input,
evaluation, target preparation, metrics and commit receipt. Runtime never changes it
because a frame is slow or a queue is full. A different profile means a different plan/
operation identity and requires normal replacement.

`PCGBaseline` supports offline bake, validation and bounded editor preview. Runtime
semantic commit is unavailable. `PCGStandard` supports those modes plus bounded
portable/profile-deterministic runtime and headless/server execution. `PCGHigh` supports
the same semantic modes at larger scale; it does not enable post-1.0 features or weaken
determinism, trust or authority.

### 2. Every tier has exact structural and data ceilings

All counts are inclusive hard ceilings. All bytes are binary MiB and include container
capacity, alignment and allocator/accounting overhead charged to PCG unless an owner
receipt assigns the allocation elsewhere.

| Limit | `PCGBaseline` | `PCGStandard` | `PCGHigh` |
|---|---:|---:|---:|
| Nodes per cooked graph | 32 | 256 | 1,024 |
| Edges per cooked graph | 64 | 512 | 2,048 |
| Exposed inputs | 32 | 128 | 512 |
| Referenced asset/provider dependencies | 64 | 512 | 2,048 |
| Graph dependency depth | 1 | 1 | 1 |
| Attributes per point | 16 | 32 | 64 |
| Canonical attribute value bytes per point | 256 | 512 | 1,024 |
| Points in one node output | 16,384 | 262,144 | 2,097,152 |
| Total materialized point records per evaluation | 65,536 | 1,048,576 | 2,097,152 |
| Output intents per generated set | 16,384 | 262,144 | 2,097,152 |
| Cooked plan plus PCG auxiliary artifacts | 2 MiB | 16 MiB | 64 MiB |
| Bounded diagnostics per operation | 256 / 1 MiB | 1,024 / 4 MiB | 4,096 / 16 MiB |

Dependency depth is one because 1.0 graphs cannot invoke graph/subgraph assets. The
dependency table may reference ordinary input/output assets and providers, but no entry
recursively expands another PCG plan. PCG-7.7 must introduce a new profile/schema before
hierarchical graphs can raise this value.

Every node descriptor supplies a checked monotonic cost function over validated input
bounds. Cook proves each stage and the whole plan stay within the profile. Runtime
recomputes/validates declared envelopes before allocation. Addition, multiplication,
alignment and conversion use checked arithmetic; overflow is a typed invalid-plan/input
failure, never saturation or wraparound.

The total materialized-record ceiling counts all simultaneously retained intermediate
node outputs plus the final candidate. It is not a cumulative count of records processed
after an intermediate has been deterministically released. Cook liveness analysis must
prove the peak across graph stages; a wide/deep graph that retains too many outputs is
rejected even when each individual node stays below its per-output ceiling.

### 3. Memory and in-flight limits include all lifecycle strata

| Charged PCG envelope | `PCGBaseline` | `PCGStandard` | `PCGHigh` |
|---|---:|---:|---:|
| Resident decoded plans/catalog leases | 16 MiB | 128 MiB | 512 MiB |
| Captured spatial/input snapshots | 16 MiB | 128 MiB | 512 MiB |
| Active evaluation scratch/intermediates | 32 MiB | 256 MiB | 1,024 MiB |
| Candidate outputs/diagnostics | 16 MiB | 256 MiB | 1,024 MiB |
| Old/new replacement and retirement overlap | 32 MiB | 256 MiB | 1,024 MiB |
| Total PCG-owned/charged aggregate | 112 MiB | 1,024 MiB | 4,096 MiB |
| Concurrent evaluating operations | 1 | 4 | 8 |
| Concurrent target-preparing transactions | 1 | 2 | 4 |
| Queued admitted operations | 4 | 32 | 64 |
| Queued terminal completion records | 8 | 64 | 128 |

The aggregate is the sum of these maxima; implementations may configure lower slices
but may not exceed the total or silently borrow from another owner. Shared physical
allocations have one charge identity. Assets cache/provider bytes, target-owner state,
GPU resources, Physics/Navigation objects and World Streaming reservations are reported
by their owners and are not relabeled as PCG memory.

An operation reserves its complete plan/input/scratch/candidate/target/overlap envelope
before starting. Unknown cost is not zero. Growth beyond a reservation must acquire a
bounded extension before allocation; denial cancels/fails the candidate without partial
publication. Retired memory remains charged until every worker/owner lease acknowledges
release.

### 4. Work and owner-lane time are capped per budget epoch

One interactive PCG budget epoch is one committed presentation frame. One headless/
server epoch is one fixed simulation tick. Catch-up ticks or a long frame do not
multiply the allowance.

| Runtime gate | `PCGBaseline` | `PCGStandard` | `PCGHigh` |
|---|---:|---:|---:|
| Newly admitted node work units per epoch | 0 runtime | 262,144 | 1,048,576 |
| PCG owner-lane admission/completion work p95 | 0.25 ms | 0.75 ms | 1.50 ms |
| Evaluation worker CPU budget per epoch | preview only, 2.00 ms | 4.00 ms | 8.00 ms |
| Target-ready candidates committed per epoch | 0 runtime | 1 | 2 |
| New generated output intents committed per epoch | 0 runtime | 16,384 | 65,536 |

A work unit is a versioned semantic node operation over one canonical point/record or a
declared constant-cost unit for non-point nodes. Node schemas must publish conversion
to work units; qualification checks it against measured CPU separately. Work units are
portable admission accounting, not a promise that processors have equal throughput.

Worker jobs may span epochs. The owner admits/schedules no more new work than the epoch
allows and publishes completions in stable identity order. It never waits synchronously
for a worker to satisfy a frame/tick deadline. Offline bake uses the same memory/queue
limits but a separately declared throughput/deadline workload rather than pretending
the interactive per-epoch budget is unbounded.

### 5. Queue pressure has deterministic non-destructive behavior

Admission validates and reserves before returning a handle. When operation, completion,
target-preparation or memory capacity is unavailable, the request returns typed
`AdmissionRejected`/`Backpressure` with the saturated finite dimension and no mutation,
job or hidden queue entry.

Runtime/server requests are ordered by explicit product priority, authority epoch and
stable request identity. Arrival timing, worker completion and hash order never choose
a winner. Active work is not killed to admit a newer equal-priority request. Product
policy may cancel a lower-priority operation only through an explicit generation-fenced
command whose retirement completes before the replacement consumes released capacity.

Editor preview may coalesce only not-yet-admitted requests for the same document/session/
preview slot, retaining the newest revision. An admitted preview is cancelled through
the ordinary lifecycle. Bake, save, authoritative runtime and cleanup requests are
never silently coalesced or dropped.

Completion-queue exhaustion is prevented by reserving a terminal record at admission.
Every admitted operation therefore reaches exactly one terminal result even during
shutdown. Metrics/diagnostics saturation drops only explicitly lossy observations with
a counted drop marker; it cannot drop lifecycle receipts or convert missing evidence to
success.

### 6. Limit breach never degrades semantics implicitly

Structural/data violation at source/cook/load fails before activation. Runtime dynamic
input or output growth beyond the captured plan/profile returns `CapacityExceeded` and
publishes no candidate. Time budget exhaustion yields/resumes within the same immutable
operation or returns a typed deadline/cancellation result under explicit policy; it
does not truncate points, skip nodes, reduce attributes, choose another seed, omit
required target output or commit a prefix.

An optional reduced plan is legal only when authored/cooked product policy declares its
exact identity, semantic meaning, determinism and target readiness. Selection happens
before evaluation and appears in plan/candidate/commit/save/network identity. It is not
invented in response to overload.

Pressure may reject new work, defer an unadmitted request, request World Streaming/
product policy action, evict only disposable unleased caches or cancel explicitly
authorized work. It cannot evict canonical source, active target state or leased old
generations and cannot make PCG a target-owner eviction authority.

### 7. Every external byte/value source is untrusted until admitted

Graph source, sidecars, cooked plans, packages, cache entries, imported attributes,
spatial provider values, save/network reconstruction references, CLI/MCP inputs and
extension manifests are untrusted even when local, authenticated or previously cached.
Trust provenance informs policy and auditing but never waives schema, bounds, digest,
compatibility, capability or semantic validation.

Parsers validate envelope length and version before allocation, cap nesting/counts/
strings/blobs, use checked arithmetic, reject duplicate/unknown required fields and
canonicalize finite numeric values. Graph validation rejects cycles, undeclared node/
query/output capabilities, recursive graph references, invalid identities, explosive
cost functions and dependency closure beyond the selected profile.

Cook/cache/package reuse validates the same key, envelope, digest, dependencies, schema,
cost and profile as fresh output. A malformed entry is quarantined/evicted by its owner
and recomputed only in an authorized authoring host. Runtime never repairs bytes,
downloads code/providers, compiles source or accepts an older/empty substitute.

Fuzzing and fault injection apply before and after every decode boundary. Allocation/
CPU limits cover rejected input as well as accepted output, preventing a small encoded
graph from forcing unbounded validation work or diagnostic amplification.

### 8. Built-in code trust does not imply input or resource trust

The 1.0 execution catalog contains repository-built, host-composed PCG nodes only. Their
code follows the same pure evaluator, declared capability/determinism/cost, snapshot and
shutdown contracts. Bugs or invalid declarations fail catalog/qualification; being
built in does not permit live Scene access, arbitrary allocation, native target calls
or unbounded logging.

Project graph content cannot register executable nodes, native libraries, scripts,
WASM modules, processes or remote providers. Unknown node types fail cook/load.
`PCGNodeCatalog` manifests are inert; package discovery/trust belongs to Extension
Manager, and host activation remains explicit.

PCG-7.8 may introduce versioned custom providers post-1.0 only after a separate accepted
ABI/sandbox decision covers authenticity, permissions, memory ownership, traps/timeouts,
determinism certification, cost verification, diagnostic privacy, module replacement
and shutdown. It cannot weaken built-in validation or retroactively become required for
M5 qualification.

### 9. Observability is low-cardinality and non-authoritative

PCG registers bounded typed metric descriptors through Observability for:

- active/queued/evaluating/preparing/retiring operation counts;
- plan/input/scratch/intermediate/candidate/retirement bytes by profile and lifecycle
  class;
- admitted/completed/cancelled/stale/failed/backpressured totals by stable outcome;
- nodes/work units/points/output intents processed and per-epoch admitted/committed;
- cook/evaluation/target-readiness/retirement latency histograms;
- cache hit/miss/reject and retry/coalescing/drop totals; and
- active profile, plan/catalog/capability/measurement schema revisions.

Allowed dimensions are finite registered enums such as execution mode, profile,
determinism class, lifecycle phase, target kind and outcome. Graph/asset/node/object/
cell/player/user/operation IDs, paths, coordinates, seeds, source labels, attribute names
or values, native handles and arbitrary error text are prohibited metric dimensions.

Metrics and traces observe committed owner states/receipts; they do not control
scheduling, retry, fallback, eviction, authority or commit. Missing required measurement,
dropped required sample or stale measurement schema is explicit and invalidates a
qualification run; it is never zero or pass.

### 10. Detailed diagnostics are bounded, redacted and capability-gated

Normal diagnostics contain stable registered codes, phase/node type (not instance),
profile, counts/costs, generation-safe opaque correlations and bounded redacted cause
summaries. They do not contain source snippets, absolute paths, raw asset/entity/cell
IDs, coordinates, seeds, attribute payloads, player data, credentials, pointer values or
provider-native text.

An on-demand `PCGDiagnosticSnapshot` may include per-node/operation evidence only with
an authenticated local developer/editor capability, explicit world/document scope and
finite item/byte/time budget. Stable identities are pseudonymized for the session; paths
and values are allowlisted/redacted before publication. Truncation and dropped fields
are explicit. The snapshot is immutable and generation-scoped.

Capture/export uses process Observability retention, consent, output-root, privacy and
redaction policy. PCG owns no private log file, trace directory or telemetry uploader.
Untrusted source/provider strings never become format strings, metric names or
unredacted durable evidence. Production/headless/server defaults expose aggregate
counters and typed failures only.

### 11. M5/1.0 core scope is closed and independently acceptable

The following are required for the M5/1.0 PCG capability:

- versioned single DAG graph assets and one immutable cooked CPU execution plan;
- built-in core node families from ADR-152 with no graph/subgraph invocation;
- deterministic immutable spatial snapshots and CPU query/evaluation;
- `PCGBaseline`, `PCGStandard` and `PCGHigh` admission/overload behavior defined here;
- offline validation/bake and isolated ordinary-path editor preview;
- bounded Standard/High runtime/headless/server execution when the product requires it;
- exact generated provenance, target-owner aggregate commit/rollback and TRF/WST/NAV/
  Scene/Prefab integration;
- explicit materialized/regenerable persistence/network policy;
- low-cardinality metrics, bounded redacted diagnostics, overload/abuse coverage; and
- representative cross-platform/headless/server soak and release qualification.

PCG-7.2 (metrics/snapshots), PCG-7.3 (traces/inspectors), PCG-7.4 (overload/trust/fault
coverage), PCG-7.5 (representative benchmarks/determinism matrix) and PCG-7.6
(cross-platform/headless/server qualification) provide the required evidence. Their
absence blocks M5 because they prove this closed core contract.

### 12. Hierarchical graphs and custom providers are post-1.0

PCG-7.7 hierarchical/subgraph composition and PCG-7.8 custom-node provider extension
are post-1.0 optional capabilities. M5 parent acceptance, 1.0 packages, fixtures,
profiles and qualification matrices must not depend on them, list them as expected
failures or require placeholders that execute their semantics.

Hierarchical graphs require explicit recursion/dependency-cycle policy, expanded-cost
closure, stable cross-graph parameter/output binding, cook/cache identity, diagnostics,
replacement and call-depth limits. Until PCG-7.7 is accepted, a graph reference as an
executable node is rejected and version-1 dependency depth remains one.

Custom providers require the PCG-7.8 trust/ABI/sandbox contract described in Section 8.
Until accepted, packages containing required non-built-in executable node/provider code
are unsupported. Optional unknown nodes are not skipped because that would change graph
semantics.

Other post-1.0 candidates include GPU graph evaluation, distributed/remote cook,
runtime scripting, unbounded cross-cell/global generation and implicit incremental
execution. They require new explicit recipes/profiles/workloads and cannot be enabled
under version-1 identities or used retroactively to satisfy M5 gates.

### 13. Qualification workloads are versioned and reproducible

Version 1 defines two protected workload families:

- `PCGCore1_0`: exercises `PCGStandard` at exactly 256 nodes, 512 edges, 32 attributes,
  1,048,576 total point records and 262,144 output intents across offline bake, preview,
  runtime, headless and server-authoritative paths.
- `PCGHigh1_0`: exercises `PCGHigh` at exactly 1,024 nodes, 2,048 edges, 64 attributes,
  8,388,608 total point records and 2,097,152 output intents, including maximum
  old/new overlap and target readiness.

Each descriptor fixes graph/source/dependency hashes, node catalog, seed, spatial input,
target/profile/determinism plan, host mode, fixed tick, cache state, input trace,
toolchain/build, measurement schema and expected target owners. A smaller graph, warm
cache, different seed/profile, disabled required target or reduced output is a different
workload and cannot pass the gate.

Both workloads must prove exact-boundary success and one-over-limit rejection for every
Section 2–4 dimension, byte-identical portable deterministic plan/result fingerprints,
zero partial target publication, queue/backpressure behavior and complete retirement.
They also apply ADR-051 relative regression/stability rules on protected runner cohorts.

PCG-7.5 freezes concrete throughput/latency baselines after measured implementation
data exists. This ADR does not invent hardware throughput targets without measurements;
it fixes absolute capacity, memory, work and owner-lane budget ceilings that
implementation must meet. A missing required metric, cohort mismatch or insufficient
sample set invalidates the run.

### 14. Cancellation, replacement and shutdown remain inside budgets

Cancellation closes new work for the operation, uses its reserved terminal record and
retains all charged inputs/intermediates/candidates/target handles until workers and
owners acknowledge retirement. It does not free capacity optimistically or publish a
partial output to reduce pressure.

Profile/catalog/plan/input/target replacement reserves the complete old/new overlap in
Section 3 before preparation. On denial or failure the old generation remains active.
New work observes the new root only after atomic commit; old work never switches
profiles or cost semantics mid-evaluation.

Shutdown closes admission, invalidates generations, cancels operations, drains bounded
completion queues, rolls back target candidates, retires diagnostics/caches and releases
plans/inputs/modules/providers in dependency order. Shutdown work uses already reserved
records/memory and cannot allocate an unbounded emergency queue. A deadline reports
typed incomplete retirement and retains reachable ownership; it never fabricates
receipts or force-frees storage.

### 15. Contract and abuse-resistance coverage is required

Automated coverage must include:

- exact boundary and one-over-limit tests for every structural, data, byte, concurrency,
  queue, work and per-epoch dimension on all three profiles;
- checked arithmetic overflow, small-encoded/explosive graphs, cyclic/recursive
  dependencies and pathological attribute/diagnostic amplification;
- complete reservation, extension denial, shared charge identity and old/new/retiring
  accounting;
- deterministic admission ordering, queue saturation, editor coalescing, runtime no-
  drop behavior and reserved exactly-once terminal completion;
- time slicing without partial results or semantic truncation;
- fresh/cache/package validation equivalence and malformed/corrupt/version-skewed input;
- built-in catalog restrictions and rejection of scripts/native/WASM/remote/unknown
  executable providers;
- metric cardinality/availability/drop rules and diagnostic privacy/redaction/truncation/
  permission/export behavior;
- `PCGCore1_0` and `PCGHigh1_0` determinism, capacity, target rollback, headless/server
  and retirement evidence;
- M5 matrix independence from PCG-7.7/7.8 and hard rejection of their semantics under
  version-1 plans; and
- cancellation, replacement and shutdown under full queues/budgets with no worker,
  callback, target, provider, module, metric or diagnostic lease surviving its owner.

Fuzz/property tests enforce parser/graph/query/cost invariants. Deterministic virtual
scheduling and fault injection cover every allocation, queue, validation, publication
and acknowledgement boundary. Qualification evidence records exact workload/cohort/
profile/measurement revisions and cannot omit failed or slow samples.

## Consequences

### Positive

- Every supported PCG tier has exact admission, memory, work, queue and overload
  behavior.
- Malformed or hostile graphs cannot convert a small encoded input into unbounded work,
  memory or diagnostics.
- Metrics remain low-cardinality and detailed evidence follows explicit privacy and
  capability policy.
- M5/1.0 has a closed measurable built-in single-graph CPU scope.
- Hierarchical graphs and custom providers can evolve after 1.0 without blocking or
  silently weakening core qualification.

### Negative

- Profile limits may reject existing prototypes until their graph/data costs are made
  explicit or split by design.
- `PCGHigh` requires up to 4 GiB of explicitly reserved PCG-owned/charged overlap and is
  appropriate only for product/runner configurations that grant it.
- Portable deterministic queries/evaluation and complete instrumentation add CPU/memory
  overhead that must be measured by PCG-7.5.
- Third-party/custom executable nodes are unavailable in 1.0.

## Rejected Alternatives

### Keep tier limits indicative or backend-named

Rejected because admission and overload would be untestable, backend changes would
alter semantics and “unknown” costs could be treated as free.

### Admit work optimistically and truncate when budgets run out

Rejected because point/output prefixes change semantics and can partially mutate target
state or make deterministic results depend on pressure timing.

### Use automatic quality reduction on overload

Rejected because fallback changes plan/result identity and must be authored, cooked and
selected explicitly before evaluation.

### Trust local/cached/signed graph bytes without full validation

Rejected because provenance/authenticity does not prove bounds, compatibility,
semantic validity or absence of accidental corruption.

### Put graph/node/asset IDs and coordinates into metrics

Rejected because high-cardinality dimensions create unbounded cost and privacy leakage;
detailed evidence belongs to bounded capability-gated snapshots.

### Require hierarchical graphs or custom providers for M5

Rejected because they add recursion and ABI/sandbox trust surfaces beyond the closed
1.0 core and would prevent independent qualification of the built-in path.

### Claim throughput gates before representative measurements exist

Rejected because performance claims require measured workloads, cohorts and baselines.
This decision fixes capacity and admission ceilings; PCG-7.5 freezes measured rate/
latency gates with evidence.
