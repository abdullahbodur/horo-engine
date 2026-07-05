# Release Security

## Purpose

This document defines mandatory security boundaries and controls for release
creation, packaging, signing, automation, and publication.

Security controls apply equally when a release is triggered through the GUI,
CLI, MCP, or CI.

## Trust Boundaries

The release system crosses these trust boundaries:

- user or automation input into application services
- application services into local child processes
- engine processes into operating-system credential stores
- build output into staging and publication storage
- CI jobs into third-party actions and external services
- MCP clients into GUI or CLI hosts
- runtime asset requests into packaged archives

Data crossing a boundary is validated, minimally scoped, and represented by
typed values instead of interpolated command strings.

## Threat Surfaces

The security model covers:

- command arguments and process creation
- environment variables
- temporary files and staging directories
- logs, diagnostics, and job history
- credentials and signing identities
- CI permissions and third-party dependencies
- package construction and publication
- cryptography and key lifecycle
- MCP transport and authorization
- compiler and dependency caches

## Credential Model

Secrets are accessed through credential handles resolved at execution time.
Persistent release requests, project files, manifests, logs, and job history do
not contain raw secret values.

Credential providers may include:

- operating-system credential stores for local hosts
- CI secret stores for automation
- hardware-backed or remote signing services
- short-lived delegated credentials

Required controls:

- least-privilege access
- shortest practical lifetime
- no hardcoded project or engine secrets
- no secret serialization into build artifacts
- explicit zeroing of sensitive mutable buffers where practical
- revocation and rotation support for long-lived identities

Secrets are not passed through command-line arguments when a file descriptor,
protected temporary channel, credential provider, or dedicated process API is
available. Environment variables are a compatibility fallback, not the primary
secret transport.

## Process Execution

Commands are created from structured executable and argument arrays. Shell
concatenation is not used for untrusted paths or user input.

Child processes receive:

- an explicit working directory
- a minimal environment
- only required credential handles or secret channels
- bounded stdout and stderr capture
- cancellation and timeout control

Executable paths and toolchain identities are validated before execution.
Relative path resolution does not search attacker-controlled directories.

## Logging And Redaction

Logs, progress events, diagnostics, crash reports, and telemetry are treated as
potential disclosure channels.

Required controls:

- secrets are represented by dedicated sensitive types where possible
- known credentials are redacted before formatting
- command previews use redacted argument structures
- environment dumps are prohibited
- access tokens, passwords, private keys, and authorization headers are never
  intentionally logged
- source paths and user data follow the configured privacy level
- redaction is a backstop, not permission to pass secrets through unsafe APIs

Debug logging does not weaken credential or privacy rules.

Redaction occurs before a record reaches the console, persistent file,
in-memory editor store, event adapter, crash metadata, CI artifact, or
diagnostic bundle. See [Observability Architecture](../observability/observability.md) for the
canonical record and sink pipeline.

## Temporary Files

Release staging directories are private to the executing user or CI job.

Sensitive temporary files:

- use restrictive permissions at creation
- use unpredictable names
- are never created in the final publication directory
- are removed on success, failure, and cancellation
- are not uploaded as CI artifacts

Final artifacts are promoted atomically after verification. Partial or
unverified outputs are not exposed under a successful release path.

## Archive Protection

Protected `.horo` archives use authenticated encryption through an approved
AEAD construction such as AES-256-GCM or ChaCha20-Poly1305.

Required properties:

- a unique nonce for every encryption operation
- authenticated archive metadata and table of contents
- authenticated entry content
- password-based keys derived with a versioned, memory-hard KDF
- per-archive random salt and explicit KDF parameters
- constant-time authentication and key-check comparisons
- format versioning that permits algorithm and parameter upgrades

CRC values may be used for accidental corruption detection but are not security
controls.

Encryption without authentication, nonce reuse, hardcoded keys, and silent
downgrade to an unprotected archive are prohibited.

## Artifact Integrity And Signing

Every published artifact has a cryptographic hash recorded in the release
manifest and checksum file.

Release profiles define signing requirements for:

- executable binaries
- platform packages
- release manifests
- checksum metadata

Signing keys are stored outside the repository and build output. Signing
identity and verification metadata are recorded without embedding private
material.

The publication process verifies signatures and hashes after upload. An
artifact is not considered published until post-upload verification succeeds.

## Platform Signing, Notarization, And Entitlements

Platform signing is part of the release security boundary.

Release profiles may require:

- Windows Authenticode signing with trusted timestamping
- macOS code signing with hardened runtime
- macOS notarization and stapling where applicable
- platform-specific entitlements
- Linux package signing where a package manager or repository requires it
- store-specific signing or upload credentials

Signing verification records:

- signing identity
- certificate chain metadata
- timestamp authority result where applicable
- notarization ticket identity where applicable
- entitlement set hash
- verification tool version and result

A signed artifact is not considered releasable until platform verification
succeeds after packaging. Entitlements are declared by profile and reviewed as
part of release security; build scripts cannot add entitlements implicitly.

## Supply Chain Metadata

Release jobs may produce supply-chain metadata alongside artifacts:

- SBOM for third-party dependencies and packaged components
- source revision and build provenance
- dependency lock identity
- build environment identity
- toolchain identity
- artifact attestation
- license and notice inventory

Supply-chain metadata is generated from verified build inputs, not from
post-hoc manual notes. It is attached to the release record and may be published
with public releases depending on product policy.

A release profile declares whether SBOM and provenance are required, optional,
or internal-only. Missing required supply-chain metadata fails the release before
publication.

## CI Security

CI follows least privilege:

- default job permissions are read-only
- write and release permissions exist only in publication jobs
- secrets are unavailable to untrusted fork jobs
- protected environments gate signing and publication
- release jobs require approved branches, tags, and repository state
- concurrent publication of the same release identity is serialized

Third-party actions and build dependencies are pinned to immutable commit
identities. Their permissions and network requirements are reviewed.

Build, signing, and publication are separate jobs or trust stages. Unsigned
build artifacts cannot replace signed artifacts after verification.

## MCP Security

MCP is exposed by both GUI and CLI hosts and is subject to the same release
authorization rules as direct host actions.

Required controls:

- local transports rely on operating-system process and user boundaries
- network transports require authenticated encryption
- remote clients are authenticated and authorized
- release, signing, and publication tools use explicit capabilities
- destructive or externally visible operations support confirmation policy
- request payloads do not carry reusable raw secrets when a credential handle
  can be used
- requests and results have bounded size and execution time

MCP transport threads do not directly mutate release, project, editor, or
runtime state. Requests cross into application services through validated
commands and owning-thread queues.

## Cache Security

Compiler, dependency, and generated-artifact caches are untrusted performance
inputs.

Cache keys include:

- platform and architecture
- compiler and toolchain identity
- build configuration
- dependency lock identity
- relevant build scripts and feature flags
- cache schema version

Protected release jobs do not consume writable caches from untrusted branches.
Cache restoration never bypasses compiler, linker, manifest, signature, or
artifact verification.

Dependencies are pinned to immutable identities and verified before use.

## Publication Security

Publication destinations use authenticated, encrypted transports.

The publisher:

- applies an explicit overwrite policy
- prevents path traversal and destination escape
- uploads only files declared by the verified manifest
- verifies remote size, hash, and signature after upload
- records immutable publication identity and audit metadata
- never publishes symbol files or logs through a public profile unless
  explicitly configured

Release promotion is separate from artifact construction. A valid local
artifact does not imply authorization to publish it.

## Required Verification

Security validation includes:

- command and path injection tests
- secret redaction tests
- wrong-key and tamper-detection archive tests
- nonce uniqueness and KDF parameter tests
- manifest and signature verification tests
- interrupted staging and cleanup tests
- unauthorized MCP release and publication tests
- CI permission and fork-secret checks
- dependency pinning checks
- post-upload hash and signature verification
- macOS signing/notarization verification failure
- Windows signing timestamp verification failure
- signing key rotation and revoked-key rejection
- SBOM/provenance generation and missing-required-metadata failure

Security-sensitive failures are fail-closed. Missing credentials, unsupported
algorithms, invalid signatures, failed authentication, and incomplete
verification stop the release.

## Related Documents

- [Application Security](../security/application-security.md): running editor/CLI trust,
  plugin, project, path, process, and MCP policy.
- [Distribution And Update](./distribution-and-update.md): signed update
  manifests, package verification, activation, and rollback.
- [Release Architecture](./release.md): authoritative release jobs and
  artifacts.
- [Observability Architecture](../observability/observability.md): redaction, retention, and
  diagnostic bundles.
