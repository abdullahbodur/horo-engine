# Architecture

This directory is the source of truth for epic `#33` and its child tasks:

- `#34` Define module boundaries and public APIs
- `#35` Standardize resource ownership and lifecycle rules
- `#36` Introduce a consistent error and result model
- `#37` Document threading assumptions and safe mutation points

These documents are intentionally docs-first. They describe the current `main` branch architecture, call out temporary exceptions, and set the review rules for future changes.

## Documents

- [module-boundaries.md](./module-boundaries.md)
- [ownership-lifecycle.md](./ownership-lifecycle.md)
- [error-result-model.md](./error-result-model.md)
- [threading-and-mutation.md](./threading-and-mutation.md)

## Reviewer Rules

- New headers are internal by default unless a document in this directory explicitly treats them as part of a module's public surface.
- Every new public type must clearly identify its owning module in comments, docs, or the PR description.
- Cross-module dependencies should follow `module-boundaries.md`. Temporary exceptions must be documented rather than introduced silently.
