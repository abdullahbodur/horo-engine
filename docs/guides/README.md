# Horo Engine Guides

This directory contains implementation-facing guides for developers who build on
Horo Engine and contributors working on its development tooling. Architecture
documents define the contracts; guides show how to use those contracts in concrete
workflows.

## Available Guides

- [Extension Module Development](./extension-module-development.md): build an
  add-on package that contributes editor tabs, Settings pages, MCP tools,
  commands, and data-bus observers through the extension ABI/API.
- [Local C/C++ Analysis with SonarQube MCP and VS Code](./sonarqube-mcp-local-analysis.md):
  configure the IDE bridge, analyze local changes, and diagnose partial results.
- [Terrain Descriptor Migration](./terrain-descriptor-migration.md): adopt revisioned
  bounds, versioned tier limits, immutable configuration snapshots, and replacement fencing.
- [Foliage Definition Migration](./foliage-definition-migration.md): adopt stable typed
  placement, culling, wind, collision, and exact capability admission.
- [World Spatial Object Descriptor Migration](./world-spatial-object-descriptor-migration.md):
  adopt stable authored-object identity, source, bounds, placement, and revision admission.
- [Cross-Cell Dependency Policy Migration](./cross-cell-dependency-policy-migration.md):
  classify hard co-load and soft deferred references without mixed-policy ambiguity.
- [World Object Ownership Migration](./world-object-ownership-migration.md): adopt explicit
  authored, persistent, cell-bound, and runtime-spawned ownership admission.
- [World Partition Capability Profile Migration](./world-partition-capability-profile-migration.md):
  validate project grid, precision, capacity, and package settings without fallback.
- [World Partition Registry Snapshot Migration](./world-partition-registry-migration.md):
  publish immutable generation-pinned indices and run bounded allocation-free queries.
- [XR Coordinate and Pose Contract Migration](./xr-coordinate-pose-migration.md):
  publish generation-fenced coordinate, validity, and time evidence without native
  backend leakage or implicit clock conversion.
- [XR Tracking Snapshot Migration](./xr-tracking-snapshot-migration.md):
  publish immutable, bounded device and pose snapshots with stable identity,
  capability, confidence, and loss-state semantics.
- [CPU Particle Buffer Migration](./cpu-particle-buffer-migration.md):
  replace per-particle storage and freelists with bounded aligned SoA storage and
  generation-safe spawn/kill bookkeeping.
- [CPU Particle Spawn Pipeline Migration](./cpu-particle-spawn-pipeline-migration.md):
  prepare deterministic continuous/burst birth, descriptor initialization and expiry
  over fixed SoA capacity without steady-state allocation.
- [Grounded Navigation Provider Composition](./grounded-navigation-provider-composition.md):
  compose the pinned Detour runtime provider from neutral topology with explicit
  capacity, world-generation, cancellation, and teardown behavior.

## Writing a Guide

Start new guides from [Guide Template](./guide-template.md). Use its Purpose,
Prerequisites, Workflow, Troubleshooting, Limitations, Validation Record, and
References sections; adapt the workflow steps to the task. Use lowercase hyphenated
filenames and add the finished guide to this index.

Use generic placeholders for accounts, organization/project keys, and absolute
paths. Keep secrets out of examples. Include expected results and actual verification
limits. Link architecture contracts instead of redefining them. Existing guides can
adopt the template when substantively revised; avoid format-only rewrites.
