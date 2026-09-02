# ADR-172: Immersive Agent Ownership, Authoring Mode and Risk

- **Status**: Proposed
- **Date**: 2026-09-02
- **Supersedes**: None
- **Scope**: Immersive editor-agent audience, mode admission, multimodal evidence, proposal approval, mutation authority, cross-system ownership, privacy and risk
- **Issue**: [AIA-004.1](https://github.com/abdullahbodur/horo-engine/issues/2288)
- **Jira**: [HORO-2227](https://horo-engine.atlassian.net/browse/HORO-2227)
- **Parent**: [AIA-004](https://github.com/abdullahbodur/horo-engine/issues/2287)
- **Related**: [ADR-018](018-command-registration-permissions-threading-and-packaged-build-policy.md), [ADR-021](021-gameplay-ai-ownership-scheduling-and-behavior-boundary.md), [ADR-070](070-capture-and-voice-io-ownership.md), [ADR-157](157-xr-ownership-runtime-composition-and-capability-tier.md), [ADR-159](159-xr-action-tracking-and-input-projection-ownership.md), [ADR-161](161-xr-interaction-runtime-ui-locomotion-and-accessibility-ownership.md)
- **Normative documents**: [Editor AI Agent Architecture](../architecture/editor/editor-ai-agent-architecture.md), [Editor Document Model](../architecture/editor/editor-document-model.md), [MCP Architecture](../architecture/interfaces/mcp-architecture.md), [XR Architecture](../architecture/runtime/vr-ar-architecture.md)

## Context

Immersive authoring combines voice, gaze, pointing, tracked poses, controller actions,
physical interaction, editor selection, scene state and agent tool calls. Those signals
have different owners, reliability, privacy classes and meanings. Treating their temporal
proximity as authority would let a glance, spoken discussion, grab, collision or tracking
error approve a destructive edit.

The agent also observes three distinct worlds: persistent Edit documents, disposable
Simulate clones and Play runtime state. A tool that does not bind its proposal to the
correct mode, project, scene, document revision and XR session can apply an answer to a
different world than the user reviewed. Remote providers and MCP transports add prompt-
injection, data disclosure, confused-deputy and delayed-result risks.

The architecture already assigns microphone capture to Audio, transcription to a speech
service, XR state to XR, mutations to editor commands and tool admission to MCP. This ADR
fixes the immersive product boundary and the authority protocol that connects them.

## Decision

### 1. Immersive AI is developer authoring tooling

The initial capability is an Editor AI surface for a locally present, authenticated
developer in a trusted project. It is absent from packaged games and dedicated servers.
It is not gameplay AI, an NPC/dialogue system, a player UGC runtime, a remote-administration
channel or network authority.

An editor host may compose the capability only when the exact XR, Audio/speech, agent
provider, MCP tool, document and security policies pass preflight. Missing components
produce a typed unavailable/degraded result; they never silently widen authority or fall
back to player/runtime mutation.

### 2. Authoring modes have different admission

| Mode | Admitted reads and operations | Forbidden authority |
|---|---|---|
| Edit | Bounded context queries, transcription, planning, ghost preview, explicit approval and one editor transaction | Direct scene storage, asset or file mutation outside the transaction owner |
| Simulate | Bounded queries/captures against the isolated simulation generation; proposal and preview against a named authoring revision | Mutation of the simulation world or authoring document while simulation is active |
| Play / PIE | Bounded diagnostics and capture when product policy admits them | Runtime/gameplay mutation, authoring mutation, approval carry-over or apply-to-runtime |
| Packaged game/server | None in the initial capability | Agent composition, editor tools and authoring credentials |

A proposal prepared during Simulate or Play can be retained only as a non-authoritative
candidate. Applying it requires return to Edit, fresh context and permission checks, exact
document-revision revalidation, regenerated preview when relevant and new approval. Mode
changes invalidate outstanding approval tokens.

### 3. Evidence, request, proposal and approval are distinct

The multimodal pipeline classifies data before it reaches a mutation boundary:

```text
bounded voice / transcript ----+
XR pose, gaze, ray, action -----+--> timestamped evidence envelope
Physics query/contact ----------+                |
Editor selection/revision ------+                v
                                         interpreted request
                                                |
                                                v
                                  bounded proposal + ghost preview
                                                |
                                  explicit proposal-bound approval
                                                |
                                                v
                                    editor command transaction
```

Voice may express a request but never approves a proposal. Gaze, pointing, dwell, head
motion, proximity, selection, grab, contact, throw, collision, trigger events, tracking
recovery and their combinations are context only. Confidence scores and model assertions
cannot promote evidence to approval.

Approval is a distinct non-voice UI/controller/accessibility action presented as an
unambiguous review control. It binds the authenticated local user, proposal digest,
human-readable impact summary, project/document identity and revision, target object
generations, mode, permission snapshot, tool plan and expiry. It is single-use. Any bound
change, timeout, cancellation, focus loss, XR session replacement or tracking invalidation
returns to review or cancels the proposal.

### 4. Owners and deliberate non-owners are fixed

| Concern | Owner | Deliberate non-owner |
|---|---|---|
| Capture device, PCM timestamps, buffer/backpressure lifecycle | Audio under ADR-070 | AIA and XR do not open the microphone |
| Speech model/provider, partial/final transcript and confidence | Speech/application service | Audio does not interpret intent; transcript does not approve |
| XR session, pose/gaze/ray/action snapshots and validity | XR | AIA does not poll OpenXR or reinterpret tracking validity |
| Spatial queries, contacts and simulated body state | Physics and the target world | AIA does not write solver state; contact is not intent |
| Gameplay input, interaction and locomotion meaning | Input/Runtime UI/gameplay owners | AIA does not convert ordinary player actions into tool authority |
| Multimodal envelope, request interpretation, proposal and preview orchestration | Editor AIA | AIA does not commit documents or runtime state |
| Tool schema, transport, caller/session checks and capability admission | MCP | MCP does not own business rules or documents |
| Validation, undo/redo, dirty state, atomic commit/save and conflict handling | Editor document/application command owner | Providers, AIA and MCP handlers do not mutate storage directly |
| Network transport, peers and gameplay authority grants | Networking | AIA sessions and local presence do not grant network authority |
| Consent, provider/data policy, audit retention and redaction | Security plus host/project policy | Provider responses do not choose disclosure or retention |

Cross-owner data is immutable, bounded and generation checked. Native handles, writable
references and owner-private service instances never enter the evidence envelope or model
context.

### 5. Every mutation is a reviewable editor transaction

The model produces typed tool-plan candidates. MCP validates schemas and authorization;
the application resolves them into a complete editor transaction candidate without
publishing state. Ghost previews use transient editor-owned overlays and cannot affect
queries, simulation, save data, networking or runtime authority.

After explicit approval, the editor owner revalidates all bound identities, permissions,
locks, preconditions, cost limits and document revisions at its safe point, then commits
the whole transaction or none of it. Undo/redo uses normal semantic history. Provider or
transport success is not commit success. Direct document/ECS/Physics/file mutation,
generic data-bus commands and speculative authoritative edits are forbidden.

Deletion, broad hierarchy/asset changes, script or executable changes, external process/
network actions, project settings, camera/world transforms with high spatial impact and
irreversible export require a heightened warning and policy-selected additional
confirmation. The agent cannot split a high-risk plan to evade that classification.

### 6. Context and privacy are purpose bounded

Every request displays the admitted context categories and whether inference is local or
remote. Raw microphone data, transcripts, gaze/pose history, hand/body features, room
geometry, camera frames, paths, identities and tool results remain separate privacy
classes. OS permission, XR feature availability or project trust does not authorize cloud
disclosure, retention, training use or a later request.

Context assembly prefers typed scene/document state and bounded spatial queries. It uses
time-windowed samples with source, clock, timestamp, confidence, scene/document revision,
object generation and XR session generation. Stale, hidden, locked, cross-project or
unauthorized targets fail before planning. Raw captures are excluded unless an explicit
purpose and policy admit them.

Audit records contain safe identities, admitted category summaries, proposal digest,
approval/denial, tool/result classes, transaction receipt and provider/policy revisions.
They exclude raw voice, continuous gaze/pose, room/camera content, secrets and unrestricted
payloads. Retention and export are bounded and user-visible.

### 7. Remote and untrusted inputs cannot become authority

Transcripts, scene text, asset metadata, provider output and network content are untrusted
data. They cannot modify the system/tool policy, request hidden context, self-approve,
increase budgets, enable tools, change modes or suppress review. Tool capabilities are
computed by the host from caller identity, local presence, project trust, build profile,
mode, permissions and policy; the model sees only the resulting finite set.

MCP authentication or connection locality alone grants no authority. An immersive local
session cannot mint Networking gameplay authority, and a remote MCP/network participant
cannot impersonate local physical approval. Long-running capture, transcription,
inference, planning and execution are cancellable and generation fenced. Late results
become discarded candidates, not actions.

### 8. Migration, failure and qualification fail closed

Prototype gesture-to-command paths migrate to evidence envelopes; direct agent mutations
migrate to proposal/preview/approval/transaction; mode-agnostic context caches gain exact
world/document/session generations; and raw provider logs migrate to the bounded audit
schema. Old approvals are not imported.

Typed outcomes distinguish unavailable composition, permission/consent denial, invalid or
stale context, unsupported mode, tracking/capture/transcription/provider failure, budget
exhaustion, unauthorized tool, approval expiry/mismatch, transaction conflict and partial
diagnostic evidence. Cancellation and shutdown close admissions and retain owner leases
only until bounded acknowledgement.

Qualification covers duplicate/out-of-order multimodal samples, ambiguous speech, gaze or
gesture near approval controls, tracking loss, mode/document/session replacement, stale
and malicious provider output, permission revocation, cloud-denied/local operation, high-
risk confirmation, transaction conflict/rollback, audit redaction and packaged-build
absence. Tests must prove forbidden mutation, not merely successful happy paths.

## Consequences

- Immersive authoring can combine rich spatial evidence without turning natural behavior
  into authority.
- Simulate and Play are useful observation environments but cannot bypass persistent
  document review or mutate runtime worlds.
- Every accepted change has an attributable proposal, explicit approval and ordinary
  editor transaction receipt.
- The initial capability deliberately excludes player-facing and packaged-runtime agents;
  adding either requires a separate product, security and authority decision.

## Rejected Alternatives

### Treat a voice confirmation or gesture as approval

Rejected because speech recognition is probabilistic, conversation is not a secure
confirmation channel, and natural spatial behavior is ambiguous and accessibility-hostile.

### Let the agent edit the active Simulate or Play world

Rejected because ephemeral/runtime state has different ownership and may diverge from the
document the user believes they are changing.

### Give the provider direct editor or Physics access

Rejected because it bypasses tool admission, owner-thread rules, document history,
generation checks, rollback and auditable authority.

### Enable the same agent in packaged games

Rejected because developer project trust, editor documents, provider credentials and MCP
mutation tools are not a player-facing security or gameplay-authority model.

### Infer approval from several weak signals

Rejected because combining gaze, pointing, speech confidence and contact does not make
any signal intentional, authenticated or bound to the reviewed proposal.
