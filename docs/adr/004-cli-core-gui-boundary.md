# ADR-004: CLI / Core / GUI Boundary

- **Status**: Accepted
- **Date**: 2026-05-25
- **JIRA**: HORO-32
- **Supersedes**: None
- **Scope**: Module boundary between command-line interface, engine core pipeline, and editor UI

## Context

Horo Engine serves three distinct entry points for release/build workflows:

1. **Editor UI** (`HoroEditor`): The ImGui-based graphical editor, which
   exposes the Build Pipeline Modal for configuring and triggering builds.
2. **CLI** (`horo-engine`): The headless command-line tool for project
   creation, building, and releasing from a terminal or CI job.
3. **MCP** (`mcp/` transport): The Model Context Protocol server that
   exposes `project.release` and related tools to AI agents.

All three entry points must produce the same artifact shape, use the same
core pipeline logic, and honor the same invariants.  If the editor UI
packages assets differently than the CLI, downstream tooling breaks.  If MCP
bypasses the core pipeline and constructs its own commands, the release
output diverges.

The risk: editor UI code absorbs pipeline logic (command construction,
archive packaging, output path resolution) that should live in `core/`,
creating a situation where the CLI and MCP cannot match the editor's
behavior without duplicating code.

## Decision

**Release/build logic lives in `core/pipeline/`.  The editor UI, CLI, and
MCP are thin clients that collect user input and call into the same core
functions.**

### The boundary

```
┌──────────────────────────────────────────────────┐
│  Editor UI (ui/editor/)                           │
│  - Build Pipeline Modal                           │
│  - Collects user input → BuildPipelineDraft       │
│  - Calls core/pipeline/ to create command plans    │
│  - Spawns ExternalProcessRunner                   │
│  - Displays progress / history                    │
├──────────────────────────────────────────────────┤
│  CLI (horo-engine)                                │
│  - Parses command-line args                       │
│  - Calls core/pipeline/ to create command plans    │
│  - Spawns child process                           │
│  - Prints output / exits                          │
├──────────────────────────────────────────────────┤
│  MCP (mcp/)                                       │
│  - Receives tool call                             │
│  - Calls core/pipeline/ to plan                   │
│  - Starts async build via runner                  │
│  - Returns output path + status                   │
├──────────────────────────────────────────────────┤
│  core/pipeline/ReleasePipeline                    │
│  - BuildPipelineDraft (shared draft model)        │
│  - BuildJob, BuildHistoryEntry (shared data)      │
│  - CreateBuildCommandPlan() (command planning)    │
│  - ResolveJobOutputPath() (output naming)         │
│  - BuildProjectShellCommand() (shell construction)│
│  - BuildAssetArchiveShellCommand() (packaging)    │
│  - SigningConfig, CryptoProvider (credentials)    │
├──────────────────────────────────────────────────┤
│  core/archive/ (horopak)                          │
│  - Packager (create/read .horo archives)          │
│  - HoroFormat (binary layout, TOC, hashing)       │
│  - HashVerifier (CRC32, SHA-256)                  │
│  - CryptoProvider (AES-128-CTR)                   │
└──────────────────────────────────────────────────┘
```

### Concrete rules

1. **Command construction lives in `core/pipeline/`**:
   `CreateBuildCommandPlan()` and `BuildProjectShellCommand()` are in
   `ReleasePipeline.cpp`, not in the editor or CLI.  The editor and CLI call
   these functions and spawn the resulting process.  They do not construct
   their own shell commands.

2. **Output path resolution is centralized**:
   `ResolveJobOutputPath()` computes the version-tagged, platform-specific
   output directory.  The editor and CLI both call it.  Neither hardcodes
   the `v{version}_{os}_{arch}` convention.

3. **The archive packaging command (horopak) is constructed by core**:
   `BuildAssetArchiveShellCommand()` generates the `horopak pack` invocation
   with the correct compression level, project root, output path, and
   optional password.  The editor does not know how to spell `horopak pack
   --compression 9`.

4. **The editor UI owns presentation, not behavior**:
   - The Build Pipeline Modal collects user input into a
     `BuildPipelineDraft`.
   - It calls `core/pipeline/` functions to validate, plan, and execute.
   - It owns progress display, job status polling, error rendering, and
     history visualization.
   - It does NOT own archive format selection, hash algorithm choice, or
     release output naming.

5. **MCP uses the same core functions**:
   The `project.release` MCP tool handler constructs a
   `BuildPipelineDraft`, calls `CreateBuildCommandPlan()`, and dispatches
   the build.  It does not duplicate command construction or output path
   logic.

6. **The CLI is a thin parser over the core**:
   `horo-engine project release` parses `--version`, `--sdk-root`,
   `--archive-password`, constructs a draft, and calls the same core
   functions.  The CLI has no archive-specific logic.

7. **`horopak` is editor-free**:
   The `horopak` binary links only against `core/archive/` and has zero
   dependencies on ImGui, GLFW, or any editor code.  It can run in CI, in
   Docker containers, and on headless servers.

### Rejected patterns

- **Pipeline logic in `ui/editor/`**: Moving command construction into the
  editor would force the CLI and MCP to reimplement it.  This is the primary
  anti-pattern this ADR prevents.

- **Separate CLI and editor pipelines**: Having two implementations of
  `BuildAssetArchiveShellCommand()` (one in editor, one in CLI) would
  inevitably diverge.  The shared `core/pipeline/` layer prevents this.

- **Editor as the sole release trigger**: Requiring the editor to be open to
  produce a release would make CI/CD impossible.  The CLI and MCP must be
  first-class release triggers.

## Consequences

### Positive
- Any behavior change to the release pipeline (e.g. new archive compression
  algorithm, new output naming convention) is made in one place
  (`core/pipeline/`) and applies to all three entry points.
- `horopak` remains a small, self-contained binary suitable for CI and
  Docker use.
- The editor UI can evolve independently (new modal layouts, visual polish)
  without touching release behavior.
- Testing the release pipeline does not require launching the editor —
  headless tests can exercise `core/pipeline/` directly.

### Negative
- The shared `BuildPipelineDraft` struct must serve three different UX
  surface areas.  Fields that make sense in the editor (e.g.
  `contentSelection` preset index) may be unused by the CLI.  This is
  acceptable — unused fields are harmless and the alternative (three
  separate config structs) would cause drift.
- Adding a new release parameter requires touching the shared struct, the
  CLI argument parser, the editor modal, AND the MCP handler.  This is
  intentional — it forces the developer to consider all three entry points.
- The editor modal for the Build Pipeline (~1400 lines of ImGui code) is the
  largest single UI component.  Keeping it thin relative to `core/pipeline/`
  requires discipline in code review.

### Open questions
- Should the `ExternalProcessRunner` move from `ui/launcher/` to `core/` so
  the CLI can use it too? Currently the CLI spawns its own child processes
  directly.
- Should `BuildHistoryEntry` persistence move from `core/pipeline/` to a
  dedicated history service?

## Rejected Alternatives

### Editor-monolith approach

**Rejected because**: The engine's documented architecture commits to
headless repeatability: "The editor is only one way to trigger a release. A
release must also be repeatable from a terminal or future CI job using the
same engine-side pipeline steps." — `docs/architecture/release/release.md`.

### Each entry point constructs its own commands

**Rejected because**: This is a fork bomb waiting to happen.  Three
implementations of the same command construction logic would diverge within
weeks, creating platform-specific bugs that only manifest in one entry point.

### CLI wraps the editor in headless mode

**Rejected because**: This would require the editor binary and its GPU
context to be available in CI environments.  It would also be significantly
slower (editor startup time, ImGui frame overhead) for a simple release
command.
