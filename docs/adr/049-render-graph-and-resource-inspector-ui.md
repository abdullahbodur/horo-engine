# ADR-049: Render Graph and Resource Inspector UI

- **Status**: Proposed
- **Date**: 2026-09-01
- **Supersedes**: None
- **Scope**: Editor renderer-inspection snapshots, projection model and dockable UI ownership
- **Issue**: [RND-017.9](https://github.com/abdullahbodur/horo-engine/issues/441)
- **Jira**: [HORO-441](https://horo-engine.atlassian.net/browse/HORO-441)
- **Companion decisions**: [ADR-027](027-renderer-resource-identity-and-descriptors.md), [ADR-041](041-backend-neutral-renderer-diagnostics-model.md), [ADR-042](042-cpu-gpu-timestamps-and-pipeline-statistics.md), [ADR-043](043-gpu-memory-and-resource-inspection.md), [ADR-044](044-render-markers-and-debug-labels.md)
- **Normative documents**: [Rendering Architecture](../architecture/runtime/rendering-architecture.md), [Editor Panel And Tab Architecture](../architecture/editor/editor-panel-host.md), [Editor UI Design System](../architecture/editor/ui-design-system.md), [Metrics And Profiling](../architecture/observability/observability-performance.md)

## Context

Passes, resources, lifetime intervals, barriers, queues, GPU timing and memory are
owned by different renderer models and may become available at different times.
A UI that walks live frontend/backend state during `Draw()` would race mutation,
leak native identities, change collection cadence and keep GPU resources alive.
Simply combining the latest graph, timing and memory values could also present a
convincing but false view assembled from unrelated frames or generations.

The inspector is a persistent engineering workspace, not a renderer authority or
a modal workflow. It needs a bounded immutable projection, explicit capture,
typed absence/partial coverage and presentation that remains useful on Null and
across backend capability differences.

## Decision

### 1. A dockable tab consumes an application query service

HoroEditor registers one `RenderInspectorTab` with `EditorPanelHost`. It is closed
by default and may be placed in any tab stack; opening it never changes renderer
instrumentation. `PerformanceTab` remains the metrics/profiler surface. The
inspector may link to matching performance/capture sessions but does not absorb
their service ownership.

The application composition creates `RendererInspectionService` over narrow
frontend graph-inspection, ADR-043 memory-inspection, ADR-042 measurement and
ADR-041 diagnostic query capabilities. The tab receives only this service and
immutable operation snapshots. It does not depend on `RenderFrontend`, concrete
backends, native APIs, resource registries, memory ledgers or observability stores.

```text
Editor tab -> RendererInspectionService -> immutable inspection bundle/pages
                         |
                         +-> frontend graph projection
                         +-> ADR-043 memory/resource snapshot
                         +-> ADR-042 matching result batches
                         +-> ADR-041 bounded correlated diagnostics
```

The service and source descriptors are inert until composed. Tab construction,
layout restore or visibility changes cannot capture a graph, enable timestamps,
walk resources or register backend callbacks.

### 2. Inspection is an explicit typed operation

```cpp
struct RendererInspectionRequest {
    RendererInspectionTarget target;
    RendererInspectionDetail detail;
    RendererInspectionFilters filters;
    RendererInspectionLimits limits;
    RendererInspectionCorrelationPolicy correlation;
    Duration retention;
};
```

Targets are `NextRealRenderFrame`, an admitted future `RealRenderFrameId`, a
retained exact `GraphExecutionId`, or a completed ADR-048 incident graph manifest.
The request carries exact renderer/device generations and optional surface/view.
It never infers current target from editor focus, selected scene object or backend
enumeration. Synthetic presentation without a real graph is explicitly
unsupported.

Detail independently requests graph topology, resource uses/lifetimes, barrier/
queue transitions, ADR-043 allocation/resource records, ADR-042 CPU/GPU timing and
pipeline statistics, marker labels and correlated diagnostics. Unsupported detail
returns typed availability or `Partial` only when the caller allowed it. Opening a
collapsed UI section does not issue a new native query; recapture is explicit.

Operation states are `Queued`, `Arming`, `CapturingGraph`,
`AwaitingMeasurements`, `Materializing`, `Ready`, `Partial`, `Cancelled`,
`StaleGeneration`, `DeviceLost` and `Failed`. The tab observes these states and can
cancel or request another capture. Rendering success/failure never depends on the
inspection result.

### 3. The base graph snapshot is immutable and self-consistent

The frontend captures one post-compilation, post-culling/merge/queue-assignment
graph execution projection before native submission. Capturing the projection does
not mutate graph semantics:

```cpp
struct RendererGraphInspectionSnapshot {
    RendererGraphInspectionSnapshotId id;
    RendererGeneration renderer;
    DeviceGeneration device;
    RealRenderFrameId frame;
    GraphExecutionId graph;
    RenderPlanGeneration plan;
    EffectiveCapabilitiesRevision capabilities;
    RendererGraphInspectionCoverage coverage;
    RendererGraphInspectionPageManifest pages;
};
```

Records use stable Horo identities and contain:

- passes: pass ID/kind, registered display identity, queue, compile/execution
  order, cull/merge provenance and marker scope;
- resources: exact ADR-027 typed identity/class/generation, transient/imported/
  persistent class, descriptor summary/fingerprint and optional redacted label;
- uses: pass/resource, read/write/access/stage/usage semantics and subresource range;
- dependencies: producer/consumer pass and typed reason;
- lifetimes: first/last admitted use, queue intervals and alias-group proof;
- synchronization: logical transition/hazard reason, source/destination queue,
  stage/access semantics and ownership transfer; and
- submissions/presentation: queue batch, dependency and exact surface/frame link.

Native barrier enums, pointers, handles, command-list indices and GPU addresses do
not cross the adapter. A backend may add a bounded normalized realization record
only when mapped to the Horo logical transition. The UI never infers a barrier
solely because two passes are adjacent.

Snapshot capture uses pre-reserved fixed graph metadata already produced by
compilation plus optional finite inspection projection. It performs no native
enumeration, resource-content mapping, device idle or full registry walk. Values
are copied/owned until snapshot expiry and cannot retain graph/resource lifetimes.

### 4. Memory and timing join only by proven identity

The service coordinates graph and ADR-043 requests at one render safe point. A
memory/resource snapshot joins only when renderer/device generations match and its
resource records prove the graph identities/revision requested. If exact revision
alignment cannot be obtained, memory detail is `Unavailable` or separately labeled
`NearestAggregate` with source revision/time; it is never shown as exact per-graph
allocation truth.

ADR-042 measurement batches may arrive after the base graph snapshot. The bundle
retains an immutable base and publishes a new overlay revision only for results
whose renderer/device, real frame, graph, queue, scope and clock generations match.
Pending, unsupported, disjoint, partial and stale timings remain explicit. The UI
does not display zero or reuse a same-named pass from another frame. Pipeline
statistics preserve their canonical semantic/availability and are never compared
across unsupported scopes as if equivalent.

ADR-041 diagnostics join by exact generation/frame/graph/pass/resource context.
Uncorrelated device-wide messages appear in a separate device section and are not
attached to a selected pass heuristically. ADR-044 registered labels are
presentation metadata only, never join identity.

```cpp
struct RendererInspectionBundle {
    RendererInspectionBundleId id;
    RendererGraphInspectionSnapshot graph;
    std::optional<RendererMemoryInspectionSnapshotId> memory;
    RendererInspectionOverlayRevision overlays;
    RendererInspectionCoverage coverage;
};
```

Every overlay update preserves source identity/provenance and notifies one
coalesced bundle revision. Existing returned page values remain immutable; clients
request pages for the new overlay revision explicitly.

### 5. Pages, filters and budgets are finite

Graph projection defaults to 1,024 passes, 8,192 resources, 32,768 uses/edges,
16,384 synchronization records and 16 MiB encoded data. Hard limits are 8,192
passes, 65,536 resources, 262,144 uses/edges, 131,072 synchronization records and
128 MiB. ADR-043 resource/allocation limits remain independently authoritative;
the composite request cannot widen them. Checked arithmetic validates all counts,
subresource ranges and bytes before admission.

One page defaults to 256 rows or 256 KiB and has hard maxima of 1,024 rows and
1 MiB. Pages are move-only owned immutable values with identity/position cursors,
the same lifetime rule as ADR-043. A tab may hold eight pages by default and at
most 32; further reads return backpressure. Expiry rejects new reads but does not
invalidate already returned owned pages.

Filters use finite typed sets, exact IDs, bounded registered-name search and
numeric ranges. Free-text search is applied on a worker to already materialized
bounded safe display strings; regex/native message/path search is not evaluated on
the renderer owner thread. Sorting and graph layout run on cancellable workers over
owned pages and have finite output/time budgets.

By default one bundle may arm and two completed bundles may be retained per
renderer; hard maxima are two arming and eight retained. Retention defaults to
60 seconds and cannot exceed 10 minutes unless pinned by an explicit developer
session within the same byte/count budgets. A pinned bundle never pins GPU state.

If limits truncate data, coverage records total-known/returned counts, omitted
stable-ID intervals and whether graph topology is still closed. A topology view is
`Complete` only when every included edge endpoint is present. Partial data never
draws an edge to a fabricated “unknown pass”; it uses an explicit omitted boundary.

### 6. The view model is a read-only projection

`RenderInspectorViewModel` owns only presentation state: selected bundle/pass/
resource, expanded groups, typed filters, search draft, table sort, graph pan/zoom,
column widths and chosen timeline scale. Selection uses bundle-scoped stable IDs.
It cannot destroy/relabel resources, change barriers/queues/pass order, force
residency, recook shaders, alter graph culling or enable capabilities.

Primary views are:

- **Graph**: passes/submissions as accessible nodes with typed dependency/use edges;
- **Passes**: virtualized table and detail for queue, inputs/outputs, transitions,
  markers, measurements and diagnostics;
- **Resources**: virtualized table and detail for descriptor, lifecycle, graph
  uses, allocation references and lifetime/alias intervals;
- **Timeline**: per-queue CPU/GPU spans with explicit unavailable/pending/disjoint;
- **Memory**: ADR-043 aggregate/pool/allocation relationships without resumming
  overlapping values; and
- **Capture**: operation status, coverage, source revisions and recapture/export
  actions.

Clicking a graph node and selecting the same pass row are one view-model command.
Links to `PerformanceTab`, Console or an ADR-047/048 artifact route through typed
application/editor commands carrying exact session/bundle/context IDs. The tab
does not parse URLs, log text or filenames to correlate.

### 7. UI behavior follows editor design and accessibility contracts

The tab uses shared editor tables, tree rows, splitters, search fields, badges,
empty/error states, design tokens and localization. It does not draw raw ImGui
controls where a shared control exists. No meaning relies on color alone: queue,
access, availability and hazard states include text/icon/pattern semantics.

The graph supports keyboard node traversal in stable execution/ID order, focus
indication, zoom controls, “fit selection/all”, list-view equivalence and screen-
reader labels. Tooltips supplement rather than replace table/detail values.
Minimum typography and hit targets remain valid at narrow widths and translated
text. Large graphs default to clustered/virtualized rendering and never shrink
text below design-system roles merely to fit.

Loading, no-renderer, Null, unsupported-detail, awaiting-timing, partial,
cancelled, stale-generation, device-loss and expired states are distinct localized
surfaces with actionable recapture/open-provider guidance. An unavailable value is
not rendered as `0`, blank success or disabled checkbox implying user control.

The visible tab may request page/layout work at a throttled bounded cadence after
a committed revision. A hidden/collapsed tab performs no polling, page prefetch,
layout, search formatting or measurement capture. Coalesced revision notifications
only mark the view model dirty; `Draw()` never changes source collection cadence.

### 8. Threading, cancellation and shutdown are explicit

Graph/source revision capture and final admission occur on the render-capable
owner thread at safe points. Projection preparation, joins, sorting, search,
layout and export run on cancellable workers over owned immutable values. GUI
state changes occur on the GUI thread. No worker captures a tab, backend, registry,
ledger or page reference beyond its ownership token.

Closing the tab cancels its unpublished UI requests and releases page values but
does not cancel another client's operation or GPU work. Cancelling a bundle before
capture removes reservations; after source capture it suppresses remaining
materialization/overlays and releases service storage. Already published pages
remain valid under bounded ownership.

Device/backend replacement stops old-generation admission and marks retained
bundles historical/stale. They remain inspectable only with a visible source-
generation badge and cannot issue current renderer actions. Shutdown rejects new
requests, cancels workers, expires cursors, releases service storage and then
destroys the tab/view model. It is idempotent after partial initialization and
never waits for UI, GPU idle or external capture tools.

### 9. Export and parity preserve the same model

An explicit export writes the already captured Horo bundle manifest/pages to the
Platform diagnostics state root or user-selected validated path with atomic
publication, schemas, source revisions, coverage, byte sizes and checksums. It
does not export resource/shader contents, native handles, graphics-capture payloads
or unrestricted labels/paths. Export cannot trigger recapture silently.

OpenGL, Metal, Vulkan and D3D12 provide the same logical graph/resource/use/
lifetime/synchronization schema. Normalized realization and measurements may be
unavailable. Null generates deterministic synthetic graph, barrier, timing and
memory fixtures and validates all UI/service states, but cannot qualify native
barrier realization, timing accuracy or memory observations.

## Migration And Verification

Existing Performance views keep metrics/profiler ownership. Ad hoc backend debug
windows migrate to `RenderInspectorTab` and immutable service bundles. No backend
debug UI or editor-side registry walk remains. The initial graph authoring model
must add typed resource uses/dependencies before claiming complete barrier/lifetime
inspection; until then unsupported detail is explicit.

Tests must cover exact frame/graph/surface/generation targeting; complete/partial/
omitted topology; pass/resource/use/lifetime/barrier/queue fixtures; delayed,
disjoint and stale measurement joins; exact/nearest/unavailable memory joins;
diagnostic/marker correlation without text heuristics; all count/byte/page/lease/
retention limits; cancellation, hidden-tab zero work, device loss, historical
bundles and shutdown; virtualized tables/large graphs; keyboard/focus/list-view/
non-color accessibility; narrow/localized layouts and all empty/error states;
atomic safe export; deterministic Null fixtures; and native backend qualification.

## Consequences

Engineers can inspect one evidence-backed graph execution across passes, resources,
barriers, queues, timings and memory without the editor touching live renderer
state. Delayed and partial sources remain honest, UI work is bounded/virtualized,
and the tool cannot mutate rendering. The cost is a graph inspection projection,
composite snapshot coordination, paging/layout infrastructure and substantial UI/
backend fixture coverage.

## Rejected Alternatives

| Option | Decision and reason |
|---|---|
| Walk live frontend/backend state from `Draw()` | Rejected: races lifetime, leaks native details and couples UI cadence to rendering. |
| Put all inspector detail in `PerformanceTab` metrics | Rejected: topology/resource identity is high-cardinality snapshot data, not metrics. |
| Join the latest graph, memory and timing values by name | Rejected: produces false cross-frame/generation correlation. |
| Let the inspector mutate barriers/resources/residency | Rejected: a diagnostic projection is not renderer policy or authoring authority. |
| Copy an entire large graph every UI frame | Rejected: unbounded frame/GUI work; explicit paged snapshots own detail. |
| Show unavailable values as zero | Rejected: confuses unsupported/pending/disjoint with measured zero. |
| Use native handles/enums in UI rows | Rejected: breaks backend parity, privacy and generation safety. |
| Keep hidden tabs polling for freshness | Rejected: presentation visibility cannot create instrumentation/formatting cost. |
| Treat Null rendering as native qualification | Rejected: fixtures prove contracts/UI, not driver realization or measurement accuracy. |
