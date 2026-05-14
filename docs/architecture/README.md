# Architecture

This directory documents the current engine architecture, calls out temporary exceptions, and sets the review rules for future changes.

## Documents

- [module-boundaries.md](./module-boundaries.md)
- [renderer-foundation.md](./renderer-foundation.md)
- [backend-agnostic-rendering-foundation-and-runtime-selection.md](./backend-agnostic-rendering-foundation-and-runtime-selection.md)
- [vulkan-backend-integration-and-backend-parity.md](./vulkan-backend-integration-and-backend-parity.md)
- [ownership-lifecycle.md](./ownership-lifecycle.md)
- [error-result-model.md](./error-result-model.md)
- [threading-and-mutation.md](./threading-and-mutation.md)

## Reviewer Rules

- New headers are internal by default unless a document in this directory explicitly treats them as part of a module's public surface.
- Every new public type must clearly identify its owning module in comments, docs, or the PR description.
- Cross-module dependencies should follow `module-boundaries.md`. Temporary exceptions must be documented rather than introduced silently.
