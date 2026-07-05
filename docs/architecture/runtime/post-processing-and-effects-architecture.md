# Post-Processing And Effects Architecture

## Purpose

This document defines the post-processing stack and screen-space effects for
Horo Engine. It covers the post-process volume system, individual effects
(bloom, depth of field, motion blur, ambient occlusion, tonemapping, color
grading), effect ordering, feature tiers, and GPU performance budgets.

## Post-Process Volume

Post-processing settings are defined by post-process volumes in the scene:

```cpp
struct PostProcessVolume {
    float             priority;
    PostProcessBlendRadius blendRadius;  // 0 = hard cut, >0 = smooth blend
    PostProcessSettings settings;
};
```

The active settings are computed by blending all volumes overlapping the
camera, weighted by priority and blend radius.

### Settings Structure

```cpp
struct PostProcessSettings {
    // Bloom
    std::optional<BloomSettings> bloom;

    // Depth of Field
    std::optional<DepthOfFieldSettings> depthOfField;

    // Motion Blur
    std::optional<MotionBlurSettings> motionBlur;

    // Ambient Occlusion
    std::optional<AmbientOcclusionSettings> ambientOcclusion;

    // Screen-Space Reflections
    std::optional<ScreenSpaceReflectionsSettings> ssr;

    // Tonemapping
    TonemapMode tonemapMode;
    float       exposure;

    // Color Grading
    std::optional<AssetId> colorGradingLUT;   // 3D LUT texture
    ColorGradingSettings colorGrading;

    // Vignette
    std::optional<VignetteSettings> vignette;

    // Chromatic Aberration
    std::optional<ChromaticAberrationSettings> chromaticAberration;

    // Film Grain
    std::optional<FilmGrainSettings> filmGrain;
};
```

## Effect Pipeline

### Render Graph Integration

Post-processing is implemented as render graph passes:

```
SceneColor ──→ SSAO ──→ SSR ──→ DoF ──→ MotionBlur ──→ Bloom ──→
Tonemap ──→ ColorGrade ──→ Vignette ──→ FilmGrain ──→ FinalOutput
```

Each pass is a compute or fragment shader that reads the previous output and
writes to an intermediate render target. Passes can be enabled/disabled
dynamically. The render graph compacts the chain at build time, merging
compatible passes when possible.

### Pass Dependencies

| Pass                | Input              | Output             | GPU Budget (1080p) |
| ------------------- | ------------------ | ------------------ | ------------------- |
| SSAO                | Depth, Normal      | AO buffer          | 0.3 ms              |
| SSR                 | SceneColor, Depth  | Reflection buffer  | 0.8 ms              |
| Depth of Field      | SceneColor, Depth  | Defocused color    | 1.2 ms              |
| Motion Blur         | SceneColor, Velocity| Blurred color     | 0.6 ms              |
| Bloom               | SceneColor         | Bloom composite    | 0.5 ms              |
| Tonemap + Grade     | HDR color          | LDR color          | 0.2 ms              |
| Vignette + Grain    | LDR color          | Final output       | 0.1 ms              |

## Individual Effects

### Bloom

```cpp
struct BloomSettings {
    float  threshold;       // luminance above which pixels bloom
    float  intensity;
    float  scatter;         // bloom spread (controls downsample chain)
    uint32_t quality;       // downsample levels
};
```

Bloom uses a downsample-upsample chain with separable Gaussian blurs.

### Depth Of Field

```cpp
struct DepthOfFieldSettings {
    float  focusDistance;    // world units from camera
    float  apertureFStop;    // lower = shallower DoF
    float  focalLength;      // mm
    uint32_t quality;        // sample count for bokeh
};
```

DoF uses a circle-of-confusion based approach with bokeh shape sampling.

### Motion Blur

```cpp
struct MotionBlurSettings {
    float  intensity;
    float  maxBlurPixels;
    uint32_t sampleCount;
};
```

Motion blur reconstructs per-pixel velocity from the previous frame's camera
matrix and object transforms. Uses a velocity buffer generated during the
GBuffer pass.

### Ambient Occlusion

```cpp
struct AmbientOcclusionSettings {
    SSAOMode mode;            // GTAO, HBAO+, SSAO
    float    radius;
    float    intensity;
    float    power;
    uint32_t quality;         // sample count
};
```

### Tonemapping

Tonemapping modes:

- **ACES**: Academy Color Encoding System filmic curve
- **Reinhard**: Simple luminance compression
- **Uncharted 2**: Naughty Dog's filmic curve
- **Neutral**: Linear with exposure only

### Color Grading

```cpp
struct ColorGradingSettings {
    float  temperature;      // Kelvin
    float  tint;
    float  saturation;
    float  contrast;
    float  gamma;
    Vector4 colorWheels[3];  // shadows, midtones, highlights
};
```

A 3D LUT texture can be applied for full creative grading.

## Performance And Feature Tiers

| Feature              | `es3`           | `dx11`           | `dx12_vulkan`    | `high_end`       |
| -------------------- | ---------------- | ----------------- | ----------------- | ----------------- |
| Bloom                | 4 downsamples    | 7 downsamples     | 7 downsamples     | 9 downsamples     |
| DoF                  | Gaussian blur    | Circle bokeh      | Full bokeh        | Full bokeh        |
| Motion blur          | Camera only      | Camera + object   | Full per-object   | Full per-object   |
| SSAO                 | SSAO (8 samples) | HBAO+ (16)        | GTAO (16)         | GTAO (32)         |
| SSR                  | No               | Half-res          | Full-res          | Full-res + trace  |
| 3D LUT               | No               | 16×16×16          | 32×32×32          | 32×32×32          |

## Editor Integration

Post-process volumes are placed as scene objects:

- Volume gizmo in the viewport for placement and resizing
- Volume priority and blend radius editing in the inspector
- Post-process settings panel with live preview

The editor viewport renders with post-processing when "Lit" or "Shaded" view
mode is active. Individual effects can be toggled in the viewport toolbar for
debugging.

## Related Documents

- [Post-Processing Stack UI Reference](./post-processing-stack.html)

- [Rendering Architecture](./rendering-architecture.md): render graph and pass definitions
- [Advanced Rendering Architecture](./advanced-rendering-architecture.md): ray-traced effects
- [Material And Shader Model](./material-and-shader-model.md): shader permutations for effects
- [Observability Performance](../observability/observability-performance.md): GPU timing for post-process passes
- [Editor Panel Host](../editor/editor-panel-host.md): post-process volume inspector
