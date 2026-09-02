# ADR-133: Platform Progression Authority, Trust and Idempotency

- **Status**: Proposed
- **Date**: 2026-09-02
- **Supersedes**: None
- **Scope**: Achievement, stat and leaderboard mutation/query authority; local and server trust boundaries; typed mutation semantics; idempotency, retry, duplicate, replay, ambiguity, unsupported capability and cheating policy
- **Issue**: [PLS-004.1](https://github.com/abdullahbodur/horo-engine/issues/1889)
- **Jira**: [HORO-1845](https://horo-engine.atlassian.net/browse/HORO-1845)
- **Related**: [ADR-008](008-error-model-exception-boundary-and-registry.md), [ADR-020](020-network-target-ownership-and-dependency-boundary.md), [ADR-098](098-protocol-session-and-trust-policy.md), [ADR-102](102-runtime-network-modes-and-authority-exposure.md), [ADR-113](113-local-storage-user-profile-and-slot-ownership.md), [ADR-130](130-platform-services-frontend-request-lifetime-timeout-null-and-error-semantics.md), [ADR-131](131-platform-services-closed-sdk-extension-abi-package-and-composition-boundary.md), [ADR-132](132-platform-services-project-salt-stable-id-tombstone-and-provider-mapping.md)
- **Normative documents**: [Platform Services Architecture](../architecture/runtime/platform-services-architecture.md), [Multiplayer Replication Architecture](../architecture/runtime/multiplayer-replication-architecture.md), [Application Security](../architecture/security/application-security.md)

## Context

Achievements, persistent stats and leaderboard scores project gameplay facts into a
remote platform account. The current architecture exposes simple `Unlock`, `SetStat`
and `SubmitScore` calls and says writes can be retried with an idempotency key. It does
not say which owner establishes the fact, whether a client is trusted to submit it,
what duplicate success means or which stat/score operations are actually idempotent.

This distinction matters after an uncertain provider outcome. A frontend timeout is
terminal for its ADR-130 request, but the provider may still have committed the remote
effect. Retrying an unlock or monotonic maximum can be safe; retrying an increment or
unconditional replacement may double count or reorder state. A key helps only if the
provider or an authoritative gateway truly enforces deduplication.

Remote platform state is also not automatically authoritative gameplay state. A local
client can be modified, provider results are account-scoped external observations and
leaderboard ranking is provider-owned presentation. Competitive progression, rewards
and shared economy need a server-owned validation path without teaching gameplay code
which provider SDK or backend is active.

This ADR assigns every progression mutation/query owner, fixes mutation algebra and
idempotency behavior, and defines the server-authoritative route. PLS-007.1 separately
owns the generic durable queue file, ordering, expiry and shutdown mechanics; it must
admit only the replay classes defined here.

## Decision

### 1. Gameplay facts, Horo intent and remote projection are separate authorities

The authority chain is:

| Stage | Authority and responsibility | Deliberate non-authority |
|---|---|---|
| Gameplay semantic owner | Decides that a committed game fact occurred, with stable occurrence/evidence identity | Does not select a provider, retry or format native values |
| Product progression policy/router | Validates the registered definition, subject, authority mode and typed mutation; accepts one logical intent | Does not invent gameplay facts or treat provider state as simulation truth |
| Authority server, when required | Validates the fact in its authoritative world/epoch and is the only owner allowed to accept server-mode mutation intent | Client prediction, local UI and listen-server client world cannot mint authority |
| Platform progression coordinator | Owns semantic normalization, duplicate/coalescing rules, reconciliation and one intent outcome | Does not own ADR-130 request state or provider-native operations |
| `PlatformServicesFrontend` | Owns request admission, attempts, deadline, cancellation, terminal result and provider-generation fencing | Does not decide achievement eligibility, score validity or anti-cheat policy |
| PLS-007 durable owner | Persists and schedules only eligible logical intents with their original mutation identity/scope | Does not change mutation algebra or create a second cloud/progression authority |
| Provider adapter/package | Translates one validated Horo operation and declares/enforces native capability/limits | Cannot allocate Horo IDs, authorize gameplay facts or silently weaken semantics |
| Remote platform service | Owns the external account record, provider revision/deduplication behavior and ranked view | Its record is not authoritative Horo simulation, economy or reward state |
| UI/telemetry | Observes bounded typed outcomes and safe aggregate state | Cannot submit privileged mutations by changing display state |

The Horo source of identity remains ADR-132. Definitions, mutation values, outcomes
and authority policy are typed Horo data. Provider API names, trophy numbers, native
score structs, account IDs and display strings never become gameplay identity or
branching policy.

### 2. Every definition declares one immutable authority mode

Each achievement, stat and leaderboard definition binds one
`ProgressionAuthorityMode` in the cooked product profile:

```cpp
enum class ProgressionAuthorityMode : std::uint8_t {
    LocalProduct,
    AuthorityServer,
};
```

`LocalProduct` permits the current standalone/local product progression coordinator to
accept facts. It is appropriate only where the product explicitly accepts that a
modified client can fabricate progress. Provider APIs, local obfuscation, save
signatures and idempotency keys do not make this mode cheat resistant.

`AuthorityServer` permits acceptance only through a server-world progression commit
capability bound to the current host, world and authority generations. Client builds
receive no such capability. A listen server has separate server and client worlds;
locality, loopback, headless state, a provider login or a process-global `IsServer`
boolean cannot grant the client world authority.

Competitive ranks, shared economy/rewards, cross-player records and any mutation whose
integrity affects another user or trusted service use `AuthorityServer`. A product may
also choose server authority for ordinary achievements. If its selected deployment has
no compatible server progression gateway/provider route, the required definition is
unavailable at composition; it never downgrades to `LocalProduct`.

Authority mode and mutation semantics are release/cook inputs. Runtime flags, gameplay
code or providers cannot rewrite them. Changing either after publication is a product
data migration and compatibility review, not a live setting.

### 3. Gameplay submits Horo intent through one policy-routed port

Gameplay produces a typed fact, not a platform call:

```cpp
struct ProgressionFact {
    ProgressionDefinitionId definition;
    GameplayOccurrenceId occurrence;
    ProgressionValue value;
    GameplayAuthorityContext authority;
};

class IProgressionIntentSink {
public:
    virtual Result<ProgressionIntentReceipt>
    Submit(const ProgressionFact& fact) = 0;
};
```

The injected application capability routes according to the cooked definition. For a
local definition it validates/accepts locally. For a server definition a client sends
ordinary authenticated gameplay input/command to the server-owned gameplay system;
the client does not forward a “grant achievement” RPC. The authoritative server
validates its committed fact and submits through its server-only progression commit
port. Gameplay source is identical with respect to platform providers in either case.

`GameplayOccurrenceId` deduplicates one committed semantic fact and is scoped to its
owning world/session schema. It is not a provider idempotency token or proof by itself.
Prediction/rollback never publishes irreversible remote effects: the authority waits
for committed fact confirmation, and replay of the same occurrence resolves to the
same accepted logical intent rather than emitting another mutation.

Debug, editor, CLI and MCP adapters require an explicitly registered permission and
the same authority capability. Shipping clients do not gain server progression
authority through tooling. Administrative provider corrections are out-of-band
operator flows and do not share the runtime gameplay API.

### 4. One logical mutation receives one immutable mutation ID

When the correct semantic authority first accepts a valid fact, the progression
coordinator allocates one nonzero 128-bit `ProgressionMutationId` and records:

```cpp
struct ProgressionMutationEnvelope {
    ProgressionMutationId mutation;
    ProgressionSubjectScope subject;
    ProgressionDefinitionId definition;
    ProgressionMutationKind kind;
    ProgressionValue value;
    ProgressionPrecondition precondition;
    ProgressionAuthorityEvidence authority;
    ProgressionPolicyRevision policy;
};
```

The ID identifies this logical mutation from initial submission through bounded
frontend retry, process restart, durable replay and reconciliation. A new ADR-130
request/attempt may be created, but it carries the same mutation ID and exact semantic
payload. Timeout, cancellation or a dropped request handle never authorizes allocating
a replacement mutation ID for the same logical intent.

The authority evidence is a bounded Horo-owned proof/reference to the accepted local
or server fact and captured generations. It contains no credential, raw account ID,
provider request token or unrestricted gameplay payload. The provider sees only the
minimal translated fields needed by the accepted operation.

An exact duplicate `(mutation ID, canonical envelope)` joins/observes the existing
logical intent and receives its outcome. Reuse of a mutation ID with any different
subject, definition, kind, value, precondition, authority or policy revision fails
`platform.progression.idempotency_conflict`. Last writer never wins.

Idempotency is deduplication, not authentication. Locally supplied IDs do not grant a
fact, bypass authority validation or prove an unmodified client.

### 5. Mutation algebra determines retry and replay safety

Version 1 admits only declared semantics:

| Operation | Horo semantic effect | Automatic retry/replay eligibility |
|---|---|---|
| `UnlockOnce` | Remote unlocked state becomes true; never resets | Intrinsically idempotent when provider conformance proves repeated unlock is the same effect |
| `SetProgressMaximum` | Progress becomes `max(remote,current)` within the registered total; never decrements | Eligible only when provider/gateway atomically supplies max-or-equal semantics |
| `SetStatMaximum` / `SetStatMinimum` | Typed stat moves monotonically in the declared direction | Eligible only with matching atomic provider semantics and bounds |
| `SetStatSnapshot` | Replace exact typed value at an expected provider revision | Eligible with an atomic conditional revision; precondition failure reconciles, never retries unconditionally |
| `AddStatOnce` | Apply one delta exactly once | Eligible only when provider/gateway durably deduplicates `ProgressionMutationId`; otherwise no automatic retry/replay after submission ambiguity |
| `SubmitBestScore` | Keep the declared ascending-minimum or descending-maximum best score | Eligible only when provider/gateway atomically implements that comparison |
| `ReplaceScoreAtRevision` | Replace exact score at an expected revision | Eligible only with atomic conditional revision and the exact captured revision |

An adapter cannot emulate atomic maximum, conditional replacement or durable dedupe
with a read followed by an unconditional write. A process-local “seen IDs” set cannot
protect another device, server or a provider commit that outlives the process.

Unconditional `SetStat`, naked increments and “last submitted score wins” are not
portable v1 semantics. A provider exposing only those primitives may support a
narrower declared capability or a trusted gateway may implement the required atomic
operation. It cannot claim the stronger Horo operation and hope retries are rare.

Reset/decrement/delete and moderation corrections are excluded from the runtime
progression API. They require a separately authorized operator/product migration path
with provider-specific audit and cannot be disguised as a negative delta.

### 6. Attempts preserve identity, payload, scope and deadline

The coordinator declares retry eligibility before frontend admission using the
operation algebra, provider capability snapshot and product policy. Each in-memory
retry uses the same ADR-130 request, original finite monotonic deadline, mutation ID,
canonical payload, subject, authority/policy/session/provider generations and
precondition. Backoff, rate-limit delay and attempt count are bounded by policy and
must fit the original deadline.

Only normalized transient transport/provider failure may retry automatically.
`Forbidden`, invalid definition/value, authority denial, unsupported capability,
permanent provider failure and an exhausted/invalid precondition do not. `RateLimited`
may retry only after a bounded provider-independent normalized delay accepted by
policy. A session/account or authority-generation change closes attempts rather than
retargeting the operation.

PLS-007 durable replay may create a new frontend request after restart or offline
recovery, but it preserves the exact logical envelope and validates current subject,
authority, registry/policy and provider capability before scheduling. It does not
reset an unsafe operation to “not attempted.” Reads/queries have no durable replay
intent.

### 7. Timeout and transport loss can leave remote outcome unknown

ADR-130 remains authoritative for the caller-visible request: a missed deadline
publishes `TimedOut` exactly once and late provider completion cannot rewrite it.
That terminal state does not prove the provider failed to commit.

The progression coordinator separately classifies the logical intent as
`RemoteOutcomeUnknown` when an eligible mutation may have crossed the provider commit
boundary without conclusive evidence. It then follows the mutation algebra:

- intrinsically idempotent/deduplicated operations may resubmit the same envelope;
- conditional operations first query the exact remote value/revision and either
  recognize the intended effect, retry with the still-valid precondition or surface a
  conflict;
- non-idempotent operations without durable provider/gateway dedupe stop automatic
  retry and require reconciliation or explicit operator/user resolution; and
- a late completion may resolve coordinator reconciliation evidence but never mutate
  the original request terminal record or invoke its observers twice.

`RemoteOutcomeUnknown` is not reported as success, failure or queued success. UI may
present a bounded pending/reconciling state. Gameplay cannot assume a reward was
granted or repeat a side effect merely because remote projection is uncertain.

### 8. Coalescing preserves semantic results and individual receipts

Exact duplicate mutation IDs deduplicate as Section 4. Distinct mutations may
coalesce only when their declared algebra is associative, commutative and result-
preserving for every intent:

- maximum/minimum progress or stat mutations for the same subject/definition/policy
  generation may reduce to the strongest value;
- `SubmitBestScore` may reduce to the mathematically best score under the definition's
  declared ordering, never the most recent arrival;
- unlocks may share one remote operation after every occurrence has its own accepted
  logical receipt; and
- conditional snapshots, replacements and `AddStatOnce` never coalesce.

The coordinator retains a bounded mapping from each mutation ID to the aggregate
operation and publishes an outcome for each receipt. Coalescing cannot erase a higher
score, acknowledge a mutation that failed authority validation, merge subjects or
cross policy/session/provider generations.

Independent targets may complete out of order. Mutations for one
`(subject, definition)` are serialized or combined by the declared algebra; provider
completion order never becomes last-writer policy.

### 9. Query results have explicit source and trust

Achievement-state and stat queries return the remote platform's account projection at
a captured provider/session generation and optional opaque revision. Leaderboard
queries return a bounded ranked view according to provider/product definition. They
are authoritative only for “what this provider currently reports,” not for the
underlying Horo gameplay fact.

Queries validate registered typed ADR-132 IDs, pagination/range/count limits, typed
values, provider response bounds and current generation before publication. Provider
display names, avatars or other personal data are separate consent-scoped presentation
fields; they are not identity or values accepted back into mutations.

Gameplay may use results for UI, local hints or explicitly non-authoritative product
features. It cannot derive server rewards, shared economy, simulation state or access
control from a client/provider query. A server that needs external state fetches it
through its trusted integration and applies product-owned validation.

Reads may receive a bounded transient in-memory retry under explicit policy, but are
never written to the progression offline queue. Cache entries include provider,
subject, registry and session generations plus freshness; stale cache is labeled and
cannot masquerade as a successful current query.

### 10. Unsupported semantics fail without policy downgrade

The provider capability snapshot declares each supported mutation kind, typed value
domain/range, atomic comparison/precondition support, durable mutation-ID dedupe,
query/revision features, limits and server/client availability. The product profile
intersects this with every required definition before admission opens.

Missing required semantics fails composition/certification or the required feature
with a typed reason. Optional definitions become explicitly unavailable. Horo never:

- maps `AddStatOnce` to an unconditional increment without dedupe;
- maps maximum/best semantics to read-then-write;
- changes `AuthorityServer` to local authority;
- accepts writes through Null or treats an absent mapping as success; or
- changes score ordering/value type to fit a provider.

Gameplay may suppress optional unavailable presentation intent before submission, but
that is not a remote success. Every admitted mutation still receives one explicit
logical/request outcome.

### 11. Security and privacy constraints are independent of idempotency

Client-authoritative progression is tamperable by definition. Horo does not market
provider unlock APIs, signed local saves, obfuscated values, SDK callbacks or
idempotency keys as anti-cheat. Secure competitive products validate facts on an
authority server and keep server credentials/privileged provider routes out of client
artifacts. Provider anti-cheat products, when used, are additional product policy and
do not replace server authority.

Mutation persistence and diagnostics store typed Horo IDs, mutation IDs, normalized
values, safe authority/policy/session generations and stable outcomes. They exclude
credentials, raw platform account IDs, native request tokens, unrestricted provider
errors and personal/display data. Subject scopes are opaque and partitioned; work
accepted for one subject/session cannot replay for another.

Repeated invalid authority, replay/conflicting mutation IDs, impossible value changes
and provider rejection may emit bounded security audit events. Logging a suspicion
does not silently reject a valid committed server fact or grant the frontend authority
to decide cheating.

### 12. Qualification covers authority, ambiguity and provider diversity

Required public Mock/Null and product integration evidence includes:

- every operation/query owner and local/server route, including client/listen-server/
  dedicated-server capability exposure and no local downgrade;
- rollback/replay of one gameplay occurrence producing one accepted logical mutation;
- exact duplicate join, conflicting ID/payload rejection and independent mutation IDs;
- each mutation algebra at lower/equal/higher values, score sort directions, bounds
  and typed value mismatch;
- retry attempt/deadline bounds and identical envelope across transient failure,
  process replay, rate limit, cancellation and timeout;
- completion-before/after timeout, provider-commit ambiguity, late evidence,
  reconciliation and no double observer/remote effect;
- providers with full dedupe, only intrinsic monotonic operations, conditional
  revisions and no safe mutation primitive, proving exact capability projection;
- coalescing permutations that retain the mathematical best/max/min and an outcome for
  every logical receipt;
- offline admission/replay eligibility, subject/session partitioning, stale authority
  and provider/policy generation rejection; and
- bounded/malformed query results, stale cache labeling, privacy redaction and proof
  that provider/native identifiers never enter gameplay persistence.

Private providers add SDK/gateway deduplication and atomic-operation qualification.
Certification freezes definition authority/algebra, mapping, provider capability and
gateway versions so a shipping update cannot silently weaken semantics.

## Consequences

- Gameplay owns facts while one progression coordinator owns remote intent semantics;
  provider and frontend layers cannot invent authorization.
- Competitive/server-owned paths are explicit and do not require gameplay to branch on
  Steam, console or other backend policy.
- Retry and replay safety follows the mutation algebra and real provider capability,
  not a blanket assumption that every stat write is idempotent.
- Every logical mutation has a durable identity across attempts, and a timeout can be
  reconciled without rewriting ADR-130 terminal state.
- Provider queries remain useful for UI while being unable to become trusted gameplay
  or economy state accidentally.
- Providers lacking atomic/dedupe primitives expose narrower capabilities; some
  operations intentionally remain unavailable rather than becoming lossy emulations.
- Server-authoritative products require a separately composed trusted progression
  gateway and operational reconciliation/deduplication storage.

## Rejected Alternatives

### Let gameplay call achievement/stat/leaderboard backends directly

Rejected because gameplay would own provider selection, retry and native semantics,
bypass authority policy and make server validation inconsistent.

### Treat every write carrying an idempotency key as safe to retry

Rejected because a token has no effect unless a provider/gateway durably enforces it;
increments and unconditional replacements can duplicate or reorder after ambiguity.

### Retry timeout as a new logical mutation

Rejected because timeout does not prove the remote effect was absent. A new ID can
double apply; the same intent must reconcile or retry under its declared algebra.

### Trust platform state as authoritative gameplay or anti-cheat truth

Rejected because local clients and account projections have different trust domains.
Server rewards and shared state require product-owned server validation.

### Send a client `GrantAchievement` or `SubmitScore` RPC to the server

Rejected because it asks the client to state the privileged conclusion. Clients send
ordinary gameplay input/commands; the server's gameplay owner derives and commits the
fact.

### Coalesce stat/score writes by keeping the most recent call

Rejected because arrival order can discard a better score, change monotonic progress
or hide an increment. Coalescing is permitted only under a declared result-preserving
algebra with individual receipts.

### Emulate atomic operations with read-then-unconditional-write

Rejected because another device/server can commit between calls. Missing atomic
provider semantics is capability absence unless a trusted gateway implements it.
