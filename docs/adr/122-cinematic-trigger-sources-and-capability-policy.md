# ADR-122: Cinematic Trigger Sources and Capability Policy

- **Status**: Proposed
- **Date**: 2026-09-02
- **Supersedes**: None
- **Scope**: Gameplay-script, scene-autoplay, gameplay-event, editor, MCP and remote cinematic triggers; typed capability/trust/authority admission; packaged-build availability; approval reuse; denial and lifecycle outcomes
- **Issue**: [CIN-006.1](https://github.com/abdullahbodur/horo-engine/issues/1703)
- **Jira**: [HORO-1662](https://horo-engine.atlassian.net/browse/HORO-1662)
- **Related**: [ADR-014](014-sequencer-ownership-clock-authority-and-binding-boundary.md), [ADR-018](018-command-registration-permissions-threading-and-packaged-build-policy.md), [ADR-054](054-extension-and-package-authority-boundary.md), [ADR-117](117-playback-ownership-frame-order-and-determinism.md), [ADR-120](120-cinematic-event-dispatch-and-audio-coupling-boundary.md)
- **Normative documents**: [Cinematic Sequencer Architecture](../architecture/runtime/cinematic-sequencer-architecture.md), [Gameplay Module Boundary](../architecture/extensions/gameplay-module-boundary.md), [MCP Architecture](../architecture/interfaces/mcp-architecture.md), [Editor AI Agent Architecture](../architecture/editor/editor-ai-agent-architecture.md), [Runtime Lifecycle](../architecture/runtime/runtime-lifecycle.md)

## Context

ADR-117 makes scene-authored playback components inert start descriptors and gives a
session `CinematicRuntimeService` ownership of live players. The architecture does
not yet define every caller allowed to submit a start request, the capability and
world authority each caller needs, or whether editor/MCP/remote triggers ship in a
retail executable.

A trigger is security-sensitive because a sequence can request gameplay events,
camera authority, Character control, Audio/VFX work or gameplay pause. Treating every
caller as trusted because it can reach Cinematic Runtime would let a client, script,
MCP connection or remote debug endpoint widen its authority. Conversely, inventing a
cinematic-specific approval database would duplicate host capability, project trust,
MCP approval and debug-command permission infrastructure.

This ADR enumerates admitted sources and makes every source submit the same typed
request to the same owner. It specializes existing authorization mechanisms; it does
not create a new trust or approval system.

## Decision

### 1. All triggers submit one typed application request

No source constructs or starts a `SequencePlayer` directly. Each submits a bounded
`SequenceStartRequest` through an injected `ICinematicPlaybackCapability` into the
session-owned `CinematicRuntimeService`:

```cpp
struct SequenceStartRequest {
    SequenceAssetId sequence;
    SequenceTriggerSource source;
    TriggerPrincipalId principal;
    RuntimeSessionId session;
    SceneRuntimeId scene;
    StableActivationId activation;
    SequencePlaybackSettings settings;
    CapabilityGrantRef capabilityGrant;
    AuthorityEvidenceRef authority;
    CancellationToken cancellation;
};
```

The application adapter validates caller provenance, product/build profile, project/
package trust, capability grant, session/scene scope, gameplay/network authority,
asset/cook revision, requested sequence effects and budgets. Admission then follows
ADR-117's stable activation and immutable batch rules. A source receives a typed
`SequenceStartResult`; possession of an asset ID, component, event name, endpoint or
service pointer is never authority.

Capabilities are narrow and composable: `cinematic.playback.start`,
`cinematic.playback.stop-own`, `cinematic.playback.control-any`,
`cinematic.preview`, and effect-specific grants inferred from the compiled sequence
plan. A start grant cannot implicitly confer gameplay pause, server mutation, remote
administration or control over another principal's player.

### 2. Source policy is explicit

| Trigger source | Required provenance/capability | Authority and packaged policy |
|---|---|---|
| Gameplay script/native behavior | Activated trusted gameplay module/script context plus `cinematic.playback.start` | May start only in its runtime session and world authority. Included in Development/Profile/Shipping/Server when the module and cooked sequence are product content. Client cannot gain server-owned effects. |
| Scene-load auto-play | Validated cooked `SequencePlaybackComponent`/scene activation descriptor | RuntimeScene submits after successful aggregate scene activation. Allowed in packaged content profiles, including Shipping, because it is authored product behavior rather than a remote command. Effect plan still needs all owner capabilities. |
| Gameplay event | Registered typed adapter under ADR-120 plus `cinematic.playback.start` for its destination principal | Enqueued after the source occurrence commits; recursion/nesting/player budgets apply. Allowed in packaged content profiles when the event binding and target sequence are cooked/allowlisted. |
| Editor Play/Preview UI | Active trusted editor project, document/context scope and `cinematic.preview` or local play capability | Editor profile only. Preview is non-authoritative and isolated; PIE play uses PIE world authority and ordinary runtime admission. Direct user action is intent, not blanket future approval. |
| MCP/agent tool | Registered MCP tool, authenticated caller where applicable, trusted project, scoped capability and operation approval | Editor/local development tooling only by default. Mutating start requires the existing visible revision-bound approval unless an existing user/project policy has explicitly pre-authorized that exact bounded tool class. Absent from Retail Shipping. |
| Local debug/CLI adapter | ADR-018 descriptor, matching permission/availability and owner-thread dispatch | Development profiles only unless a separate product-safe local trigger is compiled as ordinary gameplay UI. `AdminCheat` plus world/server authority is required for gameplay-mutating debug playback. No Shipping debug backdoor. |
| Authenticated remote tooling/admin | Existing authenticated remote transport, exact registered tool/command capability, audit policy and target-world authority | Disabled for Retail Shipping clients. Diagnostics is opt-in and bounded. Dedicated Server may admit an explicitly registered `Restricted` server operation with authenticated session and server authority; headless compatibility is required. |

Unlisted sources are denied. Extensions/packages may contribute an adapter only
through their declared application/MCP/command capability descriptors, package trust
approval and selected product profile. They do not register a raw Cinematic callback
or receive a service locator.

### 3. Scene auto-play is authored activation, not implicit trust

Runtime conversion emits an inert start descriptor containing sequence identity,
autoplay condition, stable activation slot, lifetime scope, required/optional policy
and compiled capability summary. RuntimeScene submits it only after the scene
candidate and required assets/services are ready. Failed aggregate scene activation
starts nothing.

The descriptor is admitted against the same owner capabilities as every other
request. Auto-play cannot bypass missing gameplay/camera/Audio authority merely
because it came from cooked content. Required denial fails or disables the scene
candidate according to declared scene policy; optional denial produces a visible
typed diagnostic. Scene replacement/cancellation generation-fences pending starts.

### 4. Gameplay scripts and events cannot widen authority

Gameplay modules receive `ICinematicPlaybackCapability` in their validated runtime
context only when declared and granted. Scripts use the same typed binding; string
lookup of a global Cinematic service is forbidden. The capability scopes requests to
the caller's session, world role, allowed sequence set/effect classes and owned-player
control policy.

A client-side caller may start cosmetic/local presentation content explicitly
qualified for that client view, but it cannot acquire server Character, gameplay,
Physics or pause authority. A server-authoritative sequence begins from server-owned
gameplay or an authenticated server operation and replicates semantic/resulting state
through normal network contracts.

ADR-120 gameplay events may request a sequence only after the occurrence's source
tick commits. The request creates a distinct stable activation identity derived from
the occurrence ID and authored trigger slot. Loops, recursion and sub-sequences count
against ADR-117's shared capacities; an event cycle cannot manufacture unbounded
players or bypass preflight.

### 5. Tooling reuses existing authorization and approval

MCP, agent, CLI, debug-console and remote adapters are presentation/protocol layers
over the same application capability. Their existing infrastructure remains final:

- MCP owns tool schema, authenticated connection, context scope, rate limits and
  cancellation under MCP Architecture;
- editor-agent writes retain visible revision-bound proposal/approval under Editor AI
  Agent Architecture;
- debug/CLI/remote commands use ADR-018 `CommandPermission`,
  `CommandAvailability`, thread policy, server-authority check and audit/redaction;
- package/extension adapters require manifest-declared capability and the exact
  publisher/package/artifact trust approval under ADR-054; and
- the application capability registry owns final operation admission and result.

Authentication proves caller identity, not authorization. Project trust proves the
project/package may load, not that every mutation is approved. User approval is
bound to the exact tool/request, document/session revision and visible effect scope;
it is not a reusable bearer token. A project setting may streamline prompts only via
the existing policy engine and cannot enable a descriptor absent from the build.

There is no `cinematic_approvals.json`, global `trustedRemote` boolean, approval in
sequence assets, or special bypass for localhost/agent-generated calls.

### 6. Build availability and runtime permission are independent

Build composition first determines whether an adapter/descriptor exists. Runtime
authorization only evaluates descriptors present in that product profile:

| Product profile | Cinematic content triggers | Tooling triggers |
|---|---|---|
| Editor | Scene, gameplay, PIE and isolated preview | Local editor UI/MCP/agent/debug tools subject to trust, capability and approval |
| Game Development | Scene and gameplay | Local debug/CLI; loopback only where existing policy permits; mutating requests need declared permission/authority |
| Game Profile | Scene and gameplay | Read-only diagnostics only; no gameplay-mutating cinematic debug trigger |
| Diagnostics | Qualified scene/gameplay if product includes them | Explicit authenticated diagnostic adapter only; no implicit cheat capability |
| Game Shipping/Retail | Cooked scene/gameplay/product UI triggers | MCP, agent, debug cinematic start and remote triggers absent; runtime flags cannot enable them |
| Dedicated Server | Headless-compatible scene/gameplay/server triggers | Explicit authenticated `Restricted` server adapter only, with server authority, audit and product registration |

A product-facing menu/button that starts a cinematic in Shipping is ordinary trusted
gameplay UI calling a granted gameplay use case, not a compiled debug/MCP allowlist
exception. It receives only the capability designed for that product flow.

### 7. Admission is atomic and side-effect free on denial

Before acquiring any player/domain lease, admission validates the complete request
and compiled effect plan. Denial returns one stable result and creates no player,
cursor, event occurrence, pause token, camera/Character lease, Audio/VFX request,
operation or partial diagnostic subscription.

Typed outcomes include `TriggerSourceUnsupported`, `TriggerAdapterUnavailable`,
`ProductProfileDenied`, `ProjectTrustDenied`, `CapabilityDenied`,
`ApprovalRequired`, `ApprovalDenied`, `ApprovalStale`, `WrongSession`,
`WorldAuthorityDenied`, `SequenceNotAllowed`, `SequenceAssetUnavailable`,
`EffectCapabilityDenied`, `ActivationConflict`, `CapacityExceeded` and
`HeadlessIncompatible`.

Failures preserve the boundary that rejected the request. They are not collapsed to
`PlayFailed`, and permission-sensitive details are redacted according to the existing
security/observability policy. Unauthorized callers cannot probe unavailable sequence
names, capabilities or world state through different timing/error detail.

### 8. Start identity, ownership and cancellation are source-scoped

Each admitted request receives a stable `SequencePlayerHandle` scoped to caller,
session and player generation. `stop-own` can stop only players created by that
principal/owned activation slot. `control-any` is a separate high-privilege grant and
still requires target-world authority. Dropping a handle does not destroy the service-
owned player under ADR-117.

Caller disconnect, script/module unload, scene replacement, editor preview close,
approval revocation or remote session expiry closes new admission and follows the
request's declared lifetime policy. Scene-owned autoplay stops with its scene scope;
application-owned gameplay sequences may survive component removal if their compiled
lifetime permits it. Late approvals, asset loads or start completions carry all
principal/session/scene/request generations and cannot revive a revoked request.

### 9. Observability is bounded and auditable

Every attempt records source kind, safe principal identity, product profile, target
session/world, sequence/activation identity where disclosure is permitted, requested
capability class, decision, denial boundary, audit correlation and latency. Sensitive
tool arguments, tokens and private content are redacted. High-cardinality raw remote
identity and asset paths are excluded from metric labels.

Gameplay-authored successful starts use ordinary bounded diagnostics. MCP, remote,
Restricted/AdminCheat and repeated denied attempts emit security audit records through
existing HostObservability. Logging or EngineDataBus delivery never performs the
start or becomes approval evidence.

### 10. Qualification covers every source/profile pairing

Required implementation evidence includes:

- gameplay script/native module with granted/missing/revoked capability, module
  reload and client/server authority combinations;
- scene autoplay after successful/failed activation, required/optional owner
  capability denial, scene replacement and duplicate activation fencing;
- ADR-120 gameplay-event trigger commit/failure/loop identity, recursion cycle and
  shared player/nesting budget exhaustion;
- editor preview versus PIE isolation, direct UI intent and revision-stale preview;
- MCP/agent authenticated/unauthenticated, trusted/untrusted, approval
  required/granted/denied/stale and disconnect/cancellation paths;
- local debug/CLI and remote descriptor availability, permission, server authority,
  rate/audit policy and owner-thread dispatch;
- build inspection proving Retail Shipping omits MCP/agent/debug/remote start
  descriptors and cannot enable them with runtime flags;
- Dedicated Server headless-compatible success and visual/unauthorized denial; and
- every denial producing zero player/domain effects with stable typed/redacted result.

## Consequences

### Positive

- All sources reach one admission boundary and cannot construct players directly.
- Shipping product behavior remains available without shipping debug/MCP backdoors.
- Tooling reuses established trust, permission and approval infrastructure.
- Denials identify the failed boundary and guarantee zero partial effects.

### Costs

- Gameplay, scene conversion, events and tooling need narrow adapters over the common
  capability rather than direct service access.
- Build-profile descriptor manifests and effect-capability summaries require
  qualification across editor, client and server compositions.
- Principal/session ownership and late approval/load results need generation fencing.

## Rejected Alternatives

### Expose CinematicRuntimeService as a global service

Rejected because reachability would become authority and callers could bypass product,
world, effect and capacity admission.

### Treat cooked scene autoplay as pre-approved for every effect

Rejected because authored content still cannot grant unavailable gameplay pause,
camera, Character, Audio or network authority. It uses the common admission plan.

### Allow localhost or authenticated MCP to start any sequence

Rejected because authentication/trust identify a caller/project but do not grant
mutation capability, world authority or user approval. Existing MCP/agent policy
remains mandatory.

### Ship remote cinematic debug triggers behind a runtime flag

Rejected because Retail Shipping must omit the descriptors/handlers. A flag cannot
restore code that the product profile deliberately excludes.

### Store cinematic-specific approvals in project or sequence files

Rejected because project content cannot grant its own trust or user authorization.
The existing host policy owns revision-bound approval and audit.

### Let event-triggered sequences start reentrantly

Rejected because the current occurrence batch/player registry could mutate during
dispatch and cycles could bypass budgets. ADR-120 submits a later owner-boundary start
request with stable occurrence-derived identity.
