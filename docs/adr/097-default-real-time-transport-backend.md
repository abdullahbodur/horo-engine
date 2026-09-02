# ADR-097: Default Real-Time Transport Backend

- **Status**: Proposed
- **Date**: 2026-09-02
- **Supersedes**: The ENet-specific concrete-backend selection in [ADR-020](020-network-target-ownership-and-dependency-boundary.md); its target boundary, dependency direction and lifecycle decision remain active
- **Scope**: First production real-time transport selection, third-party encapsulation, security and authentication ownership, direct-IP baseline, optional NAT/relay and provider composition, bounded work, shutdown, qualification and migration
- **Issue**: [NET-001.9](https://github.com/abdullahbodur/horo-engine/issues/1106)
- **Jira**: [HORO-1106](https://horo-engine.atlassian.net/browse/HORO-1106)
- **Related**: [ADR-020](020-network-target-ownership-and-dependency-boundary.md)
- **Normative documents**: [Networking Architecture](../architecture/runtime/networking-architecture.md), [Multiplayer Replication Architecture](../architecture/runtime/multiplayer-replication-architecture.md)

## Context

Horo needs one production transport baseline without making a third-party wire
protocol part of `NetworkApi`. The architecture previously named ENet as the
concrete target while the replication document separately described a Horo-owned
reliable UDP transport as the default and listed browser, Steam and console
providers as if they were all 1.0 implementations. That leaves implementation,
security and support ownership ambiguous.

The evaluated choices were a Horo-owned reliability layer over UDP, ENet and
Valve's open-source GameNetworkingSockets (GNS). The comparison covered license,
maintenance, reliable and unreliable delivery, fragmentation, congestion behavior,
encryption, authentication, NAT/relay integration, platform portability, native
encapsulation and lifecycle testing.

The upstream snapshot evaluated for this decision is GNS v1.6.0, annotated tag
commit `2cb93a06350bb065db53abdb0d87cf297e0bfd34`, released 2026-06-03. The ADR does
not add or pin the dependency; the implementation change must pin an exact reviewed
release and commit according to repository dependency policy.

## Decision

### 1. GameNetworkingSockets is the first production baseline

The open-source GameNetworkingSockets library is the default production real-time
transport for direct-IP Horo client/server products. The private CMake target is
`HoroEngine::NetworkTransportGNS`. `HoroEngine::NetworkTransportNull` remains the
deterministic, offline and contract-test backend.

The baseline supports IPv4/IPv6 endpoints plus reliable and unreliable
message-oriented delivery. GNS owns native packet encryption, fragmentation,
reassembly, acknowledgement, retransmission and congestion behavior inside the
concrete backend. Horo retains its application protocol, session, replication,
queueing and scheduling contracts above that backend.

The initial supported topology is direct IP. Open-source GNS does not require
Steam for that topology. Native ICE/STUN/TURN, Steam signaling, Steam Datagram
Relay, WebRTC and console-network provider integrations are not mandatory M0 or
1.0 baselines.

### 2. No GNS identity crosses `NetworkApi`

`HoroEngine::NetworkTransportGNS` links GNS, its selected crypto provider,
protobuf and platform socket dependencies as `PRIVATE`. GNS headers, handles,
enums, connection-info structures, configuration keys, callback types and message
objects never appear under `include/Horo/`.

The host composition root creates the concrete backend through a private factory
and transfers `std::unique_ptr<INetworkTransport>` into `NetworkRuntime`.
`NetworkApi` continues to expose only Horo-owned addresses, opaque generation-
checked handles, delivery policies, channel/lane-neutral identifiers, capability
values, results and normalized events. Backend-specific diagnostics are copied
into bounded Horo diagnostic records.

GNS message lanes and native send flags may implement Horo delivery and priority
policy, but their numeric values are not serialized or exposed. Unsupported policy
combinations fail capability validation; the adapter does not silently degrade
reliable, ordering or priority semantics.

### 3. Security, authentication and traversal have separate owners

GNS transport encryption and packet integrity are backend capabilities. They
protect a concrete connection but do not establish Horo application identity,
authorization or protocol compatibility.

`NetworkRuntime` owns:

- application protocol and schema negotiation;
- credential/token exchange, peer identity and authorization policy;
- session admission, replay policy and replication authority;
- traversal/signaling policy, credential acquisition and host/provider choice.

A concrete transport may own an ICE/STUN/TURN or provider-native traversal
mechanism and report it through typed capabilities. The M0 GNS composition keeps
ICE/P2P and provider-specific features disabled. Enabling traversal later requires
an explicit product composition, credential and privacy policy, bounded lifecycle,
platform qualification and regression evidence.

Steam authentication, Steam signaling and Steam Datagram Relay belong to an
optional Steam provider composition. Console networking, browser/WebRTC and other
private SDKs are optional future peers behind the same Horo contract; none is a
fallback silently selected by the GNS adapter.

### 4. Configuration and work are bounded before native admission

The adapter validates Horo-owned configuration before calling GNS. Products must
set finite limits for listeners, connections, message size, channels/lanes,
queued inbound and outbound bytes/messages, connect timeouts, close drain,
per-poll events and per-flush submissions. Invalid, unknown or overflowed values
fail initialization or admission with a typed Horo error.

`Send()` copies accepted caller bytes into transport-owned bounded storage before
returning. Reliable overload rejects the send; replaceable unreliable snapshots
use the configured deterministic drop policy. Native callbacks and service work
publish normalized events into the bounded inbound queue and never mutate runtime,
scene, ECS or gameplay state.

Malformed native messages, impossible lengths, unknown connection generations,
unexpected callbacks and unsupported state transitions are rejected before
publication. Native error text is diagnostic context, never a control-flow or
serialized error identity.

### 5. Cancellation and shutdown are explicit lifecycle paths

Cancellation after a handle is issued stops resolution/connect work, closes any
native attempt and publishes exactly one terminal Horo event. A late native
callback is filtered by the connection and shutdown generation; it cannot revive a
cancelled or reclaimed handle.

Shutdown performs this order:

1. stop new connect, listen and send admission;
2. close listeners and cancel unresolved/pending connects;
3. request connection close and perform only the configured bounded drain;
4. stop and join the backend service thread;
5. drain or explicitly discard native messages and normalized queued events under
   the shutdown policy;
6. release GNS connections, interfaces and library state; then invalidate all
   remaining Horo handles.

No callback may target the adapter or runtime after destruction. Shutdown remains
idempotent after partial initialization and reports cleanup failures without
skipping owned resource release.

### 6. Qualification is contract-driven

The implementation is not production-qualified until automated coverage proves:

- successful listen/connect, reliable and unreliable transfer, close and reconnect;
- zero/max boundary values, queue and message limits, IPv4/IPv6 and capability
  mismatch behavior;
- malformed, truncated, oversized, duplicate, reordered and flood inputs without
  unbounded allocation or state mutation;
- cancellation during resolution/connect and races with native success/failure;
- shutdown before initialization, after partial failure and with active listeners,
  connections, callbacks and queued messages;
- parity of public errors, events, handles and ownership against
  `NetworkTransportNull` contract tests;
- dependency builds and runtime smoke tests on each claimed platform/toolchain,
  including crypto-provider configuration and sanitizer/fuzz coverage where
  supported.

Tests may use GNS packet simulation and statistics through a private test seam.
Those native controls do not become public API.

### 7. Dependency admission and upgrades are deliberate

The implementation change records the exact GNS release, resolved commit, license,
crypto provider, protobuf/toolchain requirements and enabled/disabled features.
The baseline build disables ICE/P2P and Steam-specific integration unless a later
decision and product target explicitly compose them.

Upgrades require upstream release-note and security review, supported-platform
builds, wire-compatibility assessment, contract tests and lifecycle tests. A GNS
upgrade never changes `NetworkApi` merely to mirror a native feature. Horo's
application protocol fingerprint/version changes whenever wire or policy
compatibility cannot be proven.

### 8. Migration replaces the private baseline, not the public contract

Implementation replaces the aspirational `NetworkTransportENet` target, factory
and product mappings with `NetworkTransportGNS`. Existing backend-neutral
`INetworkTransport`, address, handle, delivery, capability, event and runtime
contracts remain the migration seam.

There is no promise of ENet or earlier prototype wire compatibility and no saved
project data requires migration. Any existing prototype sessions must use an
explicit new protocol fingerprint. Tests move to the shared transport contract;
backend-specific fixtures remain private to their adapter.

If GNS later becomes unsuitable, another concrete adapter can replace it without
changing callers. Migration cost is the adapter, dependency/build topology,
configuration projection, platform qualification, diagnostics and wire-version
rollout—not a public API rewrite.

## Evaluation Evidence

- [GameNetworkingSockets repository and feature overview](https://github.com/ValveSoftware/GameNetworkingSockets)
- [GameNetworkingSockets v1.6.0 release](https://github.com/ValveSoftware/GameNetworkingSockets/releases/tag/v1.6.0)
- [GameNetworkingSockets build and dependency guidance](https://github.com/ValveSoftware/GameNetworkingSockets/blob/v1.6.0/BUILDING.md)
- [GameNetworkingSockets BSD-3-Clause license](https://github.com/ValveSoftware/GameNetworkingSockets/blob/v1.6.0/LICENSE)
- [ENet repository and feature overview](https://github.com/lsalzman/enet)
- [ENet MIT license](https://github.com/lsalzman/enet/blob/master/LICENSE)

## Consequences

### Positive

- Horo starts from a maintained, permissively licensed, message-oriented transport
  with integrated reliability, fragmentation and encrypted packet support.
- Direct-IP client/server work has one testable baseline without requiring a
  platform provider, relay service or browser stack.
- Third-party replacement remains bounded by a Horo-owned public contract.
- Security, session authentication and traversal responsibilities are explicit.
- Production qualification includes malformed input, cancellation and shutdown,
  rather than covering only the happy path.

### Negative

- GNS adds native crypto and protobuf build/dependency work plus a private adapter.
- Horo must track upstream security, compatibility and platform changes.
- Direct IP does not solve matchmaking, signaling, NAT traversal or relay service
  operation by itself.
- GNS's native model is richer than the initial Horo abstraction; unsupported
  features remain unavailable until deliberately modeled.

## Rejected Alternatives

### Horo-owned reliable UDP as the production baseline

Maximum control and minimal third-party dependencies do not offset ownership of a
new wire protocol, reliability, fragmentation, congestion, crypto, abuse defense,
cross-platform socket behavior, compatibility and fuzz/security burden. Horo may
retain low-level experimental transports, but they are not the production default.

### ENet as the production baseline

ENet is small, permissively licensed and supplies reliable UDP channels,
fragmentation and bandwidth throttling. It does not provide the selected baseline's
integrated encrypted connection model or traversal/provider growth path. Choosing
it would require Horo to design and qualify more security and connectivity layers
before production use. It is not kept as a mandatory parallel backend.

### Make Steam, WebRTC or console providers mandatory 1.0 transports

These choices tie baseline availability to store, browser or private platform SDK
requirements and do not cover the same product set. They remain optional future
compositions when a product requirement and qualification matrix justify them.

### Expose GNS directly through `NetworkApi`

This would couple public headers, callers, serialization and tests to a replaceable
dependency and its native lifecycle. The private adapter boundary is mandatory.

### Enable ICE/P2P and relay behavior by default

Traversal adds signaling, credentials, privacy, service availability, cancellation
and platform-policy obligations. It requires a separate explicit composition and
qualification; it is not inferred from library availability.
