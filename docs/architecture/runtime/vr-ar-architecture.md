# VR / AR Architecture

## Purpose

This document defines the virtual reality (VR) and augmented reality (AR)
subsystem for Horo Engine. It covers OpenXR integration, VR rendering
(instanced stereo, foveated rendering), VR input (motion controllers, hand
tracking), VR UI, VR-specific performance considerations, and AR pass-through
support.

## OpenXR Integration

Horo Engine uses OpenXR as the cross-platform VR/AR API:

```cpp
class XRSystem {
public:
    Result<void> Initialize(const XRInitSettings& settings);
    void Shutdown();

    // Session
    Result<void> BeginSession();
    void EndSession();

    // Frame
    XRFrameState WaitFrame();
    void BeginFrame();
    void EndFrame();

    // Views
    std::span<const XRView> GetViews() const;
    XRViewConfiguration GetViewConfiguration() const;

    // Input
    std::span<const XRActionState> GetActionStates() const;
    void SuggestBindings(std::string_view interactionProfile);
};
```

### Supported Runtimes

- **SteamVR**: HTC Vive, Valve Index, Windows Mixed Reality
- **Meta Quest**: Quest 2/3 via Oculus OpenXR runtime (native and Link)
- **PSVR2**: PlayStation VR2 via platform OpenXR
- **Varjo**: Varjo XR headsets
- **Monado**: Linux open-source runtime

### View Configuration

```cpp
struct XRViewConfiguration {
    XRViewType       type;           // Stereo, Mono, Foveated
    uint32_t         viewCount;      // 2 for stereo, 1 for mono
    uint32_t         recommendedWidth;
    uint32_t         recommendedHeight;
    float            refreshRate;
};
```

## VR Rendering

### Instanced Stereo

Stereo rendering uses GPU instancing to render both eyes in a single draw call:

- Each eye is a separate view instance
- View matrices and projection matrices are stored in a uniform buffer array
- The shader receives a backend-neutral view-instance index to select the correct view matrix
- Reduces draw call count by ~50% compared to two-pass stereo

```cpp
struct XRRenderViews {
    Matrix4    viewMatrices[2];
    Matrix4    projectionMatrices[2];
    Vector4    eyeOffsets[2];       // per-eye offset for stereo reconstruction
};
```

### Foveated Rendering

On headsets with eye tracking, foveated rendering reduces shading cost:

- The render target is rendered at variable resolution
- Periphery regions are rendered at lower resolution
- The fovea (where the user is looking) is rendered at full resolution
- Resolution falloff is guided by the eye tracking data

```cpp
struct FoveatedRenderingSettings {
    bool       enabled;
    float      foveaInnerRadius;    // full-resolution region
    float      foveaOuterRadius;    // transition region
    float      peripheryScale;      // resolution scale in periphery (e.g., 0.25)
    bool       useEyeTracking;
};
```

Foveated rendering is exposed as a backend-neutral `FoveatedRenderFeature` capability. Backends may implement it with native variable-rate shading or a shader-based density mask, but those details stay below the RHI contract.

### Performance Budget

VR rendering has strict performance requirements:

- 90 Hz minimum (11.1 ms per frame) for comfortable VR
- 72 Hz acceptable on standalone headsets
- Late-warp (asynchronous timewarp) compensates for missed frames
- GPU preemption support for guardian/chaperone rendering

## VR Input

### Motion Controllers

Motion controllers expose position, rotation, and button state:

```cpp
struct XRControllerState {
    XRHand          hand;            // Left, Right
    WorldTransform  gripPose;
    WorldTransform  aimPose;
    Vector2         thumbstick;
    float           trigger;         // 0-1 analog
    float           grip;            // 0-1 analog
    ButtonFlags     buttons;
};
```

### Hand Tracking

Optical hand tracking provides per-joint data:

```cpp
struct XRHandJoint {
    XRHandJointId    jointId;        // wrist, thumb tip, index tip, etc.
    WorldTransform   pose;
    float            radius;
    float            confidence;     // 0-1 tracking confidence
};

struct XRHandState {
    XRHand                   hand;
    std::vector<XRHandJoint> joints;
    XRGesture                activeGesture;  // pinch, point, grab, open hand
};
```

Hand tracking is used for natural interaction: grabbing objects, pointing at
UI, gesture-based commands.

### Locomotion

VR locomotion modes:

- **Teleport**: Point at destination and teleport instantaneously
- **Smooth locomotion**: Continuous joystick movement
- **Snap turn**: Discrete rotation steps (comfort setting)
- **Arm-swing**: Physics-based movement by swinging arms
- **Room-scale**: Physical walking within the tracked space

Comfort settings (vignette during movement, snap turn angle, movement speed)
are configurable in accessibility settings.

## VR UI

UI in VR uses world-space canvases:

- UI panels are placed in 3D space
- Laser pointer or hand ray for interaction
- Proximity-based activation for touchscreens
- Diegetic UI (in-world screens and displays) is supported

```cpp
struct VRUICanvas {
    WorldTransform    transform;
    Vector2           size;            // world-space meters
    float             interactionDistance;
    bool              useLaserPointer;
    bool              useHandRay;
};
```

## AR / Mixed Reality

AR support uses the same OpenXR API:

- Passthrough: Camera feed as background
- Spatial anchors: Persistent world-locked objects
- Plane detection: Real-world surface detection for placement
- Ray casting: Camera-based hit testing
- Light estimation: Match virtual lighting to real-world ambient

AR features are available on Meta Quest (passthrough) and Varjo XR (video
see-through).

## Performance And Feature Tiers

| Feature              | `es3`      | `dx11` / `dx12_vulkan` | `high_end` |
| -------------------- | ----------- | ------------- | ------------ |
| VR rendering         | No          | Stereo        | Stereo + foveation |
| Foveated rendering   | No          | Fixed         | Eye-tracked  |
| Hand tracking        | No          | Basic         | Full joints  |
| AR passthrough       | No          | No            | Yes          |
| Instanced stereo     | No          | Yes           | Yes          |
| Refresh rate         | —           | 90 Hz         | 120 Hz       |

## Related Documents

- [XR Setup UI Reference](./xr-setup.html)

- [Rendering Architecture](./rendering-architecture.md): VR stereo rendering and foveated-rendering capabilities
- [Input Architecture](./input-architecture.md): motion controller and hand tracking input
- [Game UI And HUD](./game-ui-and-hud.md): VR UI canvases
- [Accessibility Architecture](./accessibility-architecture.md): VR comfort settings
- [Platform Services Architecture](./platform-services-architecture.md): platform VR runtime integration
- [Post-Processing And Effects](./post-processing-and-effects-architecture.md): foveated rendering as post-process
