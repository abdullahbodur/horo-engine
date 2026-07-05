# ADR-001: Host-Agnostic Local Release Pipeline

- **Status**: accepted
- **Date**: 2026-05-30
- **Deciders**: Abdullah Bodur (Horo Engine maintainer)
- **Supersedes**: prior ADR-001 (narrow native-CI wording)
- **Superseded by**: none

## Context

HORO-31 adds a build pipeline modal to the Horo Editor. The modal must let a user
configure, trigger, and monitor release builds. Before implementation can proceed,
the release workflow's architectural contract must be settled.

The prior ADR-001 drafted a native-CI hybrid that leaned on GitHub Actions
dispatch as the primary release mechanism. That direction was rejected as the
HORO-31 product path. CI is toolbox infrastructure, not the product surface.
An editor that sends release work to a remote service and then polls for results
is architecturally different from one that drives local toolchains.

Horo Engine ships on Windows, Linux, and macOS. The editor runs on all three.
Users on any host OS may need to target any release OS. Cross-compilation is
ubiquitous in game development: a macOS editor targeting a Windows Steam build,
a Linux CI node validating macOS packaging, a Windows workstation producing
Linux dedicated server builds.

This ADR defines the release workflow that HORO-31 implements. It replaces the
old native-CI framing entirely and becomes the single source of truth for the
editor release modal, the CLI release entry point, MCP release tools, and any
future CI automation that consumes the same pipeline.

## Decision

**Horo Engine's release pipeline is host-agnostic and local-first.** Any
Windows, Linux, or macOS editor host may target any Windows, Linux, or macOS
release operating system, provided the user configures, enables, and validates
the required toolchain.

The pipeline is:

1. **Editor-driven.** The primary trigger is the editor's Build & Release modal.
   CLI and MCP triggers use the same core pipeline APIs.
2. **Local toolchain.** Builds run on the host machine through user-configured
   native and cross-compilation toolchains. No remote service, no CI dispatch,
   no cloud build farm is required for the local release path.
3. **Toolchain-gated.** Before any build, the pipeline validates the toolchain
   for the chosen (host OS, target OS, architecture) combination. Validation
   failures block the build with actionable diagnostics.
4. **Post-build packaging.** After compilation, the pipeline copies build
   artifacts, packages assets into a `.horo` archive via `horopak`, optionally
   encrypts the archive, applies code signing when configured, and produces a
   predictable release folder.
5. **CI-compatible.** The same pipeline can run headlessly from a CI job.
   CI provides runtime validation on native targets (e.g., macOS ARM CI runs
   macOS ARM release tests) but CI is not the editor's primary release path.
   The editor never ships CI dispatch as a mainline workflow.

### Product Goal

> Any Windows, Linux, or macOS editor host may target any Windows, Linux, or
> macOS release operating system **only when** a user-configured toolchain is
> enabled and passes validation.

### Non-Goals for HORO-31

- Remote build farms or cloud CI dispatch surfaced in the editor
- Installer generation (`.dmg`, `.msi`, `.AppImage`, `.deb`)
- Steam / Itch.io / storefront upload integration
- Cross-compiler provisioning (the editor validates toolchains; it does not
  install them)
- GitHub Release creation or artifact upload
- Automatic toolchain detection and download
- Multi-config CMake generators that batch Debug + Release in one invocation

## Workflow Stages

```
1. Toolchain Configuration
   └─ User points the editor at native and cross-compilation toolchains
   └─ Per (host OS, target OS, architecture) tuple

2. Toolchain Validation
   └─ Pipeline runs a smoke compile for each configured (target, arch)
   └─ Failure → capability state updated; build blocked with diagnostics

3. Build Configuration
   └─ User selects target OS(es), architecture(s), build type, version tag
   └─ User optionally sets archive password and signing credentials

4. Build Execution
   └─ Sequential per-target: cmake configure → cmake build
   └─ Output captured to per-job log buffers
   └─ Progress reported to UI / CLI / MCP callers

5. Packaging
   └─ Copy artifacts to release output directory
   └─ Pack assets into assets.horo via horopak
   └─ Apply code signing when configured
   └─ Remove raw assets/ from release output

6. Verification (optional, CI gate)
   └─ Runtime smoke boot on target-capable hardware
   └─ Archive integrity check (no raw JSON, encryption active if requested)
```

### Stage Detail: Toolchain Validation

Validation runs a minimal CMake project that compiles a trivial C++ translation
unit for the target triple. Success means the toolchain can produce a binary
for that (target OS, architecture). Failure returns structured diagnostics:

- Compiler not found (missing toolchain binary)
- Compiler found but target unsupported (wrong triple)
- Compile succeeds but linker fails (missing sysroot / SDK)
- Compile + link succeeds

Each failure category maps to a user-facing capability state (see below).

Validation is re-run when:
- The user changes a toolchain path
- The user adds a new (target OS, architecture) combination
- The editor starts and previously-validated toolchains are still cached

## Target Capability Model

Each (host OS, target OS, architecture) tuple maps to one capability state.
The states are ordered by readiness — only the first three permit build execution.

| State | Icon | Meaning | Build Allowed? |
|---|---|---|---|
| `NativeAvailable` | ✓ | Host matches target; native toolchain found and validated | Yes |
| `CrossToolchainAvailable` | ⇄ | Host differs from target; cross-compilation toolchain configured and validated | Yes |
| `ToolchainDisabled` | ⊘ | Toolchain is configured and valid but user has disabled builds for this target | No |
| `ToolchainMissing` | ✗ | No toolchain configured for this (host, target, arch) tuple | No |
| `ToolchainInvalid` | ⚠ | Toolchain configured but validation failed; diagnostics available | No |
| `UnsupportedCombination` | ⊗ | The (host, target, arch) combination is architecturally unsupported (see platform notes) | No |

### State Transitions

```
ToolchainMissing ──[user configures]──→ ToolchainInvalid  ──[validation passes]──→ CrossToolchainAvailable
                                                                                    (or NativeAvailable)
CrossToolchainAvailable ──[user disables]──→ ToolchainDisabled
ToolchainDisabled       ──[user enables] ──→ CrossToolchainAvailable
ToolchainInvalid        ──[user fixes]  ──→ ToolchainInvalid (re-validate)
Any state               ──[user clears] ──→ ToolchainMissing
```

## Platform Matrix & Architecture Support

### Host × Target Combinations

| Host ↓ / Target → | Windows | Linux | macOS |
|---|---|---|---|
| **Windows** | NativeAvailable (x86_64) | CrossToolchainAvailable (x86_64) | UnsupportedCombination |
| **Linux** | CrossToolchainAvailable (x86_64) | NativeAvailable (x86_64) | UnsupportedCombination |
| **macOS** | CrossToolchainAvailable (x86_64) | CrossToolchainAvailable (x86_64) | NativeAvailable (arm64, x86_64) |

### Architecture Variants

- **Windows target**: x86_64 only for HORO-31. ARM64 Windows is deferred.
- **Linux target**: x86_64 only for HORO-31. ARM64 Linux is deferred.
- **macOS target**: arm64 (native on Apple Silicon) and x86_64 (native on Intel,
  cross-compiled from Apple Silicon). Universal binaries are deferred.

### Unsupported Combinations

- **macOS → Windows** is tentatively listed as `CrossToolchainAvailable` because
  MinGW-w64 and clang cross-compilation from macOS to Windows PE is established.
  It is not `UnsupportedCombination`, but validation must pass before it becomes
  `CrossToolchainAvailable`.

- **Non-macOS → macOS** is `UnsupportedCombination`. Apple's toolchain licensing
  and code-signing requirements mean macOS targets require a macOS host. This
  is not a Horo Engine limitation — it is an Apple platform constraint.

### OpenGL / GLFW Runtime Limits

Build capability is not the same as runtime verification.

- A Windows editor can cross-compile a Linux binary. Whether that binary renders
  correctly on the current host depends on GLFW context creation and GPU driver
  availability on the host — which, for a cross-OS target, is usually absent.
- A macOS editor can cross-compile a Windows binary. Running that binary for
  smoke testing requires a Windows machine or VM.
- Build success means the toolchain produced an executable. Runtime verification
  requires target-capable hardware.

The pipeline distinguishes:

| Capability | Meaning |
|---|---|
| **Buildable** | Toolchain validated; binary can be produced |
| **Runnable** | Host can execute the binary for smoke testing (same-OS builds only) |
| **Verifiable** | Host can run the binary AND OpenGL/GLFW context is available |

The editor modal shows buildable targets by default. Runnable/verifiable status
is advisory — it tells the user whether the current host can smoke-test the
output, but does not prevent the build.

## CI Relationship

CI is valuable infrastructure. It is not the product surface.

### CI Principles

1. **CI consumes the same pipeline APIs** as the editor, CLI, and MCP.
   No separate "CI mode" that diverges from the local release path.
2. **Native target validation in CI** is the primary mechanism for official
   runtime confidence. A macOS ARM64 CI runner can build AND smoke-test a
   macOS ARM64 release. A Linux x86_64 CI runner can build AND smoke-test
   a Linux x86_64 release.
3. **Cross-compilation in CI** is supported but produces `Buildable` artifacts,
   not `Verifiable` ones. CI can cross-compile Linux from macOS but cannot run
   the Linux binary.
4. **CI dispatch from the editor is removed.** The editor's Build & Release
   modal does not offer CI trigger buttons, GitHub Actions dispatch fields,
   repository tokens, or remote build status polling. These were removed in
   commit `971da47` as part of HORO-31 P0 cleanup.
5. **GitHub Release publishing is not the primary release path.** Creating
   GitHub Releases, uploading artifacts, and tagging — all remain available
   as CI automation that the project maintainer configures independently.
   The editor does not participate in this workflow.

### When CI Runs the Pipeline

The same `horo-engine project release` CLI command that the editor invokes
locally can run in CI:

```bash
horo-engine project release /path/to/project \
  --version 1.0.0 \
  --sdk-root /opt/horo-sdk \
  --archive-password "$RELEASE_SECRET"
```

CI wraps this in platform-specific runners to get native verification coverage.

## Consequences

### Positive

- **Honest product surface.** The editor's Build & Release modal does one thing:
  build release artifacts using the toolchains the user provides. No hidden
  network dependencies, no CI tokens, no "waiting for remote build" states.
- **Offline-capable.** A user with configured toolchains can produce release
  builds without internet access (beyond initial toolchain installation).
- **Deterministic.** Local builds don't suffer from CI queue delays, runner
  availability, or GitHub API rate limits.
- **Single pipeline.** Editor, CLI, MCP, and CI all call the same
  `core/pipeline/ReleasePipeline` implementation. One source of truth for
  build commands, packaging, and archive creation.
- **Clear capability model.** Users know exactly why a target is or isn't
  buildable through explicit validation states.

### Negative

- **Toolchain burden on the user.** Cross-compilation toolchains must be
  installed and configured by the user. The editor validates them but does
  not install them. This is more work than a CI-dispatched build where the
  CI runner has pre-installed toolchains.
- **No Windows → macOS or Linux → macOS.** Apple platform constraints make
  cross-compilation to macOS from non-macOS hosts unsupported. Users who
  need macOS releases must own a Mac.
- **Slower than parallel CI.** Local builds run sequentially per target.
  CI can parallelize across runners. This is an acceptable tradeoff for
  the local-first model; users who need CI parallelism can configure their
  own CI to call the same pipeline.

## Rejected Alternatives

### GitHub Release / CI Workflow Dispatch as Primary Path

**Rejected.** GitHub Actions dispatch as the primary release mechanism would
mean:

- The editor sends a `workflow_dispatch` event with build parameters
- A CI runner picks up the job, builds, and uploads artifacts to GitHub Releases
- The editor polls for completion and downloads artifacts

This was rejected because:

1. **Product coupling.** The editor would depend on GitHub's API availability,
   rate limits, and authentication model. A GitHub outage blocks local work.
2. **Latency.** CI queue time + build time + artifact upload + download is
   slower than a local build for a single target.
3. **Token management.** The editor would need to store and manage GitHub
   personal access tokens with `repo` and `workflow` scopes — a security
   surface that doesn't belong in a local editor.
4. **Offline blocker.** No internet = no release builds, even for the current
   host OS where a native toolchain is installed.
5. **Fragile abstraction.** The editor would need to model CI state machines
   (queued, in_progress, completed, failed, cancelled) that are GitHub-specific
   and don't generalize to other CI providers or local builds.

CI dispatch is legitimate infrastructure. It is not the editor's product
surface. The maintainer can configure GitHub Actions to call the same
`horo-engine project release` CLI as a CI automation — without the editor
needing to know about it.

### Docker / Container-Based Cross-Compilation

**Rejected for HORO-31 scope.** Docker containers could provide pre-configured
cross-compilation environments (e.g., a Linux container for Windows→Linux
cross-builds). This is a valid extension for a future ticket but adds Docker
as a runtime dependency, complicates filesystem mount paths, and introduces
platform-specific Docker Desktop behavior on macOS and Windows.

### Cloud Build Farm (e.g., AWS CodeBuild, Azure Pipelines)

**Rejected for HORO-31 scope.** A cloud build farm solves the "no macOS host"
problem but introduces billing, credential management, and network dependency.
It is a reasonable future extension for teams that need it, but the local-first
model must work first.

## Migration Impact

### From Prior ADR-001

Prior ADR-001 described a native-CI hybrid. The narrow "native CI" framing
is replaced by this ADR's host-agnostic local pipeline. No code artifact
existed for the old ADR — this is a documentation-only migration.

### From P0 Cleanup (971da47)

Commit `971da47` removed GitHub Release / CI surface from the editor build
pipeline modal. That removal is ratified by this ADR. The removed surfaces
included:

- GitHub Release control fields (repo, tag, token)
- CI workflow dispatch buttons and status indicators
- automated release PR configuration and generated release metadata
- Docstrings and error messages referencing GitHub Release as the primary path

No further removal is needed. Future work must not reintroduce these surfaces.

### For Implementors

Future HORO-31 tickets must conform to this ADR. Specifically:

- The editor modal must implement the target capability model (6 states).
- Toolchain validation must run before any build is allowed.
- No CI dispatch, GitHub token, or repository URL fields may appear in the
  editor UI.
- The `core/pipeline/ReleasePipeline` must be callable from editor, CLI, MCP,
  and CI without editor-specific coupling.
- The `docs/architecture/release/release.md` pipeline flow (cmake configure → cmake build
  → copy artifacts → horopak → signing → strip raw assets) remains the canonical
  build sequence.

## Follow-Up Work

| Ticket | Description | Priority |
|---|---|---|
| HORO-31 P2 | Implement toolchain validation service (smoke compile per target triple) | P1 |
| HORO-31 P3 | Implement target capability model UI in editor build pipeline modal | P1 |
| HORO-31 P4 | Wire local build execution through `ExternalProcessRunner` per target | P1 |
| HORO-31 P5 | Post-build packaging integration (horopak, code signing, archive encryption) | P2 |
| Future | Cross-compiler provisioning assistant (detect installed toolchains, suggest missing ones) | Future |
| Future | Docker-based cross-compilation targets (Linux container for Win→Linux, macOS→Linux) | Future |
| Future | Universal macOS binary (arm64 + x86_64 fat binary) | Future |
| Future | Windows ARM64 target support | Future |
| Future | Linux ARM64 target support | Future |

## References

- [Release pipeline architecture](../architecture/release/release.md) — canonical build
  sequence, archive contract, encryption rules, runtime asset provider
- [CLI reference](../cli.md) — `horo-engine project release` command
- [MCP reference](../mcp.md) — `project.release` and `editor.release_project` tools
- [Architecture overview](../architecture/README.md) — module boundaries,
  ownership, threading, error model
- P0 cleanup commit: `971da47` — removal of GitHub Release / CI surface from
  editor build pipeline modal
