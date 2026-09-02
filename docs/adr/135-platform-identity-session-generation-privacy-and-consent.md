# ADR-135: Platform Identity, Session Generation, Privacy and Consent

- **Status**: Proposed
- **Date**: 2026-09-02
- **Supersedes**: None
- **Scope**: Platform account/handle/presentation separation, session and access-policy generations, sign-in/out/account switching, opaque subject handles, stale callbacks, service-purpose consent, restricted accounts, privacy, retention and observability
- **Issue**: [PLS-006.1](https://github.com/abdullahbodur/horo-engine/issues/1891)
- **Jira**: [HORO-1847](https://horo-engine.atlassian.net/browse/HORO-1847)
- **Related**: [ADR-002](002-credential-handling.md), [ADR-008](008-error-model-exception-boundary-and-registry.md), [ADR-056](056-external-editor-ui-boundary.md), [ADR-098](098-protocol-session-and-trust-policy.md), [ADR-113](113-local-storage-user-profile-and-slot-ownership.md), [ADR-115](115-cloud-save-authority-revision-and-conflict-policy.md), [ADR-130](130-platform-services-frontend-request-lifetime-timeout-null-and-error-semantics.md), [ADR-131](131-platform-services-closed-sdk-extension-abi-package-and-composition-boundary.md), [ADR-133](133-platform-progression-authority-trust-and-idempotency.md), [ADR-134](134-cloud-blob-transport-revision-precondition-and-offline-ownership.md)
- **Normative documents**: [Platform Services Architecture](../architecture/runtime/platform-services-architecture.md), [Save Game and Persistence](../architecture/runtime/save-game-and-persistence.md), [Application Security](../architecture/security/application-security.md), [Observability Logging](../architecture/observability/observability-logging.md)

## Context

Platform Services operations are scoped to a signed-in platform user, but “user” is
currently represented by a string-bearing handle and a `signedIn` boolean. That model
can conflate a provider's native account locator, a live authentication session, local
save/profile identity, network gameplay identity and user-visible display data.

It also does not define account-switch fencing. A provider callback for Alice may
arrive after Bob becomes current, a queued achievement may replay after sign-out, or a
friends response may publish after consent is revoked. Checking whichever account is
current when the callback arrives would route old work into a new subject.

Privacy and restricted-account rules are currently described as UI/platform concerns.
An operation needs a capability-level purpose/consent decision before admission and a
generation check before every user-scoped commit. UI can present a platform or product
prompt, but hiding a panel is not enforcement.

This ADR defines the identities, session state, access snapshot, consent/restriction
gates, switching transaction and data-minimization rules. It does not create Horo game
accounts, define platform login UI, retain friends history or bypass platform-holder
identity policy.

## Decision

### 1. Five identity domains remain non-interchangeable

| Domain | Owner | Lifetime/use | Forbidden substitution |
|---|---|---|---|
| Provider account subject/locator | Private provider/identity adapter | Provider-specific authentication and durable private binding only | Never gameplay, public API, log, save path or display identity |
| `PlatformSubjectHandle` | Platform frontend identity broker | Non-guessable live capability for one provider/session subject | Never persistent account key, display value or provider-native handle |
| `LocalUserStorageId` + `GameProfileId` | ADR-113 game identity/profile service | Durable product-local storage/profile namespace | Never raw provider account or live authentication proof |
| Gameplay/network player/principal | Product gameplay/network authority | World/session authority and replication | Never inferred from provider login or display name |
| `PlatformUserPresentation` | Consent-scoped presentation projection | Bounded short-lived name/avatar/status fields | Never equality, routing, storage or authorization |

No implicit conversions exist. Linking two domains is an explicit service operation
with captured source/destination identity, consent/product policy, expected revisions
and transactional result. Sign-in does not automatically select/merge a game profile;
loading a save cannot switch platform account; a provider display-name match cannot
link identities.

### 2. Public subject handles are random generation-scoped capabilities

```cpp
struct PlatformSubjectHandle {
    UInt128 nonce;
    ProviderGeneration provider;
    PlatformSessionGeneration session;
};
```

The frontend identity broker allocates `nonce` from the platform cryptographic random
source after successful authenticated subject binding. Zero, reuse and RNG failure
fail binding. The value is equality-only and non-guessable within the threat model; it
is not a UUID supplied by the provider, a hash of account ID/gamertag/email or an
authorization secret.

The handle is valid only while its exact provider and session generations are active.
It is passed as a typed capability to permitted Horo services and cannot be serialized
to project files, saves, offline records, scripts, console history, analytics or logs.
Diagnostic correlation uses a separately generated ephemeral safe correlation ID when
policy allows, never the handle bytes.

Gameplay and ordinary UI cannot enumerate handles or construct them from bytes. A
multi-user host obtains handles only from an immutable active-session projection and
selects one explicitly. A handle grants no service by itself; every operation also
passes the effective service access/consent gate.

### 3. Durable provider-to-local mapping stays private and pseudonymous

The private provider adapter may expose a stable authenticated account subject only to
the identity/profile mapping service through a narrow credential-protected operation.
That service resolves a product/provider-scoped `ProviderAccountBindingId` and maps it
to ADR-113 `LocalUserStorageId` plus explicitly selected `GameProfileId`.

Portable project files, save archives/catalogs, cloud blobs, progression/offline
records and general settings contain no raw provider account ID, gamertag, email,
native handle or credential. The minimal durable mapping store is owned by the identity
service, scoped to product/environment/provider, access controlled and encrypted or
protected by the platform credential/storage facility where available. It stores a
pseudonymous binding ID and protected locator/reference sufficient to re-resolve the
provider subject, not display metadata.

If the provider cannot supply a stable cross-launch subject, it advertises the ADR-113
single-installation/local-user capability and cloud persistence/account linking is
unavailable. Horo never hashes mutable personal/display data as a fallback. Cross-
provider account linking requires a separate product account authority and explicit
verified transaction; equal names prove nothing.

### 4. Every active binding has an immutable session snapshot

```cpp
enum class PlatformSessionPhase : std::uint8_t {
    NoSubject,
    Authenticating,
    Active,
    Closing,
    Failed,
};

struct PlatformSessionSnapshot {
    PlatformSessionPhase phase;
    PlatformSessionGeneration generation;
    std::optional<PlatformSubjectHandle> subject;
    PlatformAccessPolicyRevision accessRevision;
    PlatformSessionCapabilities capabilities;
    NormalizedSessionReason reason;
};
```

There is no independent `signedIn` boolean or nullable user object that can contradict
phase/capability. `Active` has exactly one valid subject and immutable effective access
snapshot. `NoSubject`, `Authenticating`, `Closing` and `Failed` expose no subject for
new admission. A restricted account may still be `Active` while individual service/
operation capabilities are unavailable with typed reasons.

`PlatformSessionGeneration` is nonzero and increases whenever a provider subject is
first published, signed out, switched, invalidated, rebound or provider generation is
replaced. It never wraps/reuses within a process; exhaustion fails closed. Account
refresh that preserves the exact authenticated subject may publish a new access
revision without changing session generation. Any uncertainty that the subject is the
same closes and rebinds with a new session generation.

Snapshots publish on the frontend owner lane. Provider SDK callbacks enqueue bounded
evidence; they never mutate session state or invoke gameplay/UI directly. Observers
receive immutable deferred projections after commit and cannot veto or extend the old
session lifetime.

### 5. Every user-scoped operation captures and revalidates generations

Admission captures at least:

- `PlatformSubjectHandle` and `PlatformSessionGeneration`;
- provider/frontend generation;
- effective `PlatformAccessPolicyRevision` and operation access decision;
- product/profile/authority generation required by its semantic owner; and
- request/durable-intent identity under ADR-130, ADR-133 or ADR-134.

The frontend validates them before provider submit and again before every user-scoped
state/result/cache/event/offline/journal commit. The consuming coordinator revalidates
its own profile/authority generation at its commit boundary. Checking only “currently
signed in” or comparing display/native IDs is forbidden.

A mismatch is `platform.session.stale`. Provider work may finish privately for safe
retirement, but cannot publish into the active subject, update a new user's cache,
replay durable intent, mark a cloud generation synced or dispatch a user observer.
Late evidence retains the old request's terminal/retirement rules and never targets the
new generation.

Durable intent carries a pseudonymous subject-binding partition and captured policy/
session requirements, not a live handle or raw provider ID. Replay first establishes a
new active session for the same protected binding, then obtains a new handle and
explicitly reauthorizes the intent. It cannot reuse old handle bytes or retarget a new
account merely because that account is current.

### 6. Sign-in and account switch publish transactionally

Sign-in/authentication prepares a detached candidate:

```text
provider authentication evidence
  -> private stable-subject resolution
  -> product/provider binding lookup
  -> platform restrictions and product-purpose consent resolution
  -> capability/limit/access snapshot validation
  -> subject handle allocation
  -> atomic Active session publication
```

Failure destroys candidate credentials/handles and publishes no partial subject or
capability. Authentication UI completion is evidence, not publication authority.

Account switch is close-then-bind:

1. close old admission and atomically publish `Closing` with old generation invalid;
2. stop callbacks/observers, cache publication and durable replay for the old subject;
3. request bounded cancellation and retire/preserve request/coordinator state under
   each service's policy;
4. revoke subject handle and short-lived credentials after native callback safety;
5. prepare and validate the new subject/access/profile binding independently; and
6. publish a new `Active` snapshot/generation or explicit `NoSubject`/`Failed`.

There is never an active snapshot containing old profile state and new provider
identity. If new binding fails, Horo does not silently reopen or merge the old account;
rebind is a fresh explicit transaction. Multiple platform users, when supported, have
separate sessions/handles and request partitions rather than a mutable global current-
user pointer.

### 7. Sign-out closes authority before native teardown

Sign-out, provider-forced logout, credential revocation and account removal first close
frontend admission and advance/invalidate session generation. Service coordinators
stop scheduling/replay and snapshot which durable intents remain partitioned for a
future verified same binding. Only then does the provider receive cancellation/logout
and release native identity/credential state after callback epochs retire.

Already committed provider effects remain actual effects; sign-out does not promise
rollback. Admitted requests follow ADR-130 terminal rules but stale results cannot
publish user data into a later session. Cloud/progression ambiguity remains owned by
its old partition and may reconcile only after verified same-binding reauthorization.

A shutdown deadline does not authorize force-freeing provider code, identity cookies,
sinks or credentials still referenced by possible callbacks. The module/credential
lease is retained and a typed shutdown failure is reported under ADR-131.

### 8. Consent and restrictions are typed capability policy

Every operation descriptor declares a finite Horo purpose and data-class set, for
example `CloudSaveSync`, `AchievementProjection`, `LeaderboardRead`, `FriendsRead` or
`PresencePublish`. Product policy declares whether each purpose is included/required,
its minimum age/region/parental requirements, retention class and whether product
consent is needed in addition to provider/OS authorization.

The effective access decision is the intersection of:

- provider/platform capability and authenticated subject state;
- platform/parental/child/restricted-account policy;
- region/legal/product-profile availability;
- current provider/OS permission where applicable; and
- a purpose/version/data-class-specific product consent receipt when required.

```cpp
enum class PlatformAccessState : std::uint8_t {
    Granted,
    ConsentRequired,
    Denied,
    Restricted,
    Revoked,
    Unavailable,
};

struct PlatformServiceAccess {
    PlatformServiceOperation operation;
    PlatformAccessState state;
    PlatformAccessReason reason;
    PlatformPurposeId purpose;
    PlatformDataClassMask dataClasses;
    ConsentPolicyVersion consentVersion;
};
```

Only `Granted` permits admission. All other states have one typed reason and no usable
provider binding for that operation. The access snapshot is the sole truth; UI
visibility, a remembered checkbox, nullable service pointer, provider overlay success
or generic `signedIn` state cannot bypass it.

### 9. Consent UI records evidence but cannot grant itself

Consent is informed, purpose-specific, versioned and revocable. The product consent
authority owns receipts containing subject-binding partition, purpose/data classes,
policy/text version, decision, source, time/expiry where required and revision—never a
credential or unrestricted personal data. A broad “online features” acceptance cannot
silently authorize friends, presence publication and analytics as one unrelated grant.

UI/overlay presents localized disclosure and submits a typed consent decision with an
expected access revision. The authority validates the exact policy/purpose and commits
the receipt; UI then observes a new access snapshot. Closing/dismissing a prompt is not
grant. Repeated denial cannot trigger an unbounded prompt loop; product/platform policy
controls when another request may be offered.

Provider/OS consent remains provider-owned evidence translated by the adapter. Horo
does not fake it with a product checkbox. Conversely, provider permission does not
replace separately required product consent. A restricted/parental account cannot be
overridden by local UI, config, debug command or a stale prior receipt.

Consent or restriction change increments `PlatformAccessPolicyRevision`, closes new
admission immediately and invalidates in-flight publication for affected operations.
Unrelated service operations may remain active only if their captured decision and
data classes are unchanged under the new snapshot. Revocation triggers bounded service-
owned cache/retention cleanup and observer withdrawal; it does not claim a provider
already committed effect was rolled back.

### 10. Personal/display data is bounded, untrusted presentation

`PlatformUserPresentation` is a separate opt-in projection with bounded UTF-8 display
name, optional provider-approved avatar/art token and presence fields plus source/
freshness/access revision. It is untrusted text/data: rendering escapes markup,
validates UTF-8/length/image source and never executes links/content.

Friends/social results expose ephemeral `PlatformSocialSubjectHandle` values scoped to
the requesting session/access revision, not raw account IDs. They cannot be used as
the local player's `PlatformSubjectHandle`, stored as gameplay identity or compared
across provider/session generations. A product account service may perform explicit
linking through its own trusted flow; visual name equality is irrelevant.

By default friends lists, presence, avatars and display strings are held only in
bounded memory until response/session/consent retention expires. Durable caching,
analytics/export or user-generated presence text requires a separately declared
purpose, schema, retention/deletion policy and consent. “The provider returned it” is
not blanket permission to persist or log it.

### 11. Logs, persistence and tools expose safe projections only

General logs, metrics, crash bundles, traces, save/cloud/progression journals, project
files, scripts, CLI/MCP history and ordinary tool results exclude:

- raw/native platform account/user/session/request identifiers;
- `PlatformSubjectHandle` and social subject handle bytes;
- gamertag/display name, email, avatar payload/URL, friends graph and presence free
  text;
- credentials, consent-provider tokens or private binding-store locators; and
- unrestricted native restriction/error payloads.

Allowed evidence is bounded service/operation, state/reason, provider capability ID,
provider/session/access generations, counts/latency buckets and ephemeral random
correlation IDs not usable to recover/link an account. Metrics never use per-user,
handle, display-name or mutation IDs as dimensions.

Explicit privacy/admin tools use separate authenticated capabilities, return minimum
allowlisted fields, audit access and obey purpose/retention/deletion policy. Developer
build does not imply consent or allow raw IDs in ordinary logs. Support export previews
which categories will be included and defaults to redaction.

### 12. Errors preserve absence, restriction and stale identity

Stable outcomes include:

```text
platform.session.no_subject
platform.session.authenticating
platform.session.closing
platform.session.stale
platform.session.switch_failed
platform.identity.binding_failed
platform.identity.mapping_unavailable
platform.access.consent_required
platform.access.denied
platform.access.restricted
platform.access.revoked
platform.access.policy_stale
```

Composition-time service absence remains `platform.capability.unavailable`; explicit
Null remains `platform.provider.null`; an operation dynamically denied for the current
subject uses the specific access result above rather than generic provider failure.
Provider-native codes may appear only as bounded redacted cause evidence.

A denied/restricted/consent-required operation fails before ADR-130 request creation or
provider call. Revocation/staleness after admission follows request retirement but
blocks affected user-data commit. No path converts denial to empty friends, successful
presence clear, accepted progression or cloud absence.

### 13. Qualification exercises identity and policy races

Required evidence includes:

- type/compile-time separation of provider locator, local profile/storage, gameplay
  identity, live subject handle and presentation/social handles;
- handle RNG failure/zero/reuse/guess rejection, generation expiration and proof that
  handle/native bytes never enter durable or observable broad surfaces;
- sign-in candidate failure at each stage with no partial subject/capability/credential
  publication;
- sign-out/account switch/provider replacement before submit, in flight, after native
  commit and before callback, proving every user-scoped commit checks session generation;
- Alice-to-Bob switch with stale provider callbacks, queued progression, cloud journal,
  friends cache and observers never publishing/replaying into Bob;
- single/current/multiple-user providers, guest partition, same-account reauth and
  ambiguous identity refresh forcing new generation;
- consent required/granted/denied/revoked/expired/version-changed and OS/provider versus
  product-consent intersections at admission and commit;
- child/parental/region/restricted-account policy that cannot be bypassed by UI,
  config, debug tooling or stale receipts;
- bounded/malformed personal presentation and friends data, retention expiry,
  revocation cleanup and no raw-ID/PII log/metric/crash/tool leakage; and
- shutdown/cancellation timeout retaining provider/credential dependencies until late
  callback retirement without user publication.

Private provider tests qualify native account-change callbacks, stable-subject mapping,
restriction/consent projection and credential revocation. Public Mock/Null fixtures
cover the full Horo state/access model without real personal data.

## Consequences

- A live platform subject is a non-guessable generation-scoped capability, not a
  display/native/persistent identity.
- Every user-scoped commit is fenced against account switch, sign-out, provider reload
  and applicable consent/restriction changes.
- Local profiles, cloud/progression partitions and gameplay identities link only through
  explicit owners and cannot be inferred from a platform login.
- Consent and restricted-account behavior is enforced at capability admission/commit,
  while UI remains a presenter of authority-owned decisions.
- Personal/social data has bounded purpose, exposure and retention instead of becoming
  general gameplay/log/cache state.
- Hosts and providers must implement transactional switching, protected binding stores,
  policy revisions, cleanup and race qualification; a global “current user” is simpler
  but unsafe.

## Rejected Alternatives

### Expose a provider user ID string as `PlatformUserHandle`

Rejected because it is guessable/persistable, provider-specific and likely to leak
through gameplay, saves, logs or UI while lacking explicit session lifetime.

### Use display name, email or gamertag to map local profiles

Rejected because these are personal, mutable, non-unique and untrusted presentation
data. A provider must supply a private stable subject or advertise narrower capability.

### Keep one mutable process-global current user

Rejected because callbacks, requests and durable replay can cross account switches and
multi-user hosts cannot express separate subjects safely.

### Check the current user only when a callback arrives

Rejected because old work could be attributed to the new current account. Admission
captures and commit revalidates the exact session generation and subject partition.

### Treat UI visibility or a checkbox as the consent gate

Rejected because hidden/debug/headless/API paths could bypass it and stale UI state can
outlive policy. One versioned access snapshot controls all adapters.

### Treat provider/OS permission as blanket product consent

Rejected because platform authorization and product purpose/data-use disclosure are
separate requirements; effective access is their intersection.

### Return empty data for restricted/denied social access

Rejected because empty success is indistinguishable from “no friends/data,” encourages
retry/prompt loops and hides a policy boundary. Denial is typed before admission.

### Log hashed platform account IDs for correlation

Rejected because stable hashes remain linkable/potentially enumerable personal
identifiers. Use ephemeral non-account-derived correlation IDs and bounded aggregates.
