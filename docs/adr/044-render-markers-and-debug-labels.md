# ADR-044: Render Markers and Debug Labels

- **Status**: Proposed
- **Date**: 2026-09-01
- **Supersedes**: None
- **Scope**: Backend-neutral render marker identity, placement, correlation and native debug labels
- **Issue**: [RND-017.4](https://github.com/abdullahbodur/horo-engine/issues/436)
- **Jira**: [HORO-436](https://horo-engine.atlassian.net/browse/HORO-436)
- **Companion decisions**: [ADR-027: Renderer Resource Identity and Descriptors](027-renderer-resource-identity-and-descriptors.md), [ADR-040: Reconstruction, Frame Generation and Latency Providers](040-reconstruction-frame-generation-and-latency-providers.md), [ADR-041: Backend-Neutral Renderer Diagnostics Model](041-backend-neutral-renderer-diagnostics-model.md), [ADR-042: CPU/GPU Timestamps and Pipeline Statistics](042-cpu-gpu-timestamps-and-pipeline-statistics.md), [ADR-043: GPU Memory and Resource Inspection](043-gpu-memory-and-resource-inspection.md)
- **Normative documents**: [Metrics And Profiling](../architecture/observability/observability-performance.md), [Rendering Architecture](../architecture/runtime/rendering-architecture.md), [Render Backend Parity](../architecture/runtime/render-backend-parity-contract.md)

## Context

Horo has generation-aware frame, graph, pass, queue and resource identities, but
does not define how those identities become markers visible in native GPU tools or
backend debug labels. Letting every backend format arbitrary pass/resource strings
would make captures incomparable, permit unbounded hot-path allocation, expose
asset paths or native handles and leave begin/end balance dependent on API-specific
call sites.

Several adjacent concepts are intentionally different. ADR-041 diagnostic events
describe findings and failures. ADR-042 measurement scopes own timestamps and
pipeline counters. ADR-040 latency-provider markers represent vendor latency
operations. Profiler zones own Horo capture timelines, while RND-017.7 owns
RenderDoc/PIX/Metal capture activation. A native debug marker may correlate these
facts, but cannot become their identity, result or lifecycle authority.

Native support also differs: queue/encoder scope rules, maximum text lengths,
standalone inserts, colors and object labels are not uniform. Some modes require
debug extensions, layers or build settings selected before device creation. A
requested diagnostic mode must therefore resolve against effective support rather
than assuming Debug builds provide every native facility.

This ADR defines marker descriptors, correlation, graph placement, finite plans,
native encoding, object labels and lifecycle. It does not enable validation layers,
launch external capture tools, define timing semantics, retain captures, upload
telemetry or expose native object identity.

## Decision

### 1. Logical markers are frontend-owned instrumentation

The responsibility split is:

| Responsibility | Owner |
|---|---|
| Stable marker descriptors, graph placement, correlation fields, budgets and fallback policy | Render frontend |
| Native command/encoder marker calls, supported object labels, text limits and private API translation | Selected backend |
| Frame/pass/resource identity and lifetime | Existing frontend graph and ADR-027 registry |
| Diagnostics, timing, profiler retention and external capture lifecycle | ADR-041, ADR-042, Observability and RND-017.7 respectively |

A marker is observational. It cannot change pass ordering, barriers, queue choice,
resource lifetime, frame admission, presentation, quality or gameplay. Backend
failure to emit optional instrumentation cannot make rendering choose a different
path. A required developer/qualification request returns a typed instrumentation
failure to its owner; it still cannot reinterpret completed GPU work.

The host composes one marker registry and lends the frontend a validated descriptor
table. Descriptor construction is inert metadata: it performs no backend calls,
global registration, sink creation or marker emission.

### 2. Descriptors provide bounded semantic identity

Modules contribute static descriptors before renderer activation:

```cpp
struct RenderMarkerDescriptor {
    RenderMarkerId id;
    ModuleId owner;
    RenderMarkerKind kind;
    RendererSubsystem subsystem;
    RegisteredStringId displayName;
    RenderMarkerColor color;
    RenderMarkerCorrelationMask allowedCorrelation;
    RenderMarkerPlacement placement;
    DiagnosticPrivacyClass privacy;
};
```

`RenderMarkerId` is the registered stable identity used by tests, Horo captures and
native encoding. `displayName` is bounded fallback presentation, not identity.
Version 1 marker IDs contain at most 64 lowercase ASCII letters, digits, dots,
underscores or hyphens; display names contain at most 96 UTF-8 bytes. The reserved
native-token delimiters cannot occur in an ID.
Duplicate IDs, foreign owners, unknown registered strings, invalid colors,
unsupported placement/correlation combinations or unbounded privacy classes fail
composition. Runtime code uses validated handles and never registers an ID or
interns an arbitrary string while recording a frame.

`RenderMarkerKind` distinguishes `Frame`, `Queue`, `Pass`, `ResourceOperation`,
`PipelineOperation`, `ProviderOperation`, `Presentation` and `DiagnosticGroup`.
It is not a native enum. `RenderMarkerPlacement` declares `Scope` or `Insert` and
the allowed frontend phases. Colors are optional visual hints from a fixed palette;
identity, hierarchy and status remain understandable without color.

User, asset, entity and path strings cannot create descriptors. Feature-specific
variants use registered finite IDs. A bounded developer label may accompany an
explicit resource operation under Section 6, but does not create a marker family.

### 3. Correlation is typed and generation-aware

Every emitted logical record contains a descriptor handle, marker sequence and an
applicable subset of typed correlation:

```cpp
struct RenderMarkerCorrelation {
    RendererGeneration renderer;
    DeviceGeneration device;
    std::optional<RealRenderFrameId> realFrame;
    std::optional<SyntheticPresentationFrameId> syntheticFrame;
    std::optional<GraphExecutionId> graph;
    std::optional<RenderQueueId> queue;
    std::optional<RenderPassId> pass;
    std::optional<TypedRenderResourceIdentity> resource;
    std::optional<PipelineIdentity> pipeline;
    std::optional<ProviderOperationId> providerOperation;
};
```

Absence is explicit, never a zero ID. A resource uses its exact ADR-027
owner/slot/generation and resource class; a native handle, pointer, address or
backend object name cannot substitute. A frame marker distinguishes real render
frames from synthetic presentation frames. Generated frames may correlate to their
bracketing real frames but never acquire a false graph/resource identity.

Marker sequence is monotonic within one command-recording context. Cross-queue
ordering is not inferred from sequence or marker nesting; ADR-042 calibration and
submission synchronization remain the timeline authorities. An ADR-040 latency
operation may contribute its typed provider operation ID, while the provider keeps
the operation's semantics and result.

Marker IDs and correlation may enter local capture manifests/records but never
metric dimensions. ADR-041 events may reference one marker/operation ID when
explaining a failure; normal marker traffic does not become diagnostic events.

### 4. One immutable marker plan resolves support and cost

The frontend resolves an immutable generation-scoped plan before graph admission:

```cpp
struct RendererMarkerPlan {
    RendererMarkerPlanId id;
    RendererMarkerPlanGeneration generation;
    RendererMarkerMode mode;
    RendererMarkerBudgets budgets;
    RendererMarkerFallbackPolicy fallback;
    EffectiveCapabilitiesRevision capabilities;
    ProductProfileId productProfile;
    DeviceGeneration device;
};
```

Modes are `Off`, `LogicalCapture`, `NativeMarkers` and
`NativeMarkersAndObjectLabels`. Logical capture writes only an explicitly armed
Horo profiler/capture channel. Native modes additionally call qualified backend
operations. Enabling native validation/debug layers remains RND-017.5; selecting a
marker mode cannot mutate device-creation policy after initialization.

Effective support describes scope/insert availability by queue/encoder/command
context, supported object classes, nesting depth, encoded-text byte limit, color
support, thread constraints and whether the required native facility was enabled
at device creation. A backend may support markers without object labels or one
resource class without another. Unsupported fields are unavailable, not assumed.

Missing support for a required mode returns `MarkerModeUnsupported` before graph
or resource instrumentation is admitted. Optional policy may select only a
declared lower mode from the order above and records one ADR-041 availability
transition. There is no implicit “Debug means native markers” rule and no silent
drop to `Off`.

### 5. Graph compilation owns balanced placement

The graph compiler expands descriptor placements into logical `BeginScope`,
`Insert` and `EndScope` records after pass culling, merging and queue assignment.
It validates:

- begin/end occur on the same queue, native command context and graph execution;
- nesting is well formed and within the plan/backend maximum depth;
- a marker does not cross command-buffer/list, encoder, submission or queue
  boundaries forbidden by the selected backend;
- pass merge/cull either preserves the registered semantic scope or records that
  scope unavailable; it never attaches the old identity to unrelated work;
- resource/pipeline correlation names a valid generation accepted by that work;
- synthetic presentation does not emit a GPU graph scope when no such work exists;
  and
- every required record has pre-reserved command and encoded-text capacity.

Frontend RAII/builders guarantee logical balance on early return. Backends receive
an already-validated stream and translate it without inventing scopes. If a native
encoder must close before a frontend group ends, the capability/plan must permit a
declared segmented projection whose segments retain one logical group ID; otherwise
the scope is unsupported. Native tool nesting is never allowed to dictate render-
graph dependencies.

Worker command recording emits markers only on the thread that owns the private
native command context. Logical record storage owns all values until command
submission/retirement and captures no borrowed label text. A cancelled or rejected
command list discards its complete balanced stream. No marker call waits for GPU
completion, queries a capture tool or performs file/network I/O.

### 6. Resource debug labels are metadata, not identity

ADR-027 creation may carry a `RenderDebugLabel` separately from the immutable
resource descriptor. The label contains either a registered string ID or an
explicit bounded developer string plus privacy class. It does not participate in
descriptor equality, cache keys, handle identity, residency keys, reconstruction
identity or dependent pinning.

Registered labels are preferred. Developer strings must be valid UTF-8, are
normalized to one line, must contain no control characters and are limited to 96
bytes before native encoding. Source/asset paths, user content, credentials,
shader text, native handles/addresses and unrestricted driver labels are forbidden.
Shipping rejects developer strings unless a specific allowlisted diagnostics
profile admits them.

`SetRenderDebugLabel` is an explicit metadata operation. It validates the exact
typed ready resource generation and queues mutation on the render-capable owner
thread. It does not create a new resource generation or mutate the descriptor.
Stale, foreign, pending and retiring handles return their ADR-027 identity/lifecycle
errors. Unsupported native labeling returns `ObjectLabelUnsupported`; an optional
logical-only label remains available only when the resolved plan declared that
fallback. The inspector in ADR-043 may display admitted/redacted labels but cannot
rename through its read API.

Backend-private helper objects may receive deterministic labels derived from a
registered marker/resource identity. Such labels remain private diagnostic aids,
do not become public helper-object identity and cannot expose a native pointer.
Label removal/replacement is serialized with the owning generation. Device
recreation reapplies only labels allowed by the new plan/profile to newly created
generations; it never preserves an old native object label by slot matching.

### 7. Native text encoding is deterministic and safe

Many native tools expose marker correlation only as text. The backend adapter
encodes a fixed Horo token first, followed by an optional registered display name
and compact typed correlation. The token includes schema revision and
`RenderMarkerId`; correlation uses fixed field keys and canonical unsigned
lowercase hexadecimal Horo values. It contains no pointer or native enum.

Version 1 begins with `H1|m=<id>|k=<s|i>|x=<0|1>`, where `s` is scope, `i` is
insert and `x=1` means optional native text was truncated. Optional
correlation follows in this order: renderer generation (`rg`), device generation
(`dg`), real frame (`rf`), synthetic frame (`sf`), graph (`g`), queue (`q`), pass
(`p`), typed resource (`r`), pipeline (`pl`) and provider operation (`po`), then
display name (`n`). Unsigned numeric components use lowercase hexadecimal without
locale or a `0x` prefix. Percent-encoding covers the small allowed UTF-8 display
name suffix. Native APIs whose end operation carries no text correlate it through
the validated scope stack rather than synthesizing a second identity string.

The resource field is never packed into one implementation-defined integer. Its
grammar is `r=<class>:<owner>:<slot>:<generation>`. `class` is one registered
versioned token (`buf`, `tex`, `view`, `samp`, `shd`, `pipe`, `target` or `mesh`),
and the three identity components are separate lowercase hexadecimal values. A
future resource class requires a schema revision or an explicitly compatible new
registered token; it cannot reuse an existing token with new meaning.

The plan reserves the backend's qualified encoded-text limit. If the minimum
identity token cannot fit, native marker support is unavailable. Optional display
and correlation fields are included in fixed priority order. A field may be
omitted/truncated only when the plan explicitly permits reduced native text; the
logical capture retains the complete typed record and the encoded marker includes
a truncation bit. A required-correlation plan fails admission rather than silently
discarding required fields.

Encoding uses prevalidated registered strings and fixed-capacity frame storage.
Numeric correlation formats into bounded stack/arena space; there is no hot-path
heap allocation, locale lookup, string interning or message formatting. Native
labels are normalized through the same privacy and byte-limit policy. Colors are
adapted when supported and omitted without semantic loss when unsupported.

### 8. Budgets and saturation are finite

The default plan admits 256 nested scope pairs and 512 inserts per real-frame
graph, 1,024 registered marker descriptors, 96 KiB of registered display strings
and 256 KiB of in-flight encoded marker storage per real frame. Hard limits are
2,048 scope pairs, 4,096 inserts, 8,192 descriptors, 1 MiB of registered strings
and 2 MiB encoded storage per real frame. Counts across frames in flight use
checked arithmetic and are charged to renderer diagnostics CPU memory before plan
activation. Profiles may lower but not grow these limits at runtime.

Static graph markers are counted after compile transformations and rejected before
frame admission when a required plan cannot fit. Optional policy may omit only
descriptor classes declared optional and reports exact omitted counts/IDs through
one bounded availability result. Runtime inserts reserve from a finite producer
queue before command mutation. An insert beyond reservation returns
`MarkerCapacityExceeded`; it cannot leave an unmatched scope or fail the render
operation that was otherwise valid.

No catch-up markers are emitted on a later frame. Marker records are not persisted
unless a bounded profiler/native capture session is explicitly armed. `Off` mode
allocates no per-frame marker stream. Object labels perform work only on admitted
create/relabel/recreate operations, not once per frame.

### 9. Failure, cancellation and shutdown preserve rendering

Configuration/plan results distinguish `MarkerModeUnsupported`,
`MarkerPlacementUnsupported`, `MarkerNestingUnsupported`,
`MarkerEncodingUnsupported`, `ObjectLabelUnsupported`, `MarkerCapacityExceeded`,
`MarkerProfileDisabled` and `InvalidMarkerPlan`. Session statuses distinguish
`Valid`, `Partial`, `Cancelled`, `StaleGeneration` and `DeviceLost`.

Optional native call failure disables only the affected instrumentation generation
at the next safe boundary and emits one aggregated ADR-041 transition. It does not
retry per command, switch renderer, alter graph execution or hide a failed required
diagnostic session. A required qualification/capture session becomes failed with
the typed cause; already admitted rendering still retires normally.

Within the command context that observes the first failure, the adapter emits no
new native begins or inserts. It remembers which native begins succeeded and makes
one best-effort attempt to close only those scopes in reverse order when the native
API permits recovery; logical records remain balanced regardless. A failed begin
suppresses its matching native end. After the command boundary the failed plan
generation accepts no more native records. This fail-closed path is bounded and
does not repeatedly invoke a failing API.

Cancellation closes/discards logical scopes before native submission and retains
command storage until normal completion rules permit reuse. Device loss invalidates
the marker plan and pending native streams with the old generation. Recreation
resolves a new capability/profile plan. Stale streams and label operations cannot
enter the new device.

Shutdown stops new instrumentation, cancels unsubmitted streams, lets submitted
command storage retire without GPU-idle waits, removes backend hooks on their owner
threads and releases preallocated arenas. It is idempotent after partial setup.
Observability/capture consumers cannot delay renderer shutdown.

### 10. Backend parity and privacy are explicit

OpenGL, Metal, Vulkan and D3D12 translate the same logical descriptor, correlation,
balance and failure semantics through qualified private operations. Backend-specific
marker APIs, encoders, command lists, object classes and maximum text lengths stay
private. Lack of native markers does not disable Horo typed results or logical
capture; it only affects a mode whose fallback was declared.

Null validates descriptor registration, plan resolution, graph placement, nesting,
encoded token fixtures, budgets, cancellation, device generations and object-label
metadata using synthetic records. It reports no native-tool support and cannot
qualify capture visibility, driver overhead or hardware behavior.

Logical records and registered labels are local diagnostics by default. External
capture/export follows Observability and RND-017.7 user intent, retention, redaction,
size and atomic-publication rules. The backend performs no network access. Native
tool attachment does not grant access to resource contents, shader source, paths,
credentials or unrestricted labels through this contract.

## Migration And Verification

Existing ad hoc backend push/pop labels migrate to registered descriptors and
frontend graph placement. Existing resource label strings migrate to the bounded
metadata operation and are removed from descriptor identity/hash logic. RND-017.5
owns validation/debug-layer activation; RND-017.7 consumes the marker plan when
arming external captures. ADR-042 timing scopes may share registered semantic IDs
but retain independent query placement/results.

Tests must cover:

- descriptor duplicate/owner/string/privacy rejection and inert construction;
- exact correlation for real/synthetic frames, graph/pass/queue, typed resource
  generations, pipelines and provider operations with explicit absence;
- cull/merge/queue reassignment, nested/segmented scope validation, early return,
  cancellation and no unmatched native begin/end;
- required unsupported rejection and each declared optional mode fallback without
  changing render work;
- default/hard descriptor, scope, insert, nesting, string, encoded-byte and frames-
  in-flight limits with checked arithmetic and no catch-up burst;
- no frame-hot allocation/interning/locale formatting, owner-thread native calls,
  worker command-context affinity and owned record lifetimes;
- object create/relabel/recreate, stale/foreign/pending/retiring identity errors,
  helper labels and no descriptor/cache/resource-identity mutation;
- native text golden fixtures, minimum-token rejection, deterministic field order,
  UTF-8/control/path/secret redaction, truncation marker and required-correlation
  rejection;
- saturated insert queues, native call failure, device loss/recreation, partial
  initialization and repeated shutdown without render-policy changes;
- metric/diagnostic cardinality boundaries and local capture/export privacy; and
- deterministic Null fixtures plus OpenGL/Metal/Vulkan/D3D12 native visibility and
  overhead qualification under the exact marker plan.

## Consequences

Native captures can correlate frame, pass, queue, resource, pipeline and provider
work through stable Horo identities without exposing backend objects or allowing
tools to own graph placement. Marker cost, text, nesting and lifetime are bounded,
and optional instrumentation failure cannot alter rendering. The cost is a static
descriptor registry, finite per-plan arenas, graph marker validation and one
qualified adapter per backend/tool mode.

## Rejected Alternatives

| Option | Decision and reason |
|---|---|
| Let each backend format arbitrary marker strings | Rejected: loses parity, bounds, privacy and stable identity. |
| Use pass/resource names as marker identity | Rejected: names change, may be private and create unbounded cardinality. |
| Expose native object handles or GPU addresses in labels | Rejected: unsafe, backend-specific and stale across recreation. |
| Emit diagnostic events for every marker | Rejected: normal command traffic is instrumentation, not a finding. |
| Make marker scopes own timestamp queries | Rejected: visual correlation and measured time have different support/lifetime contracts. |
| Infer marker support from Debug builds | Rejected: native facilities and device-creation policy are explicit effective support. |
| Repair unmatched scopes inside a backend | Rejected: hides frontend graph-placement bugs and may cross invalid native boundaries. |
| Allocate or intern dynamic labels while recording | Rejected: frame-hot cost and cardinality must be admitted before execution. |
| Fail rendering whenever optional marker emission fails | Rejected: observational instrumentation cannot become render correctness. |
| Launch or control capture tools from marker adapters | Rejected: external tool lifecycle, permission and artifacts belong to RND-017.7. |
