# AGENTS.md

This file is the working contract for coding agents in `horo-engine`.

## Role

Default posture: make careful, production-grade C++ changes for a real engine codebase.

Optimize for, in order:
- correctness
- root-cause fixes
- ownership and lifetime safety
- architecture consistency
- regression prevention
- delivery speed

## Repository Status And Sources Of Truth

Horo Engine is an active-development C++20 engine and editor IDE. Until a stable
release line exists, APIs may evolve, but changes must still be deliberate,
reviewable, and migration-aware.

Use this precedence when guidance conflicts:

1. the user's explicit instruction for the current task
2. this `AGENTS.md` working contract
3. normative documents under `docs/architecture/`
4. accepted ADRs under `docs/adr/`
5. the current implementation and tests
6. aspirational examples or deprecated code

Important repository boundaries:

- `include/Horo/` is the public API surface. Keep it narrow, backend-neutral, and
  stable unless the task explicitly requires an API change.
- `src/` is the active implementation tree.
- `apps/` owns executable composition and process entry points.
- `tests/` owns executable regression and contract coverage.
- `docs/architecture/` is normative: implementations must follow its dependency,
  ownership, lifecycle, and capability contracts.
- `deprecated/` is migration reference only. Do not compile, repair, or extend it
  unless the task explicitly targets migration from that tree.
- `docs/architecture/desired-project-tree.md` describes the target layout; verify
  that a path exists before treating it as implemented.

`CLAUDE.md` intentionally delegates to this file. Do not duplicate repository
rules into agent-specific instruction files.

## C++ Expectations

- Prefer simple, explicit code over clever or overly generic abstractions.
- Make ownership obvious. Favor RAII, value semantics, and clear lifetime boundaries.
- Keep state transitions explicit, especially in editor, scene loading, and runtime lifecycle code.
- Avoid hidden global behavior and side effects.
- Do not introduce new heap allocation, virtual dispatch, or template complexity unless it clearly improves the design.
- Prefer narrow interfaces and stable headers.
- When the code already has a typed model, do not push behavior back into stringly-typed maps or ad hoc property parsing.
- Preserve invariants first; cleanup and elegance come second.

## C++ Design And Coding Style

- Follow the existing local style and `clang-format` output. Do not do style-only rewrites.
- Keep functions focused and easy to reason about. Break up long functions when it improves clarity, not just to move lines around.
- Prefer explicit types where they improve readability; use `auto` when the type is obvious from the right-hand side.
- Use `const` aggressively for inputs and helper variables that should not change.
- Prefer `enum class`, small structs, and plain data carriers over loose flag combinations and magic strings.
- Minimize macro usage.
- Comments should explain invariants, ownership, or non-obvious intent. Do not add comments that restate the code.
- Headers should expose the minimum necessary surface. Prefer forward declarations when they keep dependencies cleaner.

## Architecture And Dependency Discipline

- Read `docs/architecture/README.md` and the owning subsystem document before a
  cross-module or public-contract change.
- Respect dependency direction from
  `docs/architecture/foundation/system-design.md`; do not solve local convenience
  by introducing a reverse dependency.
- Keep composition in host/application boundaries. Feature code must not discover,
  instantiate, or select concrete platform/render backends.
- Prefer typed configuration, IDs, capabilities, results, commands, and events
  over string comparisons, loosely structured maps, or scattered booleans.
- Preserve error information across layers. Translate errors only at a boundary
  that can add actionable context or present them to a user.
- Treat cancellation, shutdown, rollback, and partial initialization as normal
  lifecycle paths, not exceptional cleanup added after the happy path.
- Breaking a public contract requires a written reason, affected callers, a
  migration path, and regression coverage. Do not silently preserve two competing
  sources of truth as a compatibility workaround.

## Performance And Concurrency

- Establish whether a path is frame-hot, load-time, background, or tooling-only
  before choosing an abstraction.
- Avoid per-frame heap allocation, unbounded work, blocking I/O, and accidental
  CPU/GPU synchronization in rendering and editor draw paths.
- Keep data layout and iteration order visible in performance-sensitive code.
  Prefer contiguous data and stable handles where ownership permits.
- Do not add mutexes or atomics without documenting the protected invariant,
  ownership, allowed threads, and shutdown behavior.
- Job-system work must have explicit cancellation, completion, error propagation,
  and lifetime ownership. Never capture references whose lifetime is shorter than
  the scheduled work.
- Performance claims require measurements. State the workload, build mode,
  platform/backend, metric, and before/after result.

## Documentation (Doxygen)

Single source of truth: the **header declaration** owns the full contract (`@brief`, `@param`, `@return`, `@throws`, pre/postconditions). Definitions in `.cpp` stay thin.

Rules:

- **Header declarations** — document public API and types with full Doxygen (`@brief`, `@param`, `@return`). Use `@file` + `@brief` at the top of headers that form the documented surface.
- **`.cpp` definitions** — use `/** @copydoc SymbolName */` above out-of-line definitions. Do not duplicate parameter/return blocks (drift risk).
- **`@copydoc` naming** — free functions in the same namespace: simple name (`@copydoc MakeObjectFromAsset`). Class members: `ClassName::member` (`@copydoc EditorLayer::OnMenuSave`). Qualify further only when Doxygen cannot disambiguate overloads.
- **File-local helpers** — symbols in anonymous namespaces or `static` functions with no header declaration: document at the definition in `.cpp` with `@brief` (and `@param`/`@return` only when behavior is non-obvious).
- **Types** — `@brief` on the type; document non-obvious members with `/**< ... */` trailing briefs.
- **Do not** maintain parallel full doc blocks on both declaration and definition for the same symbol.
- **Do not** use `@copydoc` for symbols that exist only in `.cpp` with no header forward declaration.

Exemplar pairs: `EditorPropertyRules.h`/`.cpp`, `Raycaster.h`/`.cpp`, `EditorLayer.h` + split `.cpp` files.

## How To Work In This Repo

- Inspect the real implementation before changing it.
- Check `git status` before editing. The worktree may contain substantial user
  changes; do not overwrite, unstage, reformat, or fold them into the task.
- Trace a symbol through its declaration, definition, callers, tests, and owning
  architecture document before changing its contract.
- Understand the boundary you are touching:
  - editor document and UI state
  - serialization and path handling
  - typed scene model and runtime conversion
  - lifecycle and reload flows
- Solve the bug completely, but avoid unrelated cleanup unless it reduces real risk.
- If a broader refactor is tempting, prefer a narrow safe fix and note the follow-up.
- When verification is partial, say exactly what was and was not validated.
- Do not add a dependency until existing dependencies and neighboring code have
  been inspected. Pin fetched dependencies according to repository policy and
  keep native/backend dependencies inside their owning target.
- Do not commit generated build output, local caches, logs, screenshots, IDE state,
  credentials, or machine-specific paths.

For general project documentation, build commands, and module overview, use [README.md](README.md).

## High-Risk Areas

- `src/editor/`: document/UI state, modal and route lifecycle, drag/drop, selection,
  settings, localization, asset operations, and live editing
- `src/runtime/`: renderer lifecycle, backend registration, frame ordering, resource
  ownership, scene/runtime conversion, and shutdown
- `src/platform/`: cross-platform paths, files, process execution, window/native
  handles, temporary storage, and OS-specific behavior
- `include/Horo/`: public API and dependency fan-out
- project metadata, serialization, reload, cache invalidation, and file deletion;
  these paths can cause silent data loss or cross-platform regressions

Changes in these areas should bias toward explicit validation and regression tests.

## Editor UI And Localization

- Use shared controls and design tokens from the editor design system. Do not
  bypass a shared component with raw ImGui widgets when the control already exists.
- Native ImGui style and custom-drawn `Theme`/design-token surfaces are separate
  layers; a theme-sensitive change must remain correct in both.
- Avoid hard-coded horizontal offsets for text-adjacent actions. Calculate widths,
  constrain wrapping, and position controls from available bounds.
- Preserve minimum typography and existing font roles. Do not introduce a smaller
  visible text tier merely to make content fit.
- Creation and transient workflows belong in modals/routes, not document tabs.
  Tabs represent persistent editor document/workspace sections.
- New user-visible editor copy must use the localization service. Keep
  `assets/localization/editor/en-US.json` and `tr-TR.json` structurally aligned and
  update localization regression coverage.
- For visual changes, verify normal, hovered, active, disabled, focused, open-popup,
  narrow-width, and long/localized-text states when applicable.

## Renderer And GPU Rules

- OpenGL, Metal, Vulkan, and future interactive renderers are equal concrete peers
  behind Horo-owned typed contracts. Never make one backend the architectural base
  of another.
- Native API types and handles stay private to concrete backend/platform targets.
  Public/editor-facing contracts expose Horo types only.
- Backend selection occurs before window and presentation creation. Do not bolt a
  different scene renderer onto an editor GUI composition owned by another API.
- Keep renderer installation, host support, runtime availability, selection, and
  activation as distinct states. Do not silently fall back to another backend.
- Avoid normal-frame waits such as GPU idle or command-buffer completion. Explicit
  waits are reserved for bounded tests, readback, teardown, or documented recovery.
- GPU resource destruction must account for frames in flight and queued references.
  A CPU owner releasing a handle does not prove the GPU has finished using it.
- Any renderer contract change must consider OpenGL-only, Metal-only where
  supported, combined-backend, and headless/test compositions.

Read these before renderer work:

- `docs/architecture/runtime/rendering-architecture.md`
- `docs/architecture/runtime/render-backend-parity-contract.md`
- `docs/architecture/runtime/renderer-distribution-and-availability.md`

## Verification Bar

- Every behavior change should add or update regression coverage when practical.
- Run targeted tests first, then broader validation when the change crosses subsystem boundaries.
- Think across Linux, macOS, and Windows when paths, temp files, file deletion, or case sensitivity are involved.
- Do not claim success without stating what commands, tests, or manual checks actually ran.

Canonical local skeleton validation:

```bash
cmake -S . -B build/skeleton -DBUILD_TESTING=ON
cmake --build build/skeleton --parallel
ctest --test-dir build/skeleton --output-on-failure
```

Use `cmake --build build/skeleton --target <target>` and
`ctest --test-dir build/skeleton -R <test-regex> --output-on-failure` for the
targeted pass. Reconfigure after CMake options, dependencies, targets, source lists,
or platform guards change.

Additional gates by change type:

- public headers: build every affected consumer target
- CMake/target topology: configure every supported option combination affected
- renderer code: run backend contract tests and relevant GPU smoke tests when a
  compatible display/device is available
- editor UI: run render/model tests plus a manual interaction check when practical
- paths/files/processes: test spaces, non-ASCII names, missing inputs, permissions,
  and platform-specific behavior
- serialization/metadata: test malformed, duplicate, missing, oversized, and
  version-skewed inputs

`HORO_ENABLE_GPU_SMOKE_TESTS` is opt-in because it requires a display server and
hardware graphics context. Do not report those tests as passed unless they were
actually configured and run.

## Review Mindset

Default review posture:
- look for correctness bugs first
- check ownership, lifetime, and invalid state transitions
- look for missing tests and quiet behavior drift
- call out uncertainty instead of guessing
- do not overwrite user changes unless explicitly asked

When reviewing or preparing a PR, focus on:
- why the bug happened
- why the fix is safe
- which invariant is now protected
- what regression coverage was added

## Commits And PRs

Use Conventional Commits with a truthful narrow type and scope.

Preferred examples:
- `feat(editor): add asset viewport drag placement`
- `fix(core): handle empty ProjectPath init`
- `test(mcp): normalize deleted asset directory assertions`

Rules:
- write the subject in imperative mood
- keep one commit focused on one coherent intent
- avoid placeholder subjects such as `wip`, `misc`, `tmp`, `update`, or `fix stuff`

### Branch Naming

Branches must follow the pattern `<type>/<short_topic>`.

- `<type>` is the Conventional Commit type: `feat`, `fix`, `chore`, `docs`, `refactor`, `test`, `perf`, `ci`, `build`
- `<short_topic>` is a lowercase snake_case slug summarising the change

Examples:
- `feat/asset_guid_registry`
- `fix/gizmo_hidpi_picking`
- `chore/clang_tidy_pass`

Use one topic slug exactly once per branch; do not append suffixes such as `_v2`
or `_final`. If the scope changes materially, open a new focused branch.

Agents must not commit, push, force-push, rewrite history, merge, or change branches
unless the user explicitly requests that operation. Never discard unrelated
worktree changes to make a task easier.

Use [`.github/pull_request_template.md`](.github/pull_request_template.md) for PR structure.

## References

- General repo and build documentation: [README.md](README.md)
- Architecture index and reading order: [docs/architecture/README.md](docs/architecture/README.md)
- System dependency direction: [docs/architecture/foundation/system-design.md](docs/architecture/foundation/system-design.md)
- Ownership and lifetime: [docs/architecture/foundation/ownership-and-resource-lifetime.md](docs/architecture/foundation/ownership-and-resource-lifetime.md)
- Editor UI design system: [docs/architecture/editor/ui-design-system.md](docs/architecture/editor/ui-design-system.md)
- PR structure: [`.github/pull_request_template.md`](.github/pull_request_template.md)
