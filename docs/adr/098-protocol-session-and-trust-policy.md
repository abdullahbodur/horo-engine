# ADR-098: Protocol, Session and Trust Policy

- **Status**: Proposed
- **Date**: 2026-09-02
- **Supersedes**: None
- **Scope**: Transport-to-session admission boundary, wire compatibility, listener trust profiles, peer and application identity, credential binding, active-session gating, bounded hostile parsing, timeout, revocation, diagnostics and shutdown
- **Issue**: [NET-002.1](https://github.com/abdullahbodur/horo-engine/issues/1110)
- **Jira**: [HORO-1110](https://horo-engine.atlassian.net/browse/HORO-1110)
- **Related**: [ADR-020](020-network-target-ownership-and-dependency-boundary.md), [ADR-097](097-default-real-time-transport-backend.md)
- **Normative documents**: [Networking Architecture](../architecture/runtime/networking-architecture.md), [Application Security Architecture](../architecture/security/application-security.md), [Multiplayer Replication Architecture](../architecture/runtime/multiplayer-replication-architecture.md)

## Context

A native transport can report `Connected` after it has established packet flow and
possibly an encrypted channel. That fact does not prove that the peer is the
intended product/server, that its application protocol is compatible, that a user
or service is authorized, or that gameplay messages are safe to parse. Treating
transport connectivity as a gameplay session would let malformed or hostile peers
reach replication and RPC dispatch before Horo establishes trust.

ADR-097 makes the distinction concrete. GameNetworkingSockets encrypts ordinary
connections, but its API explicitly reports whether a peer identity was
authenticated and leaves application policy to the caller. An IP address is not
an authenticated identity. Horo must therefore own the protocol and trust
decision above every transport, including Null and future provider backends.

The policy must also distinguish loopback development, LAN and remote exposure.
Loopback tests cannot require an external identity service. LAN does not make a
peer trustworthy merely because its address is private. Remote listeners require
stronger admission and cannot silently inherit a developer setting. All profiles
still need bounded parsing, deterministic compatibility, terminal timeouts and a
clean shutdown path.

## Decision

### 1. Transport connection and application session are separate authorities

`INetworkTransport` owns connectivity, packet confidentiality/integrity capability,
native peer evidence and transport close. It emits normalized transport events but
never creates an application principal or authorizes gameplay.

`NetworkRuntime` owns one `SessionAdmissionController` per listener/runtime
composition. Only that controller may create and activate a `NetworkSession`. The
state machine is:

```text
Created
  -> Negotiating
  -> Authenticating
  -> Activating
  -> Active
  -> Closing
  -> Closed

Any non-terminal state -> Failed
```

`TransportConnectionState::Connected` creates a `NetworkSession` in `Created`; it
does not enter `Active`. Before `Active`, only bounded admission control messages
are accepted on the reserved control channel. Gameplay, RPC, replication, voice,
console and extension messages received early are rejected, counted and may close
the connection under abuse policy. They are never buffered for later dispatch.

`MessageDispatcher`, `ReplicationManager` and gameplay callbacks receive messages
only when the owner-thread session table contains the matching generation in
`Active`. Transport callbacks, queue ordering and a forged session ID cannot
bypass that lookup.

### 2. The host supplies an immutable trust-policy snapshot

The composition root resolves credentials, trust anchors and product policy before
opening a listener or beginning a connection. It supplies a Horo-owned immutable
`NetworkTrustPolicySnapshot` containing:

```cpp
enum class NetworkExposure : std::uint8_t {
    LoopbackDevelopment,
    LocalNetwork,
    Remote
};

struct NetworkTrustPolicySnapshot {
    NetworkTrustPolicyId id;
    NetworkTrustPolicyRevision revision;
    NetworkExposure exposure;
    ProtocolCompatibilityPolicy protocol;
    TransportProtectionPolicy transportProtection;
    PeerTrustPolicy peerTrust;
    SessionCredentialPolicy credentials;
    SessionLimitPolicy limits;
};
```

Project data, a remote peer and a transport backend cannot choose or weaken this
snapshot, install a trust root, disable verification or widen bind scope. Secret
material remains in the host credential provider; the snapshot contains opaque
credential/trust handles and public policy only.

The listener validates exposure against the effective bind addresses. A loopback
profile may bind only OS loopback addresses. A LAN profile may not bind a public
interface unless host policy explicitly resolves it as `Remote`. DNS names and
private-address appearance are not trust evidence. A configuration mismatch fails
before listener publication.

Policy updates create a new listener/runtime generation. Existing sessions retain
their admission snapshot except that explicit trust, credential or principal
revocation may close them. A reload cannot retroactively weaken an active session.

### 3. Protection and identity requirements are exposure-specific

| Exposure | Transport confidentiality/integrity | Server/host identity | Client application admission |
|---|---|---|---|
| Loopback development | Required for native socket transport when supported; Null is in-memory | May use explicit local-only identity because both endpoints and bind are proven loopback | Explicit local development principal or configured credential; never inferred from `Connected` |
| Local network | Required | Required through a product trust anchor, exact public-key/certificate pin, or explicit out-of-band pairing result | Host policy requires a credential or explicitly grants a bounded LAN guest principal |
| Remote | Required | Required through a product trust anchor or exact pin/provider verifier | Required short-lived application admission proof; anonymous transport peers cannot become active |

An IP-address identity, private subnet, successful key exchange, encrypted-but-
unauthenticated channel, process ownership claim or user-visible server name is not
authenticated peer identity.

LAN pairing is an explicit user/host transaction that verifies a short-lived
out-of-band value and stores a scoped pin. Trust-on-first-use without confirmation
is not the default. LAN guest mode is an explicit product policy with a minimal
principal/capability set, finite lifetime and diagnostic visibility; it is not an
implicit anonymous fallback.

Remote clients present a short-lived admission proof issued or accepted by the
host's configured identity/matchmaking service. It is bound to the intended
product, server identity, protocol negotiation, client/server nonces and expiry so
captured proof cannot be replayed into another listener/session. The runtime asks
an injected credential verifier for a typed result; it does not parse provider-
specific tokens or store raw secrets in session state.

Transport client certificates or provider identity may supply peer evidence to
that verifier. They do not automatically define Horo gameplay roles. Conversely,
an application token sent over an unauthenticated remote channel is insufficient
because the client cannot prove which server received it.

### 4. Negotiation is canonical, bounded and downgrade-resistant

The admission protocol uses a fixed binary envelope and closed registered message
types. Every envelope is validated for minimum header, declared length, total
length, message count, field count and profile limits before allocation or nested
decode. Unknown required fields/messages fail; unknown explicitly optional fields
may be skipped only within their bounded length.

The hello exchange carries Horo-owned values:

- product/application ID and protocol family ID;
- minimum/maximum supported wire protocol version;
- gameplay schema-set fingerprint and compatibility epoch;
- required and optional feature/capability IDs;
- selected trust profile ID/revision and transport protection evidence;
- fresh client/server nonces and a canonical transcript version.

The server selects the highest mutually supported version that is not below either
side's configured security/compatibility floor. Required capability sets and
schema fingerprints must satisfy the version's explicit compatibility table.
There is no lexicographic version comparison, best-effort schema guessing, native
struct layout negotiation or silent feature downgrade.

Both sides compute the same canonical transcript digest. Peer verification,
credential proof and activation acknowledgements bind to that digest. A retry or
new transport connection uses fresh nonces and a new admission generation.

### 5. Authentication produces a bounded principal, not ambient authority

Successful verification produces a Horo-owned immutable `SessionPrincipal`:

```cpp
struct SessionPrincipal {
    NetworkPrincipalId principalId;
    NetworkSessionId sessionId;
    NetworkTrustLevel trustLevel;
    BoundedVector<NetworkRoleId> roles;
    BoundedVector<NetworkCapabilityId> capabilities;
    CredentialProvenanceId provenance;
    MonotonicDeadline expiresAt;
};
```

`NetworkSessionId` is a fresh cryptographically random 128-bit value scoped to one
admission generation. It is correlation identity, not an authentication secret.
Principal, roles and capabilities come only from the injected verifier/host policy;
client claims are input to verification, never authority.

The principal is copied into the owner-thread session table only during the atomic
`Authenticating -> Activating` transition. Activation completes after both peers
exchange and validate transcript-bound activation acknowledgements. Only then is
the session published as `Active` and admitted to dispatch.

Credential expiry, absolute session lifetime, inactivity policy, concurrent-session
limits and revocation behavior are explicit product policy. The M0 baseline closes
the session on expiry/revocation; it does not silently extend or refresh authority
inside gameplay dispatch. A future reauthentication flow must return to a closed
admission gate and cannot preserve queued gameplay across failed verification.

### 6. Admission work and hostile behavior are bounded

Every listener policy defines finite limits for pre-active connections, admission
bytes/messages, message size, authentication attempts, credential-verifier work,
per-source rate, total concurrent verification, negotiation deadline and
activation deadline. Limits have units and deterministic reject/close behavior.

Admission parsing occurs before gameplay deserialization and validates before
allocation. Expensive credential verification is scheduled only after cheap
envelope, product, version, capability, nonce and protection checks. A per-listener
budget drains completed verification on the owner thread. Cancellation and timeout
invalidate the admission generation; late verifier or transport completion is
discarded.

Malformed, incompatible and hostile peers receive only bounded safe failure codes
where disclosure policy permits. Internal verifier detail, token contents, trust
anchors, account existence, schema inventory and native error strings are not sent
to the peer. Repeated early gameplay, oversized input, nonce replay, rate-limit
evasion or invalid proof follows the configured abuse close policy.

### 7. Failure, cancellation and shutdown are terminal

An incompatible protocol, failed peer proof, invalid/expired credential, malformed
admission message, timeout, cancellation or revocation transitions the session once
to `Failed`/`Closing`. It never falls back to a weaker exposure or guest profile.
The transport is closed after a bounded safe failure response when permitted.

Shutdown order is:

1. stop accepting new transport connections and admission messages;
2. invalidate admission generations and cancel verifier work;
3. remove every session from gameplay dispatch before closing transports;
4. close active and pre-active sessions under the bounded drain policy;
5. drain/discard late completions by generation, release principal snapshots and
   wipe short-lived proof material;
6. destroy verifier/runtime state only after no callback can publish into it.

Partial initialization and repeated shutdown are safe. No session becomes active
during or after shutdown.

### 8. Diagnostics preserve evidence without secrets

Diagnostics record policy ID/revision, exposure, protocol outcome, safe failure
class, selected version/fingerprint ID, trust level, session generation, timing,
rate-limit action and close reason. They do not record credentials, admission
proofs, raw tokens, private keys, transcript secret material or unnecessary remote
payloads.

Metrics distinguish transport-connected, negotiating, authenticating, active,
rejected, timed-out and revoked sessions. A transport connection count is never
reported as an authenticated player count.

### 9. Qualification covers the complete gate

Focused automated coverage proves:

- valid loopback, LAN and remote flows through activation and gameplay dispatch;
- connected transport with early gameplay never reaches a gameplay/replication
  callback;
- version/fingerprint/capability boundaries, downgrade attempts and policy/bind
  mismatches;
- malformed/truncated/oversized/duplicate/reordered admission messages, unknown
  required fields, nonce replay, proof replay, rate/flood limits and verifier
  exhaustion;
- wrong server pin/trust root, unauthenticated encrypted peer, invalid/expired/
  revoked credential and LAN guest capability bounds;
- negotiation, verification and activation timeouts plus cancellation races and
  late completions;
- shutdown at every state, partial verifier initialization, active revocation and
  zero dispatch/callback after teardown;
- secret redaction in errors, logs, metrics and diagnostic bundles.

Contract tests run against Null and the production GNS adapter. Host/provider
credential verifiers use deterministic fakes for failure/race coverage and bounded
integration tests for real provider composition.

## Security References

- [GameNetworkingSockets identity and authentication API](https://github.com/ValveSoftware/GameNetworkingSockets/blob/master/include/steam/isteamnetworkingsockets.h)
- [GameNetworkingSockets identity types and unauthenticated status](https://github.com/ValveSoftware/GameNetworkingSockets/blob/master/include/steam/steamnetworkingtypes.h)
- [OWASP ASVS 5.0 Session Management](https://github.com/OWASP/ASVS/blob/master/5.0/en/0x16-V7-Session-Management.md)
- [NIST SP 800-63B-4: Authentication and Authenticator Management](https://nvlpubs.nist.gov/nistpubs/SpecialPublications/NIST.SP.800-63b-4.pdf)

These references inform the threat model and session controls; this ADR does not
claim blanket ASVS or NIST conformance for every game product.

## Consequences

### Positive

- Transport connectivity can never bypass protocol, identity or authorization.
- Loopback, LAN and remote exposure have explicit non-ambient trust rules.
- Protocol downgrade, replay and pre-auth gameplay paths have one bounded owner.
- Provider credentials and native transport identities remain behind Horo-owned
  typed verification contracts.
- Metrics and diagnostics distinguish connectivity from authenticated sessions.

### Negative

- Products must provision trust roots/pins, credential verifiers and session policy
  before exposing LAN or remote listeners.
- LAN pairing and remote admission add user/service lifecycle work beyond encrypted
  packet transport.
- Strict compatibility fingerprints require deliberate rollout/version policy.
- Closing on expiry/revocation is less seamless than transparent refresh, which is
  intentionally deferred until a safe reauthentication contract exists.

## Rejected Alternatives

### Treat transport `Connected` as an active gameplay session

Encryption and packet flow do not prove product compatibility, application
identity or authorization. This would expose gameplay parsers and authority before
admission completes.

### Trust IP address, private subnet or LAN membership

Network location is attacker-influenced and does not authenticate a host or user.
LAN requires encryption plus explicit host trust and application admission policy.

### Accept encrypted but unauthenticated remote connections

Encryption without authenticated server identity remains vulnerable to an active
intermediary and cannot safely carry application credentials. Remote activation
requires both server trust and application admission proof.

### Let each transport/provider define session identity and roles

This would make gameplay authority backend-dependent and expose provider semantics
through runtime code. Provider evidence is input to the Horo verifier, not the
principal model.

### Default trust-on-first-use for LAN

Silent first contact can pin an attacker's identity. Explicit pairing or a
preconfigured product trust anchor/pin is required.

### Buffer gameplay until authentication completes

Pre-auth buffering creates memory pressure, replay ambiguity and an alternate
dispatch path. Early gameplay is rejected and never replayed after activation.

### Use flexible unbounded JSON negotiation

An extensible text object does not justify attacker-controlled allocation,
recursion or ambiguous canonicalization. The admission envelope is closed,
versioned, binary and bounded.

### Fall back to a weaker profile after failure

Automatic guest, plaintext or local-profile fallback converts attack/failure into
authority. Admission failure is terminal; a different profile requires a new
explicit host composition.
