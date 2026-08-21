# Product Roadmap Model

## Purpose

Horo Engine workstreams advance in parallel. GitHub milestones therefore
represent whole-product checkpoints, not subsystem phases or implementation
order. A milestone answers when a capability must be complete for the product;
it does not identify its owner or imply that every earlier-numbered ticket must
be implemented first.

## Planning Dimensions

| Dimension | Question |
|---|---|
| Ticket Type | What kind of work is this: architecture decision, implementation, validation, or documentation? |
| Area | Which workstream owns it? |
| Roadmap State | Is it planned, ready, blocked, or intentionally future work? |
| Roadmap Horizon | How near is active planning or implementation? |
| Milestone | Which whole-product checkpoint requires it? |
| `blocked by` | Which technical prerequisites must complete first? |
| Parent/sub-issue | Which initiative owns the deliverable? |

`Area`, milestone, horizon, and dependency are independent. Editor, rendering,
assets, gameplay, build, extensions, packages, SDK, physics, audio, networking,
platform, security, observability, and release tickets may share a milestone and
be implemented concurrently.

## Product Checkpoints

### M0 — Architecture Baseline

Major cross-subsystem contracts, ownership rules, compatibility boundaries, and
decisions needed by dependent implementation are defined and reviewable.

### M1 — Engine Prototype

The first working forms of core engine, editor, rendering, assets, gameplay,
build, extension, and platform flows are connected into a usable prototype.

### M2 — Developer Preview

A developer can create, build, run, diagnose, restore, and iterate on a real
project through coherent supported workflows.

### M3 — Alpha

The primary engine, editor, runtime, gameplay, package, and extension feature
set exists and is integrated for broader alpha use.

### M4 — Beta

Feature scope is close to complete. Work emphasizes stability, compatibility,
performance, recovery, cross-platform behavior, and production-quality UX.

### M5 — 1.0

The first stable public release meets its declared platform, compatibility,
documentation, distribution, migration, and reliability commitments.

### Post-1.0

Ecosystem, marketplace, distribution, and advanced capabilities that are
valuable but intentionally not required for the first stable release.

## Assignment Rules

- Assign a child issue to the earliest product checkpoint that requires its
  observable outcome, not to the phase of its owning subsystem.
- Assign a parent issue to the checkpoint where all required children for that
  parent are expected to be complete. A parent may therefore target a later
  checkpoint than its first children.
- Use native dependencies for real execution order. Never infer order from
  milestone number, Area, issue number, or board position.
- Keep future architecture visible, but only mark dependency-unblocked work in
  the active horizon as `Ready`.
- Capability roadmaps may describe subsystem evolution, but they do not replace
  product milestones. The extension-specific stages are documented in
  [Extension Capability Roadmap](../extensions/extension-capability-roadmap.md).
