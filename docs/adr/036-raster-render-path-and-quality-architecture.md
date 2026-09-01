# ADR-036: Raster Render Path and Quality Architecture

- **Status**: Proposed
- **Date**: 2026-09-01
- **Supersedes**: None
- **Scope**: Raster path selection, lighting preparation, transparency and quality fallback
- **Issue**: [RND-012.1](https://github.com/abdullahbodur/horo-engine/issues/383)
- **Jira**: [HORO-383](https://horo-engine.atlassian.net/browse/HORO-383)
- **Normative document**: [Rendering Architecture](../architecture/runtime/rendering-architecture.md)

## Context

The renderer product profiles name forward, Forward+ and deferred recipes, but
they do not yet define those terms, identify who selects a path, or say whether
clustered lighting is a fourth path. Transparency, materials that cannot write a
GBuffer, and missing compute or attachment support can therefore cause each
backend or feature team to invent a different fallback. That would make a profile
name behave differently for reasons that are invisible to content and would leak
backend policy into scene extraction, materials and post processing.

The current editor viewport's bounded forward-preview renderer is an implemented
migration baseline, not the production raster policy. The render graph and typed
resource model are also incomplete. This decision specifies the policy that later
delivery must implement; it does not claim that clustered light lists, a GBuffer,
or production PBR passes already exist.

This ADR owns production raster recipe semantics and selection. It preserves the
effective capability and product-profile authority in
[ADR-028](028-renderer-capability-limits-and-product-profiles.md), shader and
reflection rules in [ADR-035](035-shader-source-and-intermediate-representation.md),
and resource lifetime in
[ADR-027](027-renderer-resource-identity-and-descriptors.md). Materials own their
shading model and pass compatibility; post processing owns image effects; shadow
systems own shadow algorithms. None of those systems selects a backend path.

## Decision

### 1. The render frontend resolves one immutable raster recipe

`RenderFrontend` is the policy owner. At renderer initialization and an explicit
quality or content-policy change, it resolves a `RasterRecipe` from:

- the requested `RenderProductProfile` and project quality policy;
- the immutable effective capability/limit snapshot from ADR-028;
- cooked shader, material and pipeline variants admitted for the target;
- scene/view requirements and finite product budgets; and
- explicitly permitted fallback edges.

The resolved recipe is backend-neutral, versioned, diagnostic, and immutable for
its generation. Backends report facts and execute the compiled graph; they cannot
promote, demote or substitute a path. Scene extraction emits geometry, material,
light, view and classification data without branching on OpenGL, Metal, Vulkan or
D3D12. A material can declare compatible pass families and required semantics,
but cannot globally select the scene path.

Resolution produces either a fully admitted recipe or a typed failure. It records
the requested and selected profile/path, every applied fallback rule, the exact
failed predicate, effective capability revision, content-policy revision and
recipe generation. No path changes in response to a slow frame, allocation
failure, shader miss, driver error or arbitrary light count. Adaptive quality is
a separate explicit controller that may request a new policy generation at a
frame boundary; it does not mutate the active graph in place.

### 2. Three opaque recipe families, with clustered Forward+ as one family

Horo defines three production opaque raster families:

| Family | Required preparation and attachments | Lighting contract | Intended use |
|---|---|---|---|
| Forward | Optional depth prepass; one color target plus depth/stencil. No compute, storage-buffer, indirect-draw or multiple-render-target requirement. | Bounded CPU-prepared per-view and per-draw light lists. Every limit and overflow policy is selected before execution. | Portable baseline, simple scenes, previews and explicit fallback. |
| Clustered Forward+ | Depth prepass; compute/storage support; bounded 3D view-space cluster grid and compact light-reference lists consumed by forward material passes. | One deterministic cluster assignment for opaque and compatible transparent work. Overflow is diagnosed and handled by the cooked recipe's declared bounded policy. | Standard scalable path when all predicates pass. |
| Deferred | Depth/GBuffer production followed by screen-space lighting into scene color; requires the declared attachment count, formats, blend/sample support, bandwidth budget and compatible material variants. | Light preparation may reuse the clustered data contract, but deferred lighting consumes GBuffer attributes rather than re-shading geometry. | High-complexity opaque scenes and effects that require GBuffer data. |

“Forward+” is the product-facing name for the selected **3D clustered forward**
family. Horo does not maintain a separate 2D tiled Forward+ production path. A
backend may optimize cluster construction internally only when observable light
membership, overflow, ordering and diagnostics remain equivalent. A future tiled
or hybrid family requires a new decision and recipe identity, not an undocumented
backend optimization.

Cluster dimensions, depth partitioning, maximum lights, references per cluster,
index widths, overflow reserve and CPU/GPU buffer budgets are typed product/cook
inputs capped by effective limits. No universal values are invented by a profile
name. Cluster construction uses stable light identities and a specified stable
tie-break order; atomic append order is never visible ordering authority.

Forward light lists use the same stable identities and deterministic scoring/tie
break contract. Exceeding an admitted runtime bound reports bounded degradation
according to the recipe (for example, retain the highest-scored lights with a
stable ID tie break) and telemetry; it never reads or writes out of bounds. A
project that marks complete light coverage as required fails admission when its
declared maximum cannot be represented.

### 3. Opaque, masked, special and transparent classification is explicit

Render extraction classifies each material instance from cooked material
metadata; it does not inspect shader names or backend state:

- `Opaque` writes depth and uses the active opaque family.
- `Masked` evaluates deterministic alpha coverage and participates in depth,
  shadows and the active opaque family with a compatible cooked variant.
- `ForwardOnlyOpaque` uses a forward pass at a declared placement when its model
  cannot populate the active GBuffer; it is not silently omitted or coerced into
  incompatible GBuffer channels.
- `TransparentSorted` uses a depth-tested, non-depth-writing forward pass after
  opaque lighting, sorted back-to-front by a stable view-space key and stable
  instance identity tie break.
- `TransparentAdditive` uses a separately declared commutative additive pass;
  it is not mixed into alpha ordering by accident.

Deferred is therefore a **hybrid frame recipe**, not a requirement that every
material use deferred shading. Deferred-compatible opaque/masked work populates
the GBuffer; forward-only opaque work executes at the recipe's declared boundary;
all baseline transparency is forward shaded. The recipe provides transparent
lighting data even when the opaque family is deferred.

The baseline supports sorted alpha and additive transparency. Weighted blended
order-independent transparency, per-pixel linked lists, depth peeling, stochastic
transparency and refraction are optional named feature recipes with separate
capability, memory, ordering and fallback contracts. They cannot replace sorted
alpha merely because a backend supports an atomic or blending feature.

MSAA is admitted per complete recipe. Deferred MSAA requires declared multisample
GBuffer, resolve and lighting semantics; otherwise the resolver may select a
permitted clustered-forward or forward recipe. It may not silently disable the
requested sample count. Alpha-to-coverage is a masked-material variant and never
the universal transparency solution.

### 4. Scene-color, depth and GBuffer contracts are path-independent inputs

Every recipe publishes typed graph resources and semantics rather than native
attachments. At minimum, downstream consumers can request resolved scene color,
depth and declared optional semantic inputs. The exact scene-color encoding,
working gamut, exposure and output transform belong to RND-013; this ADR requires
only that all admitted paths produce the same selected scene-color contract.

Deferred GBuffer layouts are versioned cooked schemas. A schema declares each
semantic, format, encoding, clear/load/store behavior, sample count and producer/
consumer stage. It is selected against complete format/attachment/limit support.
Backends cannot add private channels or reinterpret values. Material and lighting
pipelines carry the schema identity; incompatible artifacts fail before graph
execution. Optional post-process inputs are graph requirements that participate
in recipe resolution, not evidence that deferred must be selected: a forward
recipe may emit a bounded normal or velocity prepass when its declared variant and
budget permit it.

Depth ownership remains with the raster recipe through opaque completion. A
prepass and later color/GBuffer pass must share an explicit depth-equality and
masked-coverage contract. Reversed-Z, depth format and resolve policy are typed
recipe fields constrained by backend support; they cannot vary per material.

### 5. Profile preferences and fallback edges are explicit

ADR-028 profile names remain preferences, not guarantees:

| Requested profile | Preferred opaque family | Ordered permitted fallback |
|---|---|---|
| Baseline | Forward | None; failure of required baseline raster/material operations fails admission. |
| Standard | Clustered Forward+ | Forward, when the project/content policy admits bounded CPU lighting and all required material/effect variants exist. |
| High | Deferred | Clustered Forward+, then Forward, with the same complete-recipe checks. |
| Ultra | Deferred plus separately enabled optional features | High's deferred recipe, then its declared High/Standard/Baseline edges. Ultra does not define a fourth opaque family. |

The resolver evaluates complete candidate recipes in this order. A fallback is
admitted only when every required material, transparency, shadow, scene-color,
post-process input, sample-count and budget constraint remains satisfied. It
reports the selected lower recipe and reasons. Explicit project requirements can
remove edges; missing optional content can disable only an optional feature with
its own declared fallback. Required content never becomes optional because a
lower profile exists.

OpenGL 4.1 and other baseline implementations may initially admit only Forward.
That is a truthful capability/implementation result, not a special OpenGL policy.
The Null backend validates recipe structure and deterministic resolution without
claiming native raster or image qualification.

### 6. Scheduling, lifetime and failure behavior

The frontend compiles the selected recipe into a graph with explicit resource
uses. A representative order is depth/coverage preparation, light preparation,
opaque family passes, forward-only opaque, sky/background, transparency, declared
scene-color consumers, output transform and presentation. Actual optional passes
are graph dependencies, not hidden callbacks or a fixed global list.

CPU extraction and light preparation are bounded jobs over immutable snapshots.
GPU cluster/GBuffer resources follow ADR-027/034 generation and retirement rules.
Recipe replacement stages all required artifacts and resources, validates the
candidate, then publishes at a render safe point. In-flight frames retain the old
generation. Cancellation, stale device/content generations, missing pipelines or
budget failure discard the candidate and keep the last good recipe where valid.
Device loss follows renderer recovery; it does not trigger an unreported path
downgrade.

## Rejected Alternatives

| Option | Decision and reason |
|---|---|
| One universal forward path | Rejected as the only production path: it preserves portability but does not provide the bounded scalable lighting and GBuffer inputs requested by higher recipes. It remains the required baseline. |
| Deferred rendering for every profile and material | Rejected: attachment/bandwidth requirements exclude baseline devices and transparent or special materials still require forward shading. |
| Separate Forward+, tiled and clustered product paths | Rejected for the baseline: overlapping recipes multiply shaders, qualification and fallback edges. Horo standardizes on 3D clustered Forward+ semantics. |
| Let each backend choose its fastest path | Rejected: hardware facts do not own product policy, cooked content or observable quality. It would make diagnostics and parity non-deterministic. |
| Switch paths automatically when frame time or allocation changes | Rejected: hidden mid-run semantic changes invalidate graph resources, temporal history and quality expectations. An explicit adaptive controller may request a new generation. |
| Route all transparency through deferred lighting | Rejected: conventional GBuffer composition does not represent ordered transmission/blending. Baseline transparency remains forward. |
| Require deferred solely because an effect wants normals or velocity | Rejected: optional semantic prepasses can satisfy declared inputs without making the opaque family an effect-owned choice. |

The selected policy costs multiple cooked material/pass variants and requires
cross-path image qualification. It bounds that cost to three opaque families,
one clustered Forward+ definition and explicit feature variants instead of a
backend-specific matrix of hidden paths.

## Migration And Verification

The current editor forward-preview executor remains a scoped migration input. It
must not be relabeled production Forward until it uses the common material/light,
recipe, graph and qualification contracts. Existing profile prose is interpreted
through this ADR; implementations must remove direct profile/backend branches and
resolve a typed recipe before admitting scene work.

| Delivery | Required implementation evidence |
|---|---|
| RND-012.2 / #381 | Production Forward opaque/masked passes, deterministic bounded CPU light lists and baseline fallback diagnostics. |
| RND-012.3 / #382 | Versioned clustered Forward+ grid/list contract, bounded overflow behavior and compute-to-graphics graph synchronization. |
| RND-012.4 / #384 | Versioned GBuffer schema, deferred lighting, forward-only material placement and complete attachment/sample admission. |
| RND-012.5+ / #386 onward | Light extraction/culling, shadows, transparency and quality tiers consume the resolved recipe without becoming selection authorities. |

Contract and integration tests must cover:

- deterministic resolution for every profile under missing compute, storage,
  attachment, format, sample, budget, shader and material predicates;
- explicit removal of fallback edges, required-content failure and stable reason
  ordering in diagnostics;
- forward and clustered overflow at exact bounds, stable-ID ties, zero lights,
  maximum declared lights and malformed cluster/list data;
- opaque, masked, forward-only, sorted-alpha and additive classification, including
  stable transparent ties and incompatible/missing variants;
- deferred plus forward transparency/special materials, GBuffer schema mismatch,
  depth-prepass coverage mismatch and optional normal/velocity prepasses;
- recipe replacement, cancellation, device/content generation changes, last-good
  retention and GPU retirement with frames in flight; and
- cross-backend image fixtures for each advertised family and fallback, with
  documented numerical/image tolerances. Null coverage proves policy only.

## Consequences

Downstream tickets have one owner and one vocabulary for raster path selection.
Profiles resolve deterministically, clustered lighting is no longer ambiguous,
and deferred rendering composes explicitly with forward-only and transparent
work. The cost is a versioned recipe/schema layer, more cooked variants, bounded
light-list policy and mandatory cross-path qualification. This decision changes
no current renderer implementation or advertised backend capability by itself.
