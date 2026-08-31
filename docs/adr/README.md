# Architecture Decision Records

This directory contains Architecture Decision Records (ADRs) for Horo Engine.
Each ADR documents a significant architectural choice: the context, the
decision, the consequences, and the rejected alternatives.

ADRs are immutable after acceptance. They may be superseded by later ADRs,
but never silently edited. Superseded ADRs remain in the index with a pointer
to the replacement.

## Index

| ID                                    | Title                                | Status   | Date       |
|---------------------------------------|--------------------------------------|----------|------------|
| [001](001-native-ci-builds.md)        | Host-Agnostic Local Release Pipeline | accepted | 2026-05-30 |
| [002](002-credential-handling.md)     | Credential Handling                  | accepted | 2026-05-25 |
| [003](003-artifact-identity.md)       | Artifact Identity                    | accepted | 2026-05-25 |
| [004](004-cli-core-gui-boundary.md)   | CLI, Core, and GUI Boundary          | accepted | 2026-05-25 |
| [005](005-submodule-compatibility.md) | Submodule Compatibility Constraints  | accepted | 2026-05-25 |
| [006](006-lua-5-4-gameplay-runtime.md) | Lua 5.4 Gameplay Runtime             | accepted | 2026-08-02 |
| [007](007-cross-engine-project-interchange.md) | Cross-Engine Project Interchange | proposed | 2026-08-26 |
| [008](008-error-model-exception-boundary-and-registry.md) | Error Model, Exception Boundary and Registry Ownership | proposed | 2026-08-27 |
| [009](009-configuration-schema-precedence-and-secret-boundary.md) | Configuration Schema, Precedence and Secret Boundary | proposed | 2026-08-28 |
| [010](010-job-waiting-and-operation-store-ownership.md) | Job Waiting and Operation Store Ownership | proposed | 2026-08-28 |
| [011](011-vfx-effect-ownership-simulation-domain-and-renderer-boundary.md) | Effect Ownership, Simulation Domain Policy and Renderer Boundary | proposed | 2026-08-28 |
| [012](012-world-streaming-partition-authority-and-subsystem-boundaries.md) | World Streaming Partition Authority and Subsystem Boundaries | proposed | 2026-08-28 |
| [013](013-environment-query-ownership-item-and-scoring-model.md) | Environment Query Ownership, Item and Scoring Model | proposed | 2026-08-28 |
| [014](014-sequencer-ownership-clock-authority-and-binding-boundary.md) | Sequencer Ownership, Clock Authority and Binding Boundary | proposed | 2026-08-28 |
| [015](015-accessibility-ownership-typed-transport-and-non-gating-policy.md) | Accessibility Ownership, Typed Transport and Non-Gating Policy | proposed | 2026-08-28 |
| [016](016-navigation-target-ownership-and-dependency-boundary.md) | Navigation Target Ownership and Dependency Boundary | proposed | 2026-08-28 |
| [017](017-prefab-role-ownership-and-capability-tiers.md) | Prefab Role, Ownership and Capability-Tier Decision | proposed | 2026-08-28 |
| [018](018-command-registration-permissions-threading-and-packaged-build-policy.md) | Command Registration, Permissions, Threading and Packaged-Build Policy | proposed | 2026-08-28 |
| [019](019-cli-host-command-ownership-adapter-equivalence-and-horopak-boundary.md) | CLI Host, Command Ownership, Adapter Equivalence and horopak Boundary Decision | proposed | 2026-08-28 |
| [020](020-network-target-ownership-and-dependency-boundary.md) | Network Target Ownership and Dependency Boundary | proposed | 2026-08-28 |
| [021](021-gameplay-ai-ownership-scheduling-and-behavior-boundary.md) | Gameplay AI Ownership, Scheduling and Behavior Boundary | proposed | 2026-08-28 |
| [022](022-ai-fixed-tick-order-authority-and-simulation-budget.md) | AI Fixed-Tick Order, Authority and Simulation Budget | proposed | 2026-08-28 |
| [023](023-world-index-and-cell-format-architecture-decision.md) | World Index and Cell Format Architecture Decision | proposed | 2026-08-28 |
| [024](024-perception-ownership-sense-policy-and-budget.md) | Perception Ownership, Sense Policy and Budget Decision | proposed | 2026-08-28 |
| [025](025-ai-decision-assets-and-gameplay-behavior-boundary.md) | AI Decision Assets and Shared Gameplay Behavior Boundary | proposed | 2026-08-28 |
| [026](026-large-world-precision-and-floating-origin-strategy.md) | Large-World Precision and Floating Origin Strategy | proposed | 2026-08-28 |
| [027](027-renderer-resource-identity-and-descriptors.md) | Renderer Resource Identity and Descriptors | proposed | 2026-08-31 |
| [028](028-renderer-capability-limits-and-product-profiles.md) | Renderer Capability, Limits and Product Profiles | proposed | 2026-08-31 |
| [029](029-opengl-compatibility-profile-and-platform-policy.md) | OpenGL Core Profile and Platform Policy | proposed | 2026-08-31 |
| [030](030-metal-platform-and-feature-baseline.md) | Metal Platform and Feature Baseline | proposed | 2026-08-31 |

## Conventions

- **Status**: `proposed`, `accepted`, `deprecated`, `superseded`
- **Naming**: `NNN-lowercase-title-with-hyphens.md`
- **Format**: Each ADR includes Context, Decision, Consequences, and
  Rejected Alternatives sections at minimum
- **Supersession**: When an ADR is superseded, its status changes to
  `superseded` and the `Superseded by` field points to the replacement
- **Index**: Every ADR must be listed here
