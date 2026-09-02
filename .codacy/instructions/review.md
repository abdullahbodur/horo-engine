# Horo Engine PR Review Instructions

## Purpose

- Horo Engine is an actively developed C++20 game engine and AI-centric editor IDE.
- Prioritize correctness, ownership and lifetime safety, architecture consistency, and regression prevention.

## Architecture

- `include/Horo/` is the narrow, backend-neutral public API.
- `src/` contains engine implementations; `apps/` owns executable composition and process entry points.
- `tests/` contains unit, integration, editor/UI, and GPU smoke coverage.
- `docs/architecture/` is normative for dependency direction, ownership, lifecycle, capabilities, and subsystem contracts.
- `deprecated/` is migration reference only; do not extend or repair it unless the PR explicitly targets migration.

## Stack and conventions

- Use simple, explicit C++20 with RAII, value semantics, typed IDs/configuration/results/events, and clear state transitions.
- Keep native platform and renderer types private to concrete backend targets. Public contracts expose Horo types only.
- Follow local style and `clang-format`; avoid style-only rewrites, unnecessary allocation, virtual dispatch, macros, or template complexity.
- Public header declarations own complete Doxygen contracts. `.cpp` definitions use `@copydoc` and remain thin.
- New user-visible editor text must use localization and keep `assets/localization/editor/en-US.json` and `tr-TR.json` structurally aligned.

## Review priorities

- Check ownership, lifetime, cancellation, shutdown, rollback, partial initialization, and invalid state transitions.
- Check dependency direction and reject feature code that selects or instantiates concrete platform/render backends.
- For renderer changes, require parity across OpenGL, Metal where supported, combined-backend, and headless/test compositions; do not introduce normal-frame GPU waits.
- For editor UI changes, check shared design-system controls, narrow layouts, long/localized text, and normal/hovered/active/disabled/focused/open-popup states where relevant.
- For serialization, metadata, paths, and file operations, check malformed, duplicate, missing, oversized, version-skewed, whitespace, non-ASCII, and platform-specific cases as applicable.
- Require regression coverage for behavior changes when practical. Do not claim tests passed unless the PR provides evidence.

## Project validation

- Canonical validation is `cmake -S . -B build/skeleton -DBUILD_TESTING=ON`, `cmake --build build/skeleton --parallel`, and `ctest --test-dir build/skeleton --output-on-failure`.
- GPU smoke tests are opt-in and must not be treated as passed without a compatible display and graphics device.
- Review the relevant architecture document before accepting cross-module or public-contract changes.

## PR scope

- Prefer narrow root-cause fixes over unrelated cleanup or broad refactors.
- Breaking public contracts requires a written reason, affected callers, migration path, and regression coverage.
- Do not accept generated build output, caches, logs, screenshots, credentials, or machine-specific paths.
- Use Conventional Commits with an imperative, truthful, focused subject. Do not commit, push, rewrite history, or change branches as part of review.
