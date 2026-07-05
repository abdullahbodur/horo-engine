# Release Architecture

## Purpose

The release system transforms a Horo project into a verifiable,
platform-specific distribution artifact. Release behavior belongs to shared
application and pipeline services and is available through:

- the Horo GUI
- the Horo CLI
- MCP in both hosts
- CI automation

All entry points submit the same typed release request and observe the same job
model. GUI, CLI, MCP, and CI do not implement separate build or packaging
pipelines.

## Repository Release Policy

Horo Engine releases are maintainer-driven. The repository does not use any
workflow that automatically opens release PRs, bumps versions, writes changelogs,
creates tags, or publishes GitHub Releases.

The release decision is manual:

1. A maintainer chooses the release version.
2. The maintainer updates `HORO_ENGINE_VERSION` in `CMakeLists.txt` and any
   release notes or changelog content that should ship with the release.
3. Normal CI must pass on the selected commit.
4. The maintainer creates an explicit signed or annotated tag, for example
   `v0.3.0`, and creates the GitHub Release manually.
5. The `Release Binaries` workflow reacts to the published GitHub Release and
   uploads platform artifacts. The same workflow may be dispatched manually with
   an existing tag as a repair path.

This mirrors the release posture used by projects such as Hermes Agent and
OpenClaw: automation validates, builds, signs, and uploads artifacts, but it does
not decide that a release should exist. Manual maintainer intent is the gate.

The release binaries workflow must treat the tag as immutable release input. If
a release is wrong, prefer a new corrective tag over mutating already-published
artifacts unless the maintainer is deliberately repairing an unpublished or
failed release attempt.

## Product Release Profiles

A release profile defines what kind of product is being produced. Horo supports
at least these product kinds:

- `engine-editor`: HoroEditor desktop distribution.
- `engine-cli`: `horo-engine` and `horopak` command-line packages.
- `sdk`: public development SDK and headers.
- `game-runtime`: packaged playable game.
- `game-dedicated-server`: optional headless server package.
- `developer-diagnostics`: symbols, logs, debug metadata, and validation reports.

Each profile declares:

- included executables and runtime libraries
- asset package/chunk policy
- platform package format
- signing and notarization requirements
- symbol and crash-report artifact policy
- license and notices inclusion
- update and patch eligibility
- store/publication destination eligibility

Editor and CLI releases may use Horo-owned update channels. Packaged games own
their product identity, version, update channel, store integration, privacy
policy, and patch policy.

## Service Model

```text
GUI       CLI       MCP       CI
 \         |         |        /
  +-------- ReleaseService ---+
                  |
          ReleasePipeline
                  |
   validate -> build -> cook -> package
          -> verify -> sign -> publish
```

`ReleaseService` owns use-case validation, job lifecycle, cancellation,
progress, and structured results.

`ReleasePipeline` owns deterministic execution of release stages. It does not
depend on ImGui, terminal formatting, MCP transport, or CI-provider APIs.

In the editor, `BuildReleaseModal` is the GUI adapter. It is hosted by
`EditorModalHost`, owns exclusive editor focus while open, and submits typed
requests to `ReleaseService`. The modal is not the release-job owner.

Closing `BuildReleaseModal` does not destroy or implicitly cancel a submitted
job. The user explicitly chooses whether a supported running job continues in
the background or receives a cancellation request. Reopening the modal queries
active and recent jobs from `ReleaseService`.

Host adapters own only:

- collecting and validating protocol-level input
- presenting progress and diagnostics
- translating the final typed result
- requesting cancellation

## Release Request

A release request contains:

- project root and project identity
- semantic version
- target operating system and architecture
- build configuration
- toolchain profile
- output root
- content and feature selection
- signing profile reference
- archive protection profile reference
- reproducibility and diagnostics options

Secrets are referenced through credential handles. Passwords, private keys, and
tokens are not stored directly in release request objects or persistent job
history.

## Pipeline Stages

Every release follows an explicit state machine:

```text
Pending
  -> Validating
  -> Configuring
  -> Building
  -> Cooking
  -> Packaging
  -> Verifying
  -> Signing
  -> Publishing
  -> Succeeded
```

Any active stage may transition to `Failed` or `Cancelled`.

Stage contracts:

1. `Validating` verifies the project, target, toolchain, credentials, output
   policy, and required capabilities.
2. `Configuring` generates the target-specific build configuration.
3. `Building` compiles and links project binaries.
4. `Cooking` converts source assets into runtime-ready target artifacts.
5. `Packaging` assembles the distribution and creates `assets.horo`.
6. `Verifying` validates manifest contents, checksums, archive readability, and
   launch prerequisites.
7. `Signing` signs supported binaries and distribution metadata.
8. `Publishing` transfers verified artifacts to a configured destination.

Stages publish typed progress events and structured diagnostics. A failed stage
does not silently continue into later stages.

## Build & Release Modal Design

`BuildReleaseModal` is hosted by `EditorModalHost` and presents the release job
as a three-pane workspace:

- left sidebar: product profile, target platform/architecture/configuration, and
  security settings
- top stage track: Validate → Configure → Build → Cook → Package → Verify → Sign → Publish
- main area: summary bar and structured per-stage log panel

The modal owns exclusive editor focus while a job is active. Closing the modal
does not cancel a running job; cancellation is explicit.

[Build & Release Modal reference design](./release-modal-design.html)

## Target Model

A release job produces one operating-system, architecture, and configuration
combination.

Local GUI and CLI invocations may execute one or more jobs, but each job remains
independent and validates its own toolchain. Cross-compilation is allowed only
when an explicit compatible toolchain profile is configured.

CI creates a matrix of independent release jobs and aggregates their verified
outputs. A failed target cannot be represented as a successful multi-platform
release.

## Output Layout

Release output uses a predictable layout:

```text
<output-root>/
  <version>_<platform>_<architecture>_<configuration>/
    bin/
    assets.horo
    manifest.json
    checksums.txt
    notices/
    symbols/             optional, access-controlled
    logs/
```

The distribution contains runtime artifacts only. Authoring sources such as raw
scene documents, source textures, source models, editor metadata, and temporary
build files are not included unless an explicit developer package profile
requires them.

Output is assembled in a private staging directory and atomically promoted to
its final path only after verification succeeds.

## Artifact Manifest

Every release contains a versioned machine-readable manifest describing:

- engine and project versions
- target platform, architecture, and configuration
- toolchain identity
- build and source identity
- enabled runtime features
- artifact paths, sizes, and cryptographic hashes
- asset archive format version
- signing identity and signature metadata
- reproducibility metadata

The manifest uses a canonical serialization format before hashing or signing.
Unknown required manifest fields cause validation failure. Optional extensions
are namespaced and versioned.

## Runtime Compatibility Contract

A game release declares runtime compatibility separately from editor project
format compatibility.

The release manifest may include:

- `saveFormatVersion`
- `minimumReadableSaveFormatVersion`
- `networkProtocolVersion`
- `modApiVersion`
- `assetArchiveFormatVersion`
- `requiredEngineRuntimeVersion`
- `requiredPluginApiVersion`

A game update must not silently make existing user saves unreadable. If a save
migration is required, the game release profile declares whether migration is:

- automatic and reversible
- automatic but one-way
- explicit user-confirmed
- unsupported

Network protocol incompatibility must be represented explicitly so multiplayer
clients, dedicated servers, and tools can reject incompatible connections with a
typed diagnostic instead of failing at runtime.

Mod and plugin compatibility is checked before activation or launch. Unknown or
incompatible mods are disabled, quarantined, or require explicit user action
according to the game product policy.

## Asset Packaging

Release assets are addressed by stable logical asset identifiers and packaged
into `assets.horo`.

The archive provides:

- a versioned header
- a deterministic table of contents
- per-entry type and size metadata
- compression metadata
- cryptographic integrity
- optional authenticated encryption
- explicit format and feature compatibility information

Release runtime code accesses content through an asset-provider contract:

```text
logical asset request
        |
    AssetProvider
      /       \
filesystem   archive
development  release
```

Scene, mesh, texture, shader, material, and other runtime loaders do not assume
that release assets exist as raw filesystem files.

Wrong credentials, unsupported archive versions, missing entries, and failed
integrity checks produce explicit errors. The runtime never falls back from a
protected release archive to unprotected authoring files.

## Reproducibility

Release jobs normalize inputs that otherwise vary between machines:

- file ordering
- archive entry ordering
- timestamps included in deterministic content
- locale and timezone
- generated metadata serialization
- toolchain and dependency identity

Given identical declared inputs and a reproducible toolchain, release content
hashes should be identical. Non-deterministic platform signing metadata is
tracked separately from reproducible unsigned content.

## Job History And Logs

Persistent job history contains:

- public request metadata
- target and stage results
- timestamps and duration
- artifact and log locations
- structured diagnostics
- manifest identity

History never contains credentials or raw secret values.

Logs are separated by release job and stage. Log records include timestamp,
severity, subsystem, target, and stage. User-facing adapters may render logs
differently but do not alter the underlying diagnostic identity.

Release records use the common structured schema and carry
`release.job_id`, `release.stage`, and operation context. Job logs are queryable
through the release service; the global process log stores lifecycle summaries
and references instead of duplicating unbounded child-process output.

The authoritative live copy is stored in the release-job log root managed by
`ReleaseService`. The output layout's `logs/` directory is an optional bounded
export or summary for the producer/operator. It is excluded from the
customer-facing game package unless an explicit developer-diagnostics profile
permits it.

## Cancellation And Recovery

Cancellation is cooperative and checked at every stage boundary and during
long-running operations.

On cancellation or failure:

- child processes are terminated through the platform process service
- temporary outputs are removed or quarantined
- the final artifact path is not published
- completed logs and diagnostics remain available
- credentials and sensitive in-memory buffers are released

Publishing and final output promotion are idempotent or protected by an explicit
conflict policy. Existing artifacts are never overwritten implicitly.

## Verification

A release succeeds only when:

- all required stages complete
- the manifest matches the produced files
- cryptographic hashes verify
- `assets.horo` can be opened and required runtime entries can be read
- no forbidden authoring assets are present
- signing requirements for the selected profile are satisfied
- the packaged executable passes the configured smoke test

Packaged artifacts are the objects tested and published. CI does not publish
artifacts that bypass release verification.

Additional required tests cover:

- platform package format selection
- save-format compatibility warning and migration policy
- network protocol mismatch rejection
- mod/plugin API compatibility rejection
- release candidate promotion without rebuilding artifacts
- SBOM/provenance generation and missing-required-metadata failure
- user settings/cache migration after product update

## Release Candidate And Promotion

Artifact construction and channel promotion are separate operations.

A release candidate is a verified immutable set of artifacts and metadata. It
may be promoted to one or more channels only after policy checks pass:

- required targets succeeded
- required signatures and notarization passed
- smoke tests passed on packaged artifacts
- release notes are attached
- compatibility report is available
- required approvals are present
- publication destination is authorized

Promotion never rebuilds artifacts. It moves or references an already verified
release candidate. If a promotion fails, the candidate remains valid but the
channel state is unchanged.

## Security

Release credentials, encryption, signing, CI trust, transport, logging, and
artifact integrity follow [Release Security](./release-security.md).

## Related Documents

- [Build Output UI Reference](../runtime/build-output.html)

- [Editor Modal Host](../editor/editor-modal-host.md): Build & Release presentation,
  focus, close policy, and job reconnection.
- [Release Modal Design](./release-modal-design.html): HTML reference design for the
  `BuildReleaseModal` workflow surface.
- [Engine Data Bus](../foundation/engine-data-bus.md): release/build lifecycle
  notifications.
- [Release Security](./release-security.md): signing and credential boundaries.
- [Distribution And Update](./distribution-and-update.md): installation
  packages, update manifests, activation, and rollback.
- [Horo Package System](../packages/package-system.md): package lockfile freeze and
  content package to release chunk mapping.
- [Observability Architecture](../observability/observability.md): structured records, job
  context, persistent storage, and diagnostic bundles.
