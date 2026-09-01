# ADR-037: Scene Color and HDR Architecture

- **Status**: Proposed
- **Date**: 2026-09-01
- **Supersedes**: None
- **Scope**: Scene working color space, exposure, precision, output transforms and SDR/HDR boundaries
- **Issue**: [RND-013.1](https://github.com/abdullahbodur/horo-engine/issues/391)
- **Jira**: [HORO-391](https://horo-engine.atlassian.net/browse/HORO-391)
- **Companion decision**: [ADR-033: Presentation and Display Ownership](033-presentation-and-display-ownership.md)
- **Normative documents**: [Rendering Architecture](../architecture/runtime/rendering-architecture.md), [Post-Processing and Effects Architecture](../architecture/runtime/post-processing-and-effects-architecture.md)

## Context

The renderer describes “HDR color”, exposure, tone mapping and SDR/HDR output
without selecting a scene working gamut, numeric precision, exposure unit or
display transform. Existing post-process prose stores an unqualified `float
exposure`, offers unrelated tone curves, assumes velocity comes from a GBuffer,
and labels feature tiers with graphics APIs. That leaves importers, materials,
lighting, post processing and every backend free to interpret identical values
differently.

HDR scene rendering and HDR presentation are separate concerns. A wide-range
scene buffer is needed for physically plausible lighting, bloom and exposure even
on an SDR monitor. Conversely, an HDR-capable GPU does not prove that the current
display, surface or OS mode can present HDR. ADR-033 already owns that display
negotiation; this decision supplies the color pipeline consumed by its resolved
output contract.

This ADR owns scene color encoding, exposure semantics, output-transform policy
and the handoff to presentation. It does not select post-process effects, display
modes, swapchain formats, material shading models or light transport algorithms.
[ADR-036](036-raster-render-path-and-quality-architecture.md) requires all raster
paths to publish this same scene-color contract.

## Decision

### 1. Canonical scene color is linear ACEScg

Horo's production scene working color space is **ACEScg**: AP1 primaries, the
ACES white point (`x = 0.32168`, `y = 0.33767`, approximately D60), and a linear
transfer characteristic. RGB values
are scene-referred and proportional to exposure. Values above `1.0` are expected;
finite negative components are preserved through scene-referred processing
because transforms and wide-gamut operations can produce them. Alpha is linear
coverage/opacity only where a graph resource explicitly declares that semantic;
scene color does not use alpha as hidden luminance, bloom or material metadata.

The Academy describes ACEScg as a linear AP1 space intended for CGI rendering and
compositing and permits 16- or 32-bit floating-point storage in its
[ACEScg specification](https://docs.acescentral.com/encodings/acescg/). Horo pins
the exact ACES configuration/transform release in cooked color-pipeline identity;
the label `ACEScg` alone is not a cache key or proof of transform compatibility.

Every color value entering the scene pipeline has a declared source encoding and
semantic. Import/cook converts color textures, color constants, environment maps,
LUT source data and video frames to the required working representation. Data
textures such as normals, roughness, masks, depth and IDs are never color decoded.
For legacy assets without metadata, the importer may apply a versioned role-based
project default (for example, sRGB/Rec.709 for base color); it records the inferred
choice and warning. Runtime shader code never guesses from a filename or native
format.

Authored/editor color controls declare whether values are display-referred UI
colors or scene-referred ACEScg colors. Conversion happens once at the ownership
boundary. Native `SRGB` texture formats may perform the declared transfer decode,
but their primaries and resulting working-space conversion remain represented in
the cooked transform; an sRGB bit on an API format is not the complete color
contract.

### 2. RGBA16F is the canonical production scene-color precision

Production raster recipes publish scene color as four-component IEEE binary16
floating point (`RGBA16F`) with linear ACEScg RGB. This is the minimum admitted
scene-color storage contract for interactive backends. Accumulations whose error
or range can exceed that representation—light reduction, exposure statistics,
long filters or reference validation—use declared `float32` intermediates. Shader
arithmetic follows ADR-035 target requirements and may use `float32` even when the
stored resource is binary16.

Packed unsigned formats such as `R11G11B10_FLOAT`, normalized LDR formats and
native sRGB attachments are not substitutes for canonical scene color: they do
not preserve the same signed range, alpha contract or precision. They may be
declared for a separate constrained product recipe only after a later decision
defines semantic loss, compatible effects, fallback and qualification. A backend
that cannot render, sample, blend and resolve the complete required `RGBA16F`
usage returns a typed recipe-admission failure; it does not silently lower scene
precision.

Transient effects may select narrower formats only when their output contract,
error tolerance and fallback are explicit. Color history records carry working
space, precision, extent, exposure/pre-exposure and pipeline generation. A format,
gamut, output-plan or exposure convention change invalidates incompatible history.

All ingress, intermediate and output stages reject or contain non-finite values.
Debug builds identify the first producing pass where practical; production uses
a declared bounded sanitization at the final output boundary and reports counters.
NaN/Inf is never allowed to become backend-dependent tone-map or presentation
behavior. Finite negative working values are not treated as non-finite errors.

### 3. Exposure is expressed in stops and published once per view

The canonical exposure value is `exposureEv`, a finite base-2 stop offset. The
scene-linear multiplier is:

```text
exposureScale = exp2(exposureEv)
exposedSceneColor = sceneColor * exposureScale
```

Increasing `exposureEv` by `+1 EV` doubles RGB; decreasing it by `-1 EV` halves
RGB. This sign convention is normative. User compensation is
`exposureCompensationEv` in the same stops. An optional physical-camera model may
accept aperture, shutter time and ISO and derive a camera `EV100`, but it must
publish the resulting Horo `exposureEv` plus its versioned calibration. Raw
aperture/ISO values never bypass that conversion into shaders. The physical
mapping has the opposite sign before compensation: increasing `EV100` by one stop
decreases derived `exposureEv` by one stop and therefore halves the multiplier.
Equivalently, a calibrated model has the form
`exposureEv = calibrationEv - EV100 + exposureCompensationEv`; its finite
`calibrationEv` and model revision are explicit pipeline inputs rather than an
ambient backend constant.

Manual exposure publishes a selected finite `exposureEv`. Automatic exposure
measures a declared bounded region of the unexposed scene, computes log2 ACEScg
luminance with stable histogram/reduction bounds, excludes non-finite samples,
and publishes one `ExposureState` generation per view. Metering mode, percentile
bounds, neutral target (default scene-linear `0.18`), minimum/maximum EV, separate
brighten/darken adaptation rates in EV per second, compensation, simulation/view
time source and reset policy are typed settings. Empty/invalid samples retain the
last valid value or use the declared initial EV and report the reason.

Exposure is computed once and consumed by bloom, tone mapping, histories and
debug views; individual effects cannot maintain private exposure estimates.
Implementations may use pre-exposure to protect intermediate numeric range, but
pre-exposure is an internal reversible representation. Every affected resource
records its scale/generation, producers and consumers agree, and capture/debug
tools can reconstruct canonical unexposed ACEScg values. Temporal histories are
rescaled or invalidated explicitly when the exposure generation changes.

Scene values and exposure remain relative. Absolute display luminance uses
`cd/m²` (nits) only in the resolved output contract. No code compares a relative
scene RGB value directly to a monitor-nit limit.

### 4. One versioned color pipeline produces target-specific outputs

The frontend resolves a `ColorPipelinePlan` from project look policy, cooked
transforms, `ExposureState`, the raster recipe and ADR-033's output contract. The
plan carries working encoding, transform/configuration identities, target gamut,
white point, transfer function, paper/reference white in nits, target peak and
black level when known, bit depth, dithering policy and plan generations.

The default production view transform is a pinned **ACES 2 Output Transform**
configuration, including its tonescale and target-gamut compression. The Academy's
[ACES 2 gamut-compression documentation](https://docs.acescentral.com/system-components/output-transforms/technical-details/gamut-compression/)
defines the output-transform stage Horo qualifies. Cooked transform data and
reference fixtures pin the selected release, parameters, and the required
ACEScg/AP1-to-ACES interchange conversion before the output transform;
implementations must not feed AP1 values into an AP0-declared transform or
approximate the route independently per backend. `Neutral` is a separately versioned
diagnostic transform for linear/reference inspection, not an automatic fallback
when the production transform is missing. Other creative looks are versioned look
transforms before the output transform, never untracked replacement tone curves.

The logical order is:

1. produce unexposed linear ACEScg scene color;
2. execute scene-referred effects with declared exposure/pre-exposure semantics;
3. apply the view exposure and versioned creative look/grading;
4. run the target ACES output tonescale and gamut mapping into display-linear RGB;
5. compose explicitly display-referred UI at the target reference-white scale;
6. apply the accessibility color transform to the complete composed image;
7. contain any resulting out-of-gamut/non-finite values through the plan's final
   target-gamut policy, dither for the target bit depth, and encode the target
   transfer function exactly once; then
8. hand the typed output image to presentation.

Scene-referred overlays/effects join before the output transform. Display-referred
UI declares its reference-white mapping and joins after tone mapping; it cannot
write encoded sRGB/PQ values into a linear target. HDR UI brightness is bounded by
the plan rather than multiplied by display peak. Captured scene-linear EXR-like
artifacts are ACEScg before the output transform and carry metadata; display
screenshots capture the resolved post-transform encoding. The two are different
capture products.

### 5. SDR and HDR output contracts are distinct

The baseline interactive SDR output is **sRGB/Rec.709 primaries, D65 white, sRGB
transfer encoding** with a declared output bit depth and dithering policy. Video
export that requires BT.709 transfer characteristics is a different output
descriptor; “Rec.709” does not silently choose between display sRGB and video
encoding. SDR reference-white luminance is a positive finite product/output
setting recorded in the plan, not inferred from scene value `1.0`.

The first HDR output contract is **HDR10-style BT.2100 PQ**: Rec.2020 container
primaries, D65 white, PQ transfer encoding, at least 10-bit output, and finite
paper-white/target-peak values in nits. Content gamut may be smaller than the
Rec.2020 container and is mapped by the output transform. Mastering/content-light
metadata is computed or supplied through a versioned bounded policy and handed to
ADR-033's backend/presentation adapter; fabricated metadata is forbidden. ITU-R
[BT.2100](https://www.itu.int/rec/R-REC-BT.2100) defines the PQ/HLG HDR image
parameters; Horo selects PQ for its first HDR contract. HLG, scRGB, platform EDR
and video-export variants require separate named descriptors and qualification.

There is no universal hard-coded paper white or peak. The host/project supplies
admitted defaults, while the resolved display/output snapshot supplies actual or
unknown bounds with provenance. A required HDR request fails when required facts,
surface support, format, metadata path or output transform are missing. An `Auto`
request may select only its declared SDR fallback and records that choice.

Switching SDR/HDR, moving between displays or changing output parameters creates
a new output and color-plan generation. Scene rendering remains linear ACEScg;
only target-dependent output resources and incompatible display-referred histories
are rebuilt. The old valid output remains active until atomic publication where
ADR-033 permits it. A hotplug or HDR loss never causes double tone mapping, stale
PQ metadata, an encoded image in a linear attachment, or relabeling SDR as HDR.

### 6. Ownership, cooking and diagnostics

Asset Pipeline owns source color metadata validation, input/look/output transform
cooking, dependency digests and atomic publication. Exact ACES/configuration,
transform data, LUT dimensions/interpolation, source/target encodings and numeric
options participate in artifact identity. Runtime accepts only validated cooked
production transforms; packaged builds do not discover OCIO/ACES files, compile
arbitrary LUTs or use ambient monitor profiles as scene-authoring authority.

The renderer frontend owns exposure and `ColorPipelinePlan` resolution. The render
graph owns pass ordering and typed resources. Platform/presentation owns display
facts, surface encoding, HDR enablement and native metadata calls under ADR-033.
Editor owns controls and desired settings but displays requested versus active
plans separately. Backends translate the plan; they do not choose a gamut, curve,
paper white or fallback.

Diagnostics identify source asset/encoding, working/output plan and generations,
requested/actual output, exposure state, failing transform/pass, unsupported
format/transfer/metadata predicate, fallback reason and sanitization counts. Color
values, LUT payloads and captures are bounded and excluded from telemetry by
default. Device/output changes, cancellation and stale transform completion keep
the last good compatible plan or produce the typed suspended/lost behavior from
ADR-033.

## Rejected Alternatives

| Option | Decision and reason |
|---|---|
| Linear sRGB/Rec.709 as the scene working gamut | Rejected: it clips or distorts saturated wide-gamut lighting and requires earlier gamut mapping. It remains the baseline SDR output gamut. |
| Linear Rec.2020 as the universal working space | Not selected: ACEScg is explicitly designed for CGI/VFX work and provides a versioned ACES output-transform ecosystem. |
| ACES2065-1/AP0 for real-time shading | Rejected as the primary working space: AP0 is an interchange/archive encoding; ACEScg/AP1 is the CGI working space. |
| `R11G11B10_FLOAT` or native sRGB as canonical scene color | Rejected: neither preserves the complete signed RGBA16F working contract. |
| Backend-native tone curves and gamut conversion | Rejected: visually divergent output and no stable capture/qualification identity. |
| Reinhard, Uncharted-style or arbitrary filmic curve as automatic fallback | Rejected: a missing production transform is a typed failure. Named creative/diagnostic transforms require explicit versioned policy. |
| Treat scene value `1.0` as a fixed number of nits | Rejected: scene values are relative and exposure-dependent; nits belong to display output. |
| Use one unqualified `float exposure` | Rejected: sign, unit and physical-camera mapping are ambiguous. Horo uses base-2 stops with an explicit multiplier. |
| Render SDR separately from HDR | Rejected: duplicates lighting/effects and produces semantic drift. One scene-linear result feeds target-specific output plans. |
| Let an HDR-capable GPU enable HDR output | Rejected: display, OS, surface, format, metadata and host policy must all pass ADR-033 admission. |

The selected pipeline adds color-transform cooking, metadata, reference fixtures
and wide-gamut authoring discipline. It avoids per-backend looks and preserves one
scene rendering result across SDR and HDR outputs.

## Migration And Verification

Existing untagged colors and textures require role-based import migration with
recorded warnings; no bulk reinterpretation occurs at runtime. The current
`PostProcessSettings::exposure` migrates through a schema version to
`exposureCompensationEv` with the documented sign convention. Existing ACES,
Reinhard, Uncharted and Neutral enum values are not assumed compatible with the
new transform identities: projects select/migrate deliberately, and missing
production transforms retain the last good pipeline or fail validation.

Enforcement follows implementation readiness rather than this document landing:
RND-013.3 must ship the typed color/exposure contracts, cooked production
transform and a versioned editor/cook migration adapter before the old fields are
removed. The adapter recognizes legacy schema versions, reports every inferred or
unmappable value, stages a reversible project migration and keeps the last good
preview active on failure. Packaged cooks reject remaining unmigrated production
settings only after that migration path is available; backends do not begin hard
failure merely because ADR-037 is Proposed.

Current OpenGL/Metal viewport paths are not HDR-qualified merely because they use
floating attachments or EDR-capable APIs. Each must consume the same cooked
transform, exposure state, output plan and reference corpus before advertising
SDR/HDR parity.

| Delivery | Required implementation evidence |
|---|---|
| RND-013.2 / #392 | Typed post-process graph inputs/outputs and volume blending that preserve color/exposure generations. |
| RND-013.3 / #394 | Exposure state, ACES transform cooking/execution, grading, SDR/PQ plans and reference fixtures. |
| RND-013.5 / #396 | Motion/history metadata and deterministic invalidation/rescaling across exposure/color/output changes. |
| RND-013.10 / #399 | Cross-backend SDR/HDR image, metadata, display-transition and temporal qualification. |

Verification must include:

- ACEScg primary/white-point and input-transform reference vectors, tagged and
  inferred assets, data-texture bypass and double-decode rejection;
- RGBA16F extremes, finite negative/above-one values, NaN/Inf production and
  bounded final sanitization, plus declared float32 reduction references;
- exposure `+1/-1 EV`, manual/auto bounds, empty histograms, adaptation/reset,
  pre-exposure reconstruction and history rescale/invalidation;
- pinned ACES output reference vectors/images, LUT interpolation, transform/cache
  identity changes and rejection of missing/corrupt production transforms;
- SDR sRGB and HDR10 PQ transfer/gamut/bit-depth/dither results, paper-white/peak
  bounds, metadata agreement and no double encoding;
- display-referred UI/accessibility ordering, out-of-gamut containment and scene-
  versus display-capture metadata;
- HDR admission, Auto-to-SDR fallback, explicit-HDR failure, hotplug/display move,
  safe-point publication and last-good output retention; and
- native image comparison on every advertised backend with declared tolerances.
  Null tests prove math, plan and failure schedules but not display conformance.

## Consequences

Lighting, materials, effects and backends now share one color vocabulary and
numeric contract. Scene rendering is stable across SDR/HDR displays, exposure has
an unambiguous unit, and output conversion is versioned and testable. The cost is
mandatory RGBA16F support, cooked ACES transforms, explicit color metadata,
display-aware output plans and substantially stronger image qualification. This
decision does not itself enable HDR presentation or change current viewport output.
