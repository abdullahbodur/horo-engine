# ADR-178: Application Security Primitive And Signature Baseline

- Status: Accepted
- Date: 2026-09-10
- Deciders: Horo Engine maintainers
- Supersedes: ADR-002 for credential storage and plaintext fallback semantics
- Issue: [SEC-001](https://github.com/abdullahbodur/horo-engine/issues/33)
- Jira: [HORO-35](https://horo-engine.atlassian.net/browse/HORO-35)
- Normative documents: `docs/architecture/security/application-security.md`, `docs/architecture/release/release-security.md`, `docs/architecture/foundation/platform-abstraction.md`, `docs/architecture/extensions/plugin-system.md`

## Context

The engine had SHA-256 primitives and archive integrity validation, but no typed
security failure domain, OS entropy contract, secure credential value lifetime,
trusted-root signature verifier, or mandatory pre-native-load gate. A digest can
detect corruption but cannot authenticate a publisher. A user approval flag also
cannot prove that the bytes loaded now are the bytes that were reviewed.

The principal threats are forged or substituted native modules, stale evidence
after a file mutation, unknown or revoked publishers, unavailable security
providers silently selecting a weaker path, guessable credential references, and
secret values escaping through configuration or diagnostics.

## Decision

`HoroSecurity` owns backend-neutral security contracts and stable typed errors.
It depends only on Foundation. `HoroPlatform` supplies native entropy using
`BCryptGenRandom` on Windows, `SecRandomCopyBytes` on Apple platforms, and
`getrandom` on Linux. Failure clears the requested output and is returned; there
is no pseudorandom fallback.

Secrets use a move-only `SecureBytes` owner whose storage is overwritten before
release. `CredentialVault` exposes only random opaque references outside a
resolve operation and coordinates put, resolve, rotate, expiry, revocation, and
teardown over an explicitly injected credential backend. A null or unavailable
backend fails closed and may later be replaced by the composition root. Project
files, environment fallbacks, logs, diagnostics, and error messages are not
credential backends.

Detached application-artifact signatures use ECDSA P-256 with SHA-256. The
signed, domain-separated payload binds the algorithm, publisher identity, key
identity, and exact artifact SHA-256 digest. The portable implementation uses
the repository-pinned MbedTLS `mbedcrypto` target behind `SignatureProvider`;
MbedTLS remains a private implementation detail. Public keys enter an explicit
host-owned trusted-root store. `VerifiedArtifactEvidence` has no public
constructor and is emitted only after exact digest comparison, trusted-root
lookup, algorithm agreement, and cryptographic verification all succeed.

Every native extension load requires a host-composed `NativeArtifactGate`.
Missing evidence, unknown algorithms or keys, unavailable providers, invalid
signatures, digest mismatches, and stale evidence return typed errors before the
platform loader or extension callback can run. The loader rechecks current bytes
against the verified digest immediately before `dlopen`/`LoadLibrary`; packaging
must additionally keep verified artifacts immutable to close filesystem TOCTOU
windows.

## Consequences

- Windows, macOS, and Linux share one verification algorithm and provider
  contract while retaining native entropy implementations.
- Hosts must explicitly compose trust roots, envelope lookup, credential
  backends, and the native activation gate. Missing composition is a safe error.
- ECDSA signatures are represented as fixed-width 32-byte `r` followed by
  32-byte `s`; public keys use the 65-byte uncompressed SEC1 point encoding.
- Key rotation adds a new trusted key identity; revocation removes the old root.
  Existing evidence must be regenerated for changed bytes.
- Platform-specific durable credential backends can be added without changing
  callers or introducing plaintext compatibility behavior.

## Rejected Alternatives

- Hash-only admission was rejected because it does not authenticate a publisher.
- Home-grown cryptography was rejected in favor of a pinned, reviewed provider.
- OpenSSL/system-library discovery was rejected because availability and API
  behavior vary across supported build hosts.
- Environment variables, project configuration, or plaintext files as a
  credential fallback were rejected because they broaden secret lifetime and
  leak surfaces.
- A permissive default activation gate was rejected because provider outages and
  missing metadata must never execute native code.
