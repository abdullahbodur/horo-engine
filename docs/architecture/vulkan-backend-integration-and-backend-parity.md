# Vulkan Backend Integration And Backend Parity

## Intent

This document locks the Vulkan v1 parity scope before deeper backend work starts. It defines what “backend parity” means for this engine, which behaviors are required in the first Vulkan milestone, and how unsupported features should fail or degrade.

This document does **not** replace the backend-agnostic rendering foundation. It builds on that foundation and narrows the execution target for the Vulkan rollout.

## Why This Follows The Backend-Agnostic Foundation

The renderer already has:

- typed backend selection
- backend-owned frame/pass submission seams
- explicit renderer lifecycle boundaries
- initial backend capability reporting

That foundation is necessary, but not sufficient. Before Vulkan implementation expands, the engine needs one agreed answer for:

- which workflows must work in Vulkan v1
- which workflows may degrade through explicit fallbacks
- which workflows are deferred and must not silently appear “supported”

Without this scope lock, parity work would become an unbounded rewrite.

## Vulkan v1 Goal

Vulkan v1 is successful when the engine can select the Vulkan backend at startup and run the agreed first-slice runtime and editor workflows without reintroducing OpenGL-only assumptions into shared engine layers.

The target is **capability-driven parity**, not identical implementation details between OpenGL and Vulkan.

## Non-Goals

The following are explicitly out of scope for Vulkan v1:

- image-perfect parity with OpenGL across all content
- full post-processing parity
- complete GPU profiling/tooling parity
- bindless resource support
- compute-driven renderer features
- advanced editor rendering optimizations beyond the agreed first-slice workflows

## Parity Model

Parity in this epic means:

1. higher-level engine code uses backend-neutral renderer contracts
2. required workflows succeed on both OpenGL and Vulkan
3. unsupported or deferred workflows are reported through explicit capabilities or deterministic fallbacks
4. no higher-level system silently depends on OpenGL-only objects, ids, or state transitions

Parity does **not** mean that both backends must:

- use the same internal resource model
- expose the same debug/profiling depth
- produce identical implementation-specific output when a capability is intentionally deferred

## Required Vulkan v1 Workflows

The following workflows are required for the first Vulkan parity milestone:

- runtime startup with Vulkan selected
- runtime frame clear / present / resize recovery
- opaque runtime scene rendering for the agreed starter/sample content set
- editor viewport rendering through backend-neutral target ownership
- thumbnail generation through backend-neutral offscreen rendering
- screenshot/readback through backend-owned interfaces
- debug draw support for the first editor/runtime parity slice
- debug HUD support where backed by explicit capability-aware readback behavior

## Capability-Driven Fallback Policy

When Vulkan v1 cannot support an existing OpenGL-era workflow at the same depth, the engine must use one of the following behaviors deliberately:

- **required**: feature must work; initialization or test coverage should fail if missing
- **fallback**: higher-level code may degrade to a documented reduced behavior
- **deferred**: feature is unavailable in Vulkan v1 and must report that unavailability explicitly

Hidden no-ops are not acceptable unless the fallback is documented and test-covered.

## Vulkan v1 Parity Matrix

| Workflow / capability | OpenGL status | Vulkan v1 target | Policy if unsupported | Notes |
|---|---|---|---|---|
| Runtime backend startup | Supported | Required | Fail initialization with actionable error | Must select Vulkan through typed backend bootstrap |
| Frame clear / present / resize recovery | Supported | Required | Fail initialization or resize path deterministically | No silent OpenGL fallback |
| Opaque runtime scene render | Supported | Required | Fail validation/tests | First scene slice for starter/sample content |
| Editor viewport | Supported | Required | Fallback only if explicit placeholder path exists and is test-covered | Must not rely on raw OpenGL FBO ownership in editor code |
| Thumbnail generation | Supported | Required | Fallback to explicit placeholder thumbnail only if documented | Final policy should be deterministic, not best-effort |
| Screenshot / readback | Supported | Required | Fail command/request with clear diagnostic | Backend-owned readback path required |
| Debug draw | Supported | Required | Fallback to disabled debug rendering with visible capability gate | Do not silently submit OpenGL-only debug paths |
| Debug HUD | Supported | Fallback | Suppress unsupported HUD views with explicit capability check | Depth/readback-heavy panels may ship in reduced form first |
| Wireframe overlay | Supported | Fallback | Disable toggle or render reduced debug alternative | Capability must be surfaced explicitly |
| Debug labels | Limited | Fallback | Omit labels if unsupported | Backend should expose support explicitly |
| GPU timestamps / profiling | Unsupported in practice | Deferred | Report unavailable | Not required for Vulkan v1 |
| Compute-driven features | Unsupported | Deferred | Report unavailable | Outside first parity slice |
| Bindless resources | Unsupported | Deferred | Report unavailable | Outside first parity slice |

## Initial Capability Surface Expectations

The current renderer capability model already tracks a small set of backend features. Vulkan parity work is expected to extend that model so higher-level systems can make explicit decisions about at least:

- debug draw support
- wireframe overlay support
- debug label support
- offscreen target support
- native texture handle support
- readback / screenshot support
- depth-readback support
- editor viewport support
- debug HUD feature availability

The current parity stack has already established explicit capability gates for:

- debug draw
- wireframe overlay
- offscreen target usage in editor preview flows
- native texture handle usage in editor preview flows
- screenshot/readback
- depth-readback-dependent DebugHUD behavior
- ImGui backend selection support

This document does not require all of those fields to be added in one PR. It requires the final parity work to move toward explicit capability checks instead of backend-specific branching in editor or scene code.

## Current Implemented Foundation State

The current stacked implementation toward this epic now provides:

- backend-aware window/bootstrap ownership split
- backend-owned frame output control
- hidden OpenGL payload storage behind resource implementations
- opt-in Vulkan dependency scaffolding (`Vulkan-Headers` + `volk`)
- minimal Vulkan backend bootstrap with instance/device/surface/swapchain/clear/present
- opaque-scene submission acceptance through the Vulkan backend seam
- backend-neutral preview handle abstraction for editor thumbnail paths
- editor ImGui backend routing through an explicit backend shim
- capability-gated debug draw, screenshot/readback, and DebugHUD depth-readback behavior

This means the architecture discussion is no longer purely aspirational. Follow-up work should now focus on turning these seams into fuller parity rather than reintroducing OpenGL assumptions into the engine-facing layers.

## Remaining Work After The Current Stack

The largest remaining gaps after the current implementation stack are:

- real Vulkan mesh/material/pipeline translation beyond accepted opaque-scene submissions
- backend-neutral editor viewport rendering rather than seam-only backend routing
- full offscreen render-target ownership beyond preview handles
- backend-owned screenshot/readback implementations rather than capability-gated OpenGL-only behavior
- broader parity CI once Vulkan coverage is stable enough for hosted runners

These gaps should be treated as explicit backlog, not silent capability drift.

## Validation Strategy

The living validation contract for this epic now lives in:

- `docs/development/backend-parity-validation-matrix.md`

That matrix complements this architecture document by defining:

- required default-build contract tests
- Vulkan-enabled smoke coverage
- required command lines for parity-related PRs
- current known validation gaps

## Acceptance Criteria For Closing The Scope Phase

The scope-definition phase is complete when:

- the Vulkan v1 required workflows are documented
- non-goals are explicit
- the parity model is defined as capability-driven graceful degradation
- the parity matrix identifies required, fallback, and deferred items
- follow-up PRs can cite this document as the review contract for Vulkan parity work

## Review Checklist

- Does the change move a required Vulkan v1 workflow toward backend-neutral execution?
- Does it eliminate an OpenGL-only assumption from shared engine/editor code?
- If a feature is not fully supported, is its fallback or deferred state explicit?
- Are tests aligned with the policy in the parity matrix?
- Does the PR narrow risk for the next Vulkan slice rather than widen backend coupling?
