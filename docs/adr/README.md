# Architecture Decision Records

This directory contains Architecture Decision Records (ADRs) for Horo Engine.
Each ADR documents a significant architectural choice: the context, the
decision, the consequences, and the rejected alternatives.

ADRs are immutable after acceptance. They may be superseded by later ADRs,
but never silently edited. Superseded ADRs remain in the index with a pointer
to the replacement.

## Index

| ID | Title | Status | Date |
|---|---|---|---|
| [001](001-native-ci-builds.md) | Host-Agnostic Local Release Pipeline | accepted | 2026-05-30 |

## Conventions

- **Status**: `proposed`, `accepted`, `deprecated`, `superseded`
- **Naming**: `NNN-lowercase-title-with-hyphens.md`
- **Format**: Each ADR includes Context, Decision, Consequences, and
  Rejected Alternatives sections at minimum
- **Supersession**: When an ADR is superseded, its status changes to
  `superseded` and the `Superseded by` field points to the replacement
- **Index**: Every ADR must be listed here
