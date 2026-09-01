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
| [001](001-native-ci-builds.md)        | Host-Agnostic Local Release Pipeline | Accepted | 2026-05-30 |
| [002](002-credential-handling.md)     | Credential Handling                  | Accepted | 2026-05-25 |
| [003](003-artifact-identity.md)       | Artifact Identity                    | Accepted | 2026-05-25 |
| [004](004-cli-core-gui-boundary.md)   | CLI, Core, and GUI Boundary          | Accepted | 2026-05-25 |
| [005](005-submodule-compatibility.md) | Submodule Compatibility Constraints  | Accepted | 2026-05-25 |
| [006](006-lua-5-4-gameplay-runtime.md) | Lua 5.4 Gameplay Runtime             | Accepted | 2026-08-02 |
| [007](007-cross-engine-project-interchange.md) | Cross-Engine Project Interchange | Proposed | 2026-08-26 |
| [008](008-error-model-exception-boundary-and-registry.md) | Error Model, Exception Boundary and Registry Ownership | Proposed | 2026-08-27 |
| [009](009-configuration-schema-precedence-and-secret-boundary.md) | Configuration Schema, Precedence and Secret Boundary | Proposed | 2026-08-28 |
| [010](010-job-waiting-and-operation-store-ownership.md) | Job Waiting and Operation Store Ownership | Proposed | 2026-08-28 |
| [011](011-vfx-effect-ownership-simulation-domain-and-renderer-boundary.md) | Effect Ownership, Simulation Domain Policy and Renderer Boundary | Proposed | 2026-08-28 |
| [012](012-world-streaming-partition-authority-and-subsystem-boundaries.md) | World Streaming Partition Authority and Subsystem Boundaries | Proposed | 2026-08-28 |
| [013](013-environment-query-ownership-item-and-scoring-model.md) | Environment Query Ownership, Item and Scoring Model | Proposed | 2026-08-28 |
| [014](014-sequencer-ownership-clock-authority-and-binding-boundary.md) | Sequencer Ownership, Clock Authority and Binding Boundary | Proposed | 2026-08-28 |
| [015](015-accessibility-ownership-typed-transport-and-non-gating-policy.md) | Accessibility Ownership, Typed Transport and Non-Gating Policy | Proposed | 2026-08-28 |
| [016](016-navigation-target-ownership-and-dependency-boundary.md) | Navigation Target Ownership and Dependency Boundary | Proposed | 2026-08-28 |
| [017](017-prefab-role-ownership-and-capability-tiers.md) | Prefab Role, Ownership and Capability-Tier Decision | Proposed | 2026-08-28 |
| [018](018-command-registration-permissions-threading-and-packaged-build-policy.md) | Command Registration, Permissions, Threading and Packaged-Build Policy | Proposed | 2026-08-28 |
| [019](019-cli-host-command-ownership-adapter-equivalence-and-horopak-boundary.md) | CLI Host, Command Ownership, Adapter Equivalence and horopak Boundary Decision | Proposed | 2026-08-28 |
| [020](020-network-target-ownership-and-dependency-boundary.md) | Network Target Ownership and Dependency Boundary | Proposed | 2026-08-28 |
| [021](021-gameplay-ai-ownership-scheduling-and-behavior-boundary.md) | Gameplay AI Ownership, Scheduling and Behavior Boundary | Proposed | 2026-08-28 |
| [022](022-ai-fixed-tick-order-authority-and-simulation-budget.md) | AI Fixed-Tick Order, Authority and Simulation Budget | Proposed | 2026-08-28 |
| [023](023-world-index-and-cell-format-architecture-decision.md) | World Index and Cell Format Architecture Decision | Proposed | 2026-08-28 |
| [024](024-perception-ownership-sense-policy-and-budget.md) | Perception Ownership, Sense Policy and Budget Decision | Proposed | 2026-08-28 |
| [025](025-ai-decision-assets-and-gameplay-behavior-boundary.md) | AI Decision Assets and Shared Gameplay Behavior Boundary | Proposed | 2026-08-28 |
| [026](026-large-world-precision-and-floating-origin-strategy.md) | Large-World Precision and Floating Origin Strategy | Proposed | 2026-08-28 |
| [027](027-renderer-resource-identity-and-descriptors.md) | Renderer Resource Identity and Descriptors | Proposed | 2026-08-31 |
| [028](028-renderer-capability-limits-and-product-profiles.md) | Renderer Capability, Limits and Product Profiles | Proposed | 2026-08-31 |
| [029](029-opengl-core-profile-and-platform-policy.md) | OpenGL Core Profile and Platform Policy | Proposed | 2026-08-31 |
| [030](030-metal-platform-and-feature-baseline.md) | Metal Platform and Feature Baseline | Proposed | 2026-08-31 |
| [031](031-vulkan-loader-platform-and-version-baseline.md) | Vulkan Loader, Platform and Version Baseline | Proposed | 2026-08-31 |
| [032](032-d3d12-baseline-and-agility-sdk-policy.md) | D3D12 Baseline and Agility SDK Policy | Proposed | 2026-08-31 |
| [033](033-presentation-and-display-ownership.md) | Presentation and Display Ownership | Proposed | 2026-08-31 |
| [034](034-gpu-memory-and-residency-ownership.md) | GPU Memory and Residency Ownership | Proposed | 2026-08-31 |
| [035](035-shader-source-and-intermediate-representation.md) | Shader Source and Intermediate Representation | Proposed | 2026-08-31 |
| [036](036-raster-render-path-and-quality-architecture.md) | Raster Render Path and Quality Architecture | Proposed | 2026-09-01 |
| [037](037-scene-color-and-hdr-architecture.md) | Scene Color and HDR Architecture | Proposed | 2026-09-01 |
| [038](038-gpu-scene-and-instance-data-model.md) | GPU Scene and Instance Data Model | Proposed | 2026-09-01 |
| [039](039-ray-tracing-capability-and-abstraction.md) | Ray Tracing Capability and Abstraction | Proposed | 2026-09-01 |
| [040](040-reconstruction-frame-generation-and-latency-providers.md) | Reconstruction, Frame Generation and Latency Providers | Proposed | 2026-09-01 |
| [041](041-backend-neutral-renderer-diagnostics-model.md) | Backend-Neutral Renderer Diagnostics Model | Proposed | 2026-09-01 |
| [042](042-cpu-gpu-timestamps-and-pipeline-statistics.md) | CPU/GPU Timestamps and Pipeline Statistics | Proposed | 2026-09-01 |
| [043](043-gpu-memory-and-resource-inspection.md) | GPU Memory and Resource Inspection | Proposed | 2026-09-01 |
| [044](044-render-markers-and-debug-labels.md) | Render Markers and Debug Labels | Proposed | 2026-09-01 |

## Conventions

- **Status**: `Proposed`, `Accepted`, `Deprecated`, `Superseded`
- **Header fields**, in this order when present: Status, Date, Deciders,
  Supersedes, Superseded by, Scope, Issue, Jira, Parent, Related,
  Companion decision, Normative document(s). Use `Jira` (not `JIRA`). Link the
  Jira key to the work item. Optional fields are omitted rather than left empty.
- **Issue / Parent**: use the domain alias as the GitHub issue link text
  (`[RND-011.1](https://github.com/abdullahbodur/horo-engine/issues/368)`).
  Do not show `#368` as the visible text or in a second parenthesis.
- **Naming**: `NNN-lowercase-title-with-hyphens.md`
- **IDs are stable**: a number is assigned once and is never reused for a
  different decision. ADR-008 is the error-model decision
  ([008](008-error-model-exception-boundary-and-registry.md)). ADR-027 is
  renderer resource identity
  ([027](027-renderer-resource-identity-and-descriptors.md)). Similar titles
  in drafts or review extracts do not renumber either document.
- **Format**: Each ADR includes Context, Decision, Consequences, and
  Rejected Alternatives sections at minimum
- **Supersession**: When an ADR is superseded, its status changes to
  `Superseded` and the `Superseded by` field points to the replacement
- **Index**: Every ADR must be listed here
