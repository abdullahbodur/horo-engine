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

## How To Work In This Repo

- Inspect the real implementation before changing it.
- Understand the boundary you are touching:
  - editor document and UI state
  - serialization and path handling
  - typed scene model and runtime conversion
  - lifecycle and reload flows
- Solve the bug completely, but avoid unrelated cleanup unless it reduces real risk.
- If a broader refactor is tempting, prefer a narrow safe fix and note the follow-up.
- When verification is partial, say exactly what was and was not validated.

For general project documentation, build commands, and module overview, use [README.md](README.md).

## High-Risk Areas

- `editor/`: scene mutations, drag/drop flows, asset operations, selection state, live editing
- `scene/`: typed scene model, conversion pipeline, lifecycle coordination, reference runtime behavior
- `core/ProjectPath`: cross-platform path handling and temp/project-root behavior
- Serialization, reload, and file-deletion paths are easy places to create silent data loss or cross-platform regressions

Changes in these areas should bias toward explicit validation and regression tests.

## Verification Bar

- Every behavior change should add or update regression coverage when practical.
- Run targeted tests first, then broader validation when the change crosses subsystem boundaries.
- Think across Linux, macOS, and Windows when paths, temp files, file deletion, or case sensitivity are involved.
- Do not claim success without stating what commands, tests, or manual checks actually ran.

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

Use [`.github/pull_request_template.md`](.github/pull_request_template.md) for PR structure.

## References

- General repo and build documentation: [README.md](README.md)
- PR structure: [`.github/pull_request_template.md`](.github/pull_request_template.md)
