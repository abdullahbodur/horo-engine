# ADR-162: Mixed-Reality Ownership, Privacy and Capability Tier

- **Status**: Proposed
- **Date**: 2026-09-02
- **Supersedes**: None
- **Scope**: Passthrough, spatial anchors, planes/meshes, environment hit tests, light estimation, camera/gaze/geometry/location-derived data classification, consent/permission, derived consumers, post-1.0 capability profiles, lifecycle, migration and validation
- **Issue**: [XRA-006.1](https://github.com/abdullahbodur/horo-engine/issues/2157)
- **Jira**: [HORO-2111](https://horo-engine.atlassian.net/browse/HORO-2111)
- **Related**: [ADR-026](026-large-world-precision-and-floating-origin-strategy.md), [ADR-027](027-renderer-resource-identity-and-descriptors.md), [ADR-113](113-local-storage-user-profile-and-slot-ownership.md), [ADR-135](135-platform-identity-session-generation-privacy-and-consent.md), [ADR-157](157-xr-ownership-runtime-composition-and-capability-tier.md), [ADR-159](159-xr-action-tracking-and-input-projection-ownership.md), [ADR-160](160-xr-rendering-openxr-compositor-and-renderer-ownership.md), [ADR-161](161-xr-interaction-runtime-ui-locomotion-and-accessibility-ownership.md)
- **Normative documents**: [XR Architecture](../architecture/runtime/vr-ar-architecture.md), [Application Security](../architecture/security/application-security.md), [Rendering Architecture](../architecture/runtime/rendering-architecture.md), [Platform Services Architecture](../architecture/runtime/platform-services-architecture.md)

## Context

ADR-157 places passthrough, anchors, scene understanding and advanced tracking outside
the 1.0 XR profiles. Existing text describes them as optional, permission-sensitive
capabilities but does not assign their native, Horo, consumer, storage and privacy
owners or classify raw camera, gaze, environment geometry and localization data.

Without that decision, Renderer could request camera frames because it draws
passthrough, Physics could ingest a runtime mesh as authoritative collision, Scene could
serialize observed planes, or runtime permission could be mistaken for product consent.
Anchor identifiers could become portable world identity despite provider-specific scope,
and diagnostics could retain a room layout or gaze history as ordinary data.

This ADR keeps mixed reality post-1.0, preserves stable optional seams in XRApi/XRRuntime
and defines owners, data classes, admission, revocation and lifecycle.

## Decision

### 1. Mixed reality is a family of independent post-1.0 capabilities

There is no `mixedRealityEnabled` boolean or ordered MR tier. Initial families are:

- `MRPassthroughPost1`: runtime-composited passthrough without public raw camera access;
- `MRSpatialAnchorsPost1`: live anchors and separately admitted persistence/restore;
- `MRSceneUnderstandingPost1`: bounded planes/meshes and environment hit tests;
- `MRLightEstimationPost1`: bounded derived lighting observations; and
- privacy-sensitive tracking providers such as eye gaze under ADR-159.

Each family has independent provider dependencies, purpose, permission/consent, limits,
lifetime, fallback and qualification. Supporting one proves nothing about another.
`XRProjection1_0` and `XRTrackedInteraction1_0` neither require nor emulate them and
remain valid when every MR family is absent or disabled.

XRApi preserves backend-neutral capability IDs/states/generations, opaque handles,
bounded snapshot/result envelopes and narrow ports. It exposes no raw camera buffers,
native anchor/plane handles, provider UUIDs, vendor enums or unbounded geometry.

### 2. Every MR responsibility has one owner

| Responsibility | Sole owner | Deliberate non-owner |
|---|---|---|
| Product purpose, required/optional families and fallback | Application/product composition | Runtime discovery and Renderer |
| Consent, restrictions, retention/export and access-policy revision | Application Security/privacy owner | XR backend and UI |
| OS/runtime permission primitive and current state | Platform/XROpenXR native adapter | Product consent store |
| Native passthrough/anchor/plane/mesh/hit/light handles and calls | XROpenXR/provider adapter | XRApi, Renderer and Scene |
| Horo provider/session/spatial generations and bounded snapshots | XRRuntime MR coordinator | Native callbacks and consumers |
| Passthrough intent and native compositor encoding | XRRuntime plan / XROpenXR encoding | Renderer post-processing |
| Virtual pixels, depth/occlusion and admitted light consumption | Renderer | Camera permission and environment truth |
| Anchor Horo identity, localization and native session mapping | XRRuntime spatial coordinator | Scene Transform and storage paths |
| Durable anchor envelope when admitted | Save/local storage owner | Native callback and cloud services |
| Observed plane/mesh snapshot | XRRuntime scene-understanding provider | Physics, Navigation and authored Scene |
| Derived collision/navigation/gameplay realization | Explicit adapter and destination owner | Provider snapshot itself |
| Environment hit-test evidence | XRRuntime/provider query owner | Runtime UI/gameplay authority |
| Derived light estimate/provenance | XRRuntime provider; Renderer consumes | Authored lights/exposure |
| Diagnostics/capture and support claim | Observability/release owner under separate purpose | Ordinary logs/metrics |
| Account/cloud/presence transport | Platform Services | MR session, consent and spatial state |

Native callbacks publish bounded provider candidates only. They never mutate Renderer,
Scene, Physics, Navigation, UI, gameplay or durable storage.

### 3. Privacy classification precedes collection

| Data | Minimum classification | Default handling |
|---|---|---|
| Raw camera/depth frames or camera texture access | Restricted raw sensor data | No public exposure, log, capture, persistence or upload |
| Eye gaze and continuous eye-derived behavior | Sensitive biometric/behavioral data | Purpose-scoped derived output under ADR-159 |
| Continuous head/hand/body pose history | Sensitive behavioral/spatial data | Ephemeral bounded snapshots only |
| Room planes/meshes/labels and spatial maps | Sensitive environment geometry | Ephemeral, minimized and consumer-scoped |
| Anchor provider token/localization map reference | Sensitive location-derived persistent data | Opaque protected storage only when admitted |
| Anchor pose, hit and light estimate | Sensitive derived spatial data | Short-lived generation-scoped evidence |
| Passthrough enablement/layer state | Privacy-sensitive capability state | Bounded diagnostics; no imagery |

Products may classify data more strictly. Derived values do not become non-sensitive
merely because raw frames are hidden, and combining observations may raise classification.

General logs, metrics dimensions, crash dumps, replay, saves, editor history, AI context,
analytics and support bundles exclude raw sensors, continuous pose/gaze, environment
geometry, anchor tokens and precise maps. Dedicated capture requires a separate visible
purpose, finite duration/bounds, protection, retention/deletion/export policy and honest
partial state.

### 4. Permission, consent, capability and purpose are independent gates

Admission intersects product policy, installed provider, runtime/system capability,
OS/runtime permission, purpose-specific consent, region/legal/parental restriction,
destination consumer and current access-policy revision.

States distinguish `NotInstalled`, `Unsupported`, `DisabledByPolicy`, `NotRequested`,
`PermissionRequired`, `ConsentRequired`, `Granted`, `Denied`, `Revoked`,
`TemporarilyUnavailable`, `Lost` and `Restricted`. Permission does not create consent;
UI does not grant itself; discovery does not start collection.

Each consumer obtains a least-privilege generation for one purpose/projection. Renderer
passthrough grants no gameplay camera/geometry access. Plane placement grants no Physics
ingestion or diagnostics. Eye-foveation grants no UI gaze or analytics.

Revocation closes publication, cancels work, invalidates leases, neutralizes dependent
Input/interaction, removes layers/derived candidates and schedules allowed retained data
for policy-driven deletion. It is not empty success or permission for a more invasive
fallback.

### 5. Passthrough is compositor capability, not camera ownership

The passthrough seam is a backend-neutral layer intent with mode, blend/alpha, space,
ordering band, policy-permitted style, access revisions and session/layer generations.
XROpenXR owns native objects and encodes the admitted layer under ADR-160. Renderer
supplies virtual content and declared depth/alpha but receives no raw camera or permission
handle.

Passthrough states include `Unavailable`, `PermissionRequired`, `ConsentRequired`,
`Ready`, `Running`, `Paused`, `Revoked` and `Lost`. The product distinguishes opaque VR,
admitted passthrough and unavailable MR; black/transparent output is not success.

Optional failure may use only its captured opaque fallback. A product requiring MR fails
admission/stops safely. Post-processing cannot enable passthrough or sample raw camera.

### 6. Anchors are localization capabilities, not Scene identity

`XRSpatialAnchorId` is a Horo owner/generation-safe live identity mapped privately to a
native anchor/space under exact provider/runtime/session/origin generations. Pose
snapshots record localization state, provenance, sample time and explicit `Locating`,
`Localized`, `Limited`, `Lost`, `Expired` or `Revoked`. Last-known pose is not current.

Scene entities reference a typed anchor binding, not a native handle/token. A spatial
adapter resolves localized pose into a candidate world transform; Scene/gameplay owns
application. Native callbacks never write Transform.

Session anchors do not imply persistence. Durable restore is a separate capability with
provider/version, scope, opaque protected reference, expiry, access revision and failure
semantics. Provider tokens are not project paths, cloud IDs or cross-runtime identity.
Shared/cloud anchors need a future trust/account/sharing decision; Platform Services
cannot infer one.

### 7. Scene understanding is observed evidence, not world truth

Providers publish bounded immutable plane/mesh snapshots with provider/spatial/session
generations, stable-within-generation Horo observation IDs, pose/classification
provenance, geometry revision, validity/time and finite vertex/index bounds. Removed,
merged, split, changed and unknown observations are explicit.

Snapshots do not mutate Scene, Physics, Navigation, Terrain or world streaming. An
explicit adapter may form purpose-specific derived candidates. The destination validates
limits, origin, simplification, collision/filter/material policy and safe-point
transaction before realization. Derived resources retain provenance and retire on loss.

Observed geometry is not authored project content without a visible import transaction
and privacy/export policy. Runtime labels are untrusted evidence. Missing geometry is not
empty authoritative world state.

### 8. Hit tests and light estimates are derived observations

Environment hit-test requests declare purpose, ray/shape, spatial generation, target
classes, distance/count bound and deadline. Results contain ordered bounded hits,
observation/anchor reference, pose/normal, provenance, sample revision and typed
completion. A hit cannot place an entity, teleport Character or invoke UI/editor; domain
owners revalidate under ADR-161.

Light estimates carry declared intensity/color/direction/environment representation,
units, validity, provider/sample/spatial generation and provenance. Renderer may consume
an admitted estimate as one input but cannot rewrite authored lights, exposure or color
authority. Loss uses only a declared neutral/fallback plan, never a stale frozen value.

### 9. Lifecycle is purpose- and generation-fenced

```text
NotAdmitted
  -> Permission/ConsentPending
  -> Admitted
  -> ProviderStarting
  -> Ready / Running
  -> Paused / Limited / Revoked / Lost
  -> Stopping
  -> Inactive
```

Validation is inert. Startup occurs only after complete policy admission. Native
resources/snapshots/layers prepare privately and publish atomically; failure rolls back
without partial capability or consumer projection.

Session, provider, access policy, origin, observation, anchor, layer and query revisions
fence values. Session/origin change, replacement, revocation and suspension invalidate
affected generations. Matching labels/poses do not revalidate them.

Shutdown closes requests/publication, removes layers, disconnects consumers, joins
queries/callbacks, releases snapshot/render/derived leases, retires destination resources,
destroys native MR handles and then releases XR dependencies. Durable-reference deletion
follows storage policy even after the native session ends.

### 10. Migration and contract coverage are explicit

There is no production MR implementation. The 1.0 foundation keeps only optional IDs,
states, generations and bounded envelopes, returning typed unavailable results; it must
not publish speculative raw-camera or native-anchor APIs.

Required automated coverage includes:

- 1.0 profiles admitting with every MR family absent and no provider work;
- independent family admission/fallback with no aggregate MR boolean;
- data-class/purpose/access validation and least-privilege consumer isolation;
- permission versus consent, denial/revocation/deletion and no empty success;
- passthrough lifecycle/composition without raw camera exposure;
- anchor localization/loss, session versus durable scope and token isolation;
- bounded plane/mesh replacement and no direct domain mutation;
- hit-test bounds/staleness and inability to invoke UI/gameplay/editor;
- light-estimate units/provenance/loss without authored-light/exposure mutation;
- redaction from logs/metrics/crash/replay/save/AI/support output; and
- failure/replacement/shutdown with no callback, query, layer, anchor, snapshot, derived
  resource or access lease surviving its owner.

Deterministic providers validate order/privacy faults but do not qualify physical camera,
localization, geometry, passthrough safety or lighting accuracy.

## Consequences

### Positive

- MR evolves after 1.0 through stable seams without weakening baseline XR.
- Native providers, Horo snapshots, domain realizations and privacy policy have separate
  owners.
- Passthrough creates no raw camera API; observed geometry is not world truth.
- Consumers receive purpose-limited projections and explicit revocation.

### Negative

- Each family needs independent permission, consent, limits and qualification.
- Durable anchors need protected opaque storage and cannot promise portability.
- Domain use of observed geometry needs explicit adapters and safe-point realization.
- Raw capture remains unavailable without dedicated privacy-controlled flows.

## Rejected Alternatives

### Include mixed reality in the 1.0 profiles

Rejected because sensitive permission, privacy, persistence and geometry lifecycles are
independent of baseline projection.

### Expose raw camera frames through XRApi

Rejected because passthrough composition does not require public camera ownership and a
raw stream expands privacy, security, bandwidth and lifetime risk.

### Treat runtime permission as consent

Rejected because OS/runtime access, product purpose, consent and restrictions have
different owners and revisions.

### Import runtime meshes directly into Scene or Physics

Rejected because observed geometry is incomplete, changing and sensitive evidence that
requires destination validation and transaction.

### Use native anchor IDs as persistent world identity

Rejected because provider tokens are private, scope/expiry/version dependent and may
carry location-derived information.

### Let Renderer or Platform Services own MR

Rejected because Renderer consumes admitted pixels/lighting while Platform Services owns
account/cloud transport; neither owns sensors, spatial lifecycle or consent.
