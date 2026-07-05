# ADR-002: Credential Handling Model

- **Status**: Accepted
- **Date**: 2026-05-25
- **JIRA**: HORO-32
- **Supersedes**: None
- **Scope**: Secrets, signing keys, and archive passwords in the build/release pipeline

## Context

Horo Engine's release pipeline involves several categories of secrets that
must be managed differently based on their lifecycle and exposure surface:

| Secret category | Lifecycle | Example |
|-----------------|-----------|---------|
| CI service credentials | Long-lived, per-service | `SONAR_TOKEN`, `RELEASE_PLEASE_TOKEN` |
| Code-signing certificates | Long-lived, per-identity | Windows `.pfx`, Apple Developer ID certificate |
| Code-signing certificate passwords | Paired with certificates | Password protecting the `.pfx` file |
| Release archive encryption keys | Per-release or per-project | Password used to encrypt `assets.horo` |
| Apple notarization credentials | Per-release | Apple ID + app-specific password + Team ID |

The engine has two distinct domains where secrets touch the pipeline:

1. **CI/CD environment** (GitHub Actions): Secrets are injected as
   `${{ secrets.X }}` into workflow steps. They are masked in logs and
   scoped to the repository or organization.

2. **Editor UI / local builds** (developer workstation): Secrets (archive
   passwords, signing configuration) are entered through the Build Pipeline
   Modal UI. They are held in the `BuildPipelineDraft` struct in memory only
   and are never persisted to disk in plaintext.

The current implementation has a concrete credential path: the
`HORO_RELEASE_ARCHIVE_PASSWORD` environment variable, which is read by the
shell command constructed by `core/pipeline/ReleasePipeline` and passed to
`horopak pack --password`.  The `BuildPipelineDraft` struct carries an
`archivePassword` string field for the editor UI path, and the
`SigningConfig` struct carries certificate passwords similarly.

## Decision

**Secrets are never stored in the repository, never persisted to disk in
plaintext, and follow distinct models for CI vs. local/editor use.**

### Tier 1: CI-managed secrets (GitHub Actions secrets)

Credentials that the CI pipeline itself needs (`SONAR_TOKEN`,
`RELEASE_PLEASE_TOKEN`, code-signing certificates, notarization credentials)
are stored as GitHub Actions encrypted secrets.  They are:

- Injected into workflow steps as environment variables or action inputs.
- Masked in workflow logs automatically by GitHub.
- Never present in the repository source tree.
- Rotated through the GitHub UI or API, not through code changes.

**Current state**: `SONAR_TOKEN` is the only CI secret in active use. The
release binaries workflow uses `github.token` (auto-generated, short-lived)
for `gh release upload`.  Code-signing and notarization are designed in the
`SigningConfig` struct but not yet wired to CI — this is deferred work.

### Tier 2: Build-time secrets (archive encryption passwords)

Archive encryption passwords (`HORO_RELEASE_ARCHIVE_PASSWORD`) follow a
principle of *short-lived, in-memory, user-supplied*:

- **Editor UI path**: The password is typed by the developer into the Build
  Pipeline Modal. It lives in `BuildPipelineDraft::archivePassword` (a
  `std::string` in process memory). It is passed to the build subprocess via
  environment variable. When the modal is closed, the draft is destroyed and
  the password is gone.

- **CLI path**: `horo-engine project release --archive-password <value>`.
  The password is passed as a command-line argument (visible in the process
  list on most platforms — a known limitation documented in the CLI
  reference).  The CLI sets `HORO_RELEASE_ARCHIVE_PASSWORD` before spawning
  child processes.

- **CI path** (future): Archive passwords for distribution builds will be
  stored as GitHub Actions secrets and injected into the release workflow.
  This is not yet implemented.

**Critical invariant**: The archive password is used to derive an AES-128-CTR
key via the `CryptoProvider` interface (`ICryptoProvider::SetKey`).  The key
material is held in the `horopak` process memory.  The `.horo` archive file
does NOT store the password or key — wrong/missing keys cause decryption to
fail rather than falling back to plaintext.

### Tier 3: Code-signing credentials (SigningConfig)

The `SigningConfig` struct (`core/pipeline/ReleasePipeline.h`) models the
full signing configuration:

| Field | Storage | Notes |
|-------|---------|-------|
| `certificatePath` | Filesystem path (configurable) | Points to `.pfx` / `.p12` file |
| `certificatePassword` | In-memory `std::string` only | Never serialized to history JSON |
| `appleId` | In-memory `std::string` only | Apple ID for notarization |
| `teamId` | In-memory `std::string` only | Apple Team ID |
| `keychainProfile` | In-memory `std::string` only | `notarytool` keychain profile name |

The sign command (`SignCommandForJob`) constructs a shell command with
the certificate password inline. This is acceptable for local developer
workstations where the process list is trusted. For CI, the signing
credentials would be GitHub Actions secrets and the command would be
constructed differently (using `notarytool` keychain profiles on macOS,
or Azure Key Vault / hardware security modules on Windows).

**Current state**: Signing is designed but not yet wired to CI. The
editor UI exposes the signing configuration modal; the command generation
produces correct shell commands; but end-to-end CI signing is deferred.

### What is NOT persisted to build history

The `BuildHistoryEntry` struct (serialized to `~/.horo/build_history.json`)
deliberately excludes:
- `archivePassword`
- `SigningConfig::certificatePassword`
- `SigningConfig::appleId`
- `SigningConfig::teamId`

The history JSON only contains build metadata (version, timestamp, job
statuses, output paths). This is enforced by `JobToJson` serialization: it
only serializes `BuildJob` fields, not `BuildPipelineDraft` secrets.

## Consequences

### Positive
- Clear separation between CI secrets (stored in GitHub), local secrets
  (held in memory only), and non-secret build metadata (persisted to history
  JSON).
- Archive encryption has no recovery path for lost passwords — this is
  intentional: the archive format does not embed key material.
- The `CryptoProvider` interface does not expose key bytes; it only exposes a
  `SetKey` method and a `GenerateKcv` method for password verification.

### Negative
- CLI `--archive-password` is visible in `ps` output on most platforms.
  Mitigation: the CLI documentation notes this limitation and recommends
  `HORO_RELEASE_ARCHIVE_PASSWORD` for CI/scripted use.
- The editor UI passphrase field is currently a plain `ImGui::InputText`,
  not a password-masked input. This is a known UX gap.
- Signing configuration is typed but the password fields are plain
  `std::string` with no secure zeroing after use. This is acceptable for
  developer workstations but would need hardening for CI use.

### Open questions
- Should the archive password be passed via a file descriptor or named pipe
  instead of command-line argument or environment variable?
- Should `BuildPipelineDraft::archivePassword` use a type that zeroes memory
  on destruction (e.g. `std::vector<uint8_t>` with explicit wipe)?
- For CI signing, should we integrate with a secrets manager (Azure Key
  Vault, HashiCorp Vault) or rely on GitHub Actions encrypted secrets?

## Rejected Alternatives

### Store credentials in a config file in the project repository

**Rejected because**: This is an antipattern.  Credentials committed to
version control leak to every clone, fork, and CI log.  Even encrypted
config files stored in the repo create key-management problems and false
security.

### Derive archive key from a hardcoded constant

**Rejected because**: A hardcoded key provides no real security — it is
trivially extractable from the engine binary.  The `.horo` format explicitly
requires external key management.

### Use platform keychains for all secrets (macOS Keychain, Windows
Credential Manager)

**Rejected for now, not ruled out**: Platform keychains would improve the
local development experience (no plaintext passwords in `ps`).  However, they
introduce platform-specific code paths in the credential layer, complicate CI
usage (keychains are user-session-scoped), and don't solve the problem of
passing the credential to child processes.  This is deferred for future
evaluation.

### Omit encryption entirely and rely on obfuscation

**Rejected because**: Obfuscation is not security.  The `.horo` format was
designed with AES-128-CTR encryption as a first-class feature.  Removing it
would regress the archive contract and force game developers to implement
their own asset protection, likely badly.
