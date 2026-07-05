# Animation Architecture

## Purpose

This document defines Horo Engine's animation runtime. It specifies the skeletal
animation data model, animation clips, pose evaluation, animation graphs, blend
trees, inverse kinematics, root motion, retargeting, animation events, and the
integration between animation, physics, and rendering.

The goal is to provide a deterministic, data-oriented animation runtime that can
drive characters from simple loops to complex gameplay-responsive locomotion and
cinematics.

## Scope

Covered:

- skeleton asset format and pose representation
- animation clip asset format and compression
- animation sampling and evaluation
- pose storage, write-back, and skinning data flow
- animation graphs, blend trees, and state machines
- animation events and notifies
- inverse kinematics (IK)
- root motion
- retargeting
- GPU vs CPU skinning boundary
- integration with scene runtime, physics, and render extraction

Not covered:

- specific animation tools or DCC exporters (see [Asset Pipeline](./asset-pipeline.md))
- facial animation systems
- cloth or hair simulation
- ragdoll physics (see [Physics Architecture](./physics-architecture.md))

## Core Decisions

- Animations are data assets. The runtime samples them; it does not author them.
- Poses are the primary runtime currency: animation outputs a pose, IK modifies
  it, physics may override parts of it, and skinning consumes it.
- The animation update runs as part of the runtime frame, before render
  extraction. Fixed-step physics does not drive animation timing.
- Animation graphs are authored assets that compile into a runtime evaluation
  tree. They are not interpreted scripts.
- Root motion is explicit: animation provides a delta transform; gameplay code
  decides whether to consume it.
- Retargeting is a cook/import-time process where possible. Runtime retargeting
  is supported but more expensive.
- GPU skinning is the default for skinned meshes. CPU skinning is a fallback for
  debugging, low-end platforms, or custom deformation requirements.

## Skeleton

A skeleton is a hierarchy of joints. Each joint has:

- stable name and index
- parent index (`-1` for root)
- inverse bind matrix
- local-space default transform
- optional metadata (mirror joint, IK hint, retargeting bone type)

```cpp
struct Joint {
    JointName name;
    JointIndex parent;
    Mat4 inverseBindMatrix;
    Transform defaultLocalTransform;
};

struct Skeleton {
    SkeletonId id;
    std::vector<Joint> joints;
    std::vector<JointIndex> rootJoints;
};
```

Skeleton assets are shared. Multiple meshes and animation clips reference the
same skeleton.

## Pose

A pose is a snapshot of skeleton joint transforms in local or model space.

```cpp
struct Pose {
    const Skeleton* skeleton;
    std::vector<Transform> localTransforms;   // local per joint
    std::vector<Mat4> modelSpaceMatrices;     // computed on demand
    uint32_t dirtyFromJoint;                  // for incremental update
};
```

Rules:

- Local transforms are authoritative.
- Model-space matrices are computed once per frame on first access, then cached
  until the next pose evaluation invalidates them.
- Poses are owned by the animation system and leased to gameplay/physics for
  read-only inspection unless explicit write-back is declared.
- Poses are allocated from a frame-local pool to avoid heap allocations during
  playback.

## Animation Clip

A clip stores sampled animation data for one skeleton.

```cpp
struct AnimationClipDescriptor {
    ClipId id;
    SkeletonId skeleton;
    float durationSeconds;
    float sampleRate;
    WrapMode wrapMode;
    bool hasRootMotion;
    AnimationCompressionScheme compression;
};
```

Per-joint curves store:

- translation
- rotation (quaternion, shortest-path interpolation)
- scale

Compression options:

- `None` — full precision
- `Linear` — linear key reduction
- `Adaptive` — error-tolerant key reduction per joint

Additive clips (difference clips applied relative to a reference pose) are a
separate clip kind, not a compression scheme. A clip may be both additive and
compressed.

Cook-time compression may reduce precision based on the active cook profile.

### Clip Sampling

```cpp
Pose SampleClip(const AnimationClip& clip, float time, const Skeleton& skeleton);
```

Behavior:

- time is wrapped according to `WrapMode` (`Once`, `Loop`, `PingPong`, `ClampForever`)
- keyframe interpolation is linear or spline depending on clip metadata
- rotations are normalized after interpolation
- additive clips are applied relative to the current pose

## Animation Graph

An animation graph is a directed acyclic graph of nodes that produces a final
pose each frame.

```text
AnimationGraph
  +-- Output Pose
      +-- Blend Node
          +-- State Machine
          +-- IK Pass
          +-- Aim Offset
```

Graph assets compile to a runtime `AnimationGraphInstance`. The graph owns its
parameter block and pose working memory.

### Graph Parameters

Parameters are typed values exposed to gameplay:

```text
float speed
float direction
bool isAiming
int weaponSlot
```

Parameters are set by gameplay systems before graph evaluation. The graph does
not read raw input devices directly.

### Node Types

Core nodes:

| Node | Purpose |
|---|---|
| `Clip` | Sample a single animation clip. |
| `Blend` | Blend two poses by a float parameter. |
| `BlendTree` | Multi-directional blend by two axes (e.g., speed/direction). |
| `StateMachine` | Discrete states with transition rules and blend times. |
| `Layer` | Overlay an upper-body animation on a base pose. |
| `IK` | Modify a pose to reach a target. |
| `AimOffset` | Additive aim pose selected by pitch/yaw. |
| `Mirror` | Mirror a pose across a skeleton axis. |

Nodes produce poses; they do not allocate per evaluation. Memory is owned by the
graph instance.

### State Machine

A state machine node contains states and transitions.

- each state references a sub-graph or clip
- transitions have duration, curve, and trigger conditions
- trigger conditions should use hysteresis to avoid oscillation when a parameter
  hovers near a threshold
- transitions can be interrupted by higher-priority transitions
- state changes are deterministic given the same parameter inputs

Example states:

```text
Idle
  -> Walk (speed > 0.1)
Walk
  -> Run (speed > 3.0)
  -> Idle (speed < 0.1)
Run
  -> Walk (speed < 2.5)
```

## Blend Trees

A blend tree blends multiple clips based on up to two float parameters.

Common patterns:

- 1D blend by speed
- 2D directional blend by direction and speed
- freeform Cartesian blend

Interpolation rules:

- rotations use spherical linear interpolation (SLERP) or normalized lerp
- translations and scales use linear interpolation
- blend weights are normalized and clamped

## Inverse Kinematics

IK nodes modify a pose after animation sampling.

Supported solvers:

| Solver | Use case |
|---|---|
| Two-bone IK | Elbow/knee bending for foot/hand placement. |
| CCD | Longer chains, less stable but simple. |
| FABRIK | Longer chains, more stable. |
| Look-at | Head/aim direction. |
| Foot-lock / foot-plant | Prevent foot sliding on slopes and stairs. |

IK inputs:

- target position/rotation in model or world space
- hint position for elbow/knee polarity
- weight and blend curve

IK is applied after graph sampling and before physics read. Physics therefore
sees the IK-modified pose for ragdoll initialization, hit-detection shapes, and
any other pose query.

## Root Motion

Root motion produces a transform delta each frame from the animation clip's root
joint.

```cpp
struct RootMotionDelta {
    Vec3 translation;
    Quat rotation;
};
```

Rules:

- root motion is optional per clip
- gameplay chooses whether to consume the delta for character movement
- networked games must replicate root motion parameters or resulting transforms,
  not raw animation time
- root motion deltas are accumulated and applied during the gameplay movement
  update, not inside the animation system

## Retargeting

Retargeting maps animation data from a source skeleton to a target skeleton.

### Cook-Time Retargeting

When source and target skeletons are known at cook time, retargeting bakes a
new clip for the target skeleton. This is the preferred path for performance.

### Runtime Retargeting

Runtime retargeting uses a retargeting profile that maps joints by name or
retargeting bone type. It is supported for dynamic sources (e.g., gameplay
packages, user-generated content) but costs more per frame.

Retargeting rules:

- source pose is normalized to a reference pose
- rotations are transferred using retargeting bone type semantics
- translation retargeting scales by skeleton proportions
- root motion is recomputed for the target skeleton when the source and target
  have different proportions or root joint conventions; otherwise it may be
  reused directly. The retargeting profile declares which path to use.

## Animation Events

Animation events are named markers attached to a clip at a specific time.

```cpp
struct AnimationEvent {
    EventName name;
    float timeSeconds;
    std::optional<float> durationSeconds;
    VariantMap payload;  // typed key-value map; see foundation type glossary
};
```

Event types:

- `Footstep` — spawn sound, VFX, decal
- `WeaponSwing` — enable/disable hit box
- `ReloadComplete` — gameplay notification
- `SpawnProjectile` — fire event
- `Custom` — gameplay-defined

The animation system emits events during evaluation. Consumers subscribe by
name. Events do not directly spawn gameplay objects; they queue commands for the
frame update.

## Skinning

Skinning transforms mesh vertices by joint matrices.

### GPU Skinning

Default path. The animation system uploads:

- joint matrices palette (model-space transform × inverse bind matrix)
- per-vertex joint indices and weights

The renderer consumes this in the vertex shader. Joint palette updates happen
once per skinned mesh instance per frame.

### CPU Skinning

Fallback path. The animation system computes deformed vertices on the CPU and
uploads the resulting vertex buffer. Used for:

- platforms without viable GPU compute
- debugging
- custom deformation not expressible in vertex shaders

CPU skinning must be explicitly enabled per mesh and is slower.

## Integration

### Scene Runtime

Skinned meshes and animation graph components live in the scene runtime. During
scene conversion:

- skeleton references are resolved
- clips are loaded or referenced
- graph instances are created
- skinned mesh render instances are registered

### Update Order

Animation produces a pose once per rendered frame. Physics may step multiple
times per frame at a fixed interval; the animation pose is sampled before those
steps and is not re-sampled inside a physics step.

```text
Frame Update
  Gameplay sets animation parameters
  Animation graph evaluates -> pose
  IK applied -> modified pose
  Root motion delta produced
  Gameplay consumes root motion (character controller resolves movement)
  Physics fixed-step(s)
  Render extraction reads final pose and joint palette
```

The character controller runs during the gameplay movement phase, before the
physics world is stepped. This matches the ordering described in
[Character Controller Architecture](./character-controller-architecture.md).

### Render Extraction

The renderer receives:

- transform
- mesh asset
- material
- joint palette handle or pointer
- bounds

Bounds for skinned meshes must account for pose extents. Conservative bounds may
be used when exact pose bounds are too expensive.

### Physics

Physics reads pose for ragdoll initialization or hit-detection shapes. Physics
does not write pose unless a ragdoll or procedural physics override is active.
Animation-to-physics handoff is explicit and event-driven.

## Asset Formats

### Skeleton Asset

```json
{
  "schemaVersion": 1,
  "assetType": "skeleton",
  "joints": [
    {
      "name": "Hips",
      "parent": -1,
      "inverseBindMatrix": [...],
      "defaultTranslation": [0, 1, 0]
    }
  ]
}
```

### Animation Clip Asset

```json
{
  "schemaVersion": 1,
  "assetType": "animation_clip",
  "skeleton": "skeleton_humanoid_001",
  "duration": 1.0,
  "sampleRate": 30,
  "wrapMode": "Loop",
  "hasRootMotion": true,
  "tracks": [
    {
      "joint": "Hips",
      "translationKeys": [...],
      "rotationKeys": [...],
      "scaleKeys": [...]
    }
  ],
  "events": [
    { "name": "Footstep.Left", "time": 0.25 }
  ]
}
```

### Animation Graph Asset

```json
{
  "schemaVersion": 1,
  "assetType": "animation_graph",
  "skeleton": "skeleton_humanoid_001",
  "parameters": [
    { "name": "speed", "type": "float", "default": 0.0 }
  ],
  "nodes": [...],
  "outputNode": "..."
}
```

## Diagnostics And Validation

- Skeleton mismatch between clip and mesh produces errors at import time.
- Graph cycles are rejected at compile time.
- Missing clips or parameters are reported with asset paths.
- IK targets outside reachable range degrade gracefully rather than exploding.
- Root motion clips used without root joint metadata produce warnings.

## Testing Requirements

- Unit tests for pose hierarchy updates and matrix multiplication.
- Sampling tests for loop/ping-pong wrap modes.
- Blend tree tests verifying normalized weights and rotation correctness.
- IK solver tests for known configurations.
- Retargeting tests comparing source and target poses.
- Visual regression tests for skinned mesh playback.
- Performance tests for joint palette upload and GPU skinning throughput.

## Related Documents

- [Animation Editor UI Reference](./animation-editor.html): clip timeline, state machine, blend tree, events, and skeleton preview panel.

- [Rendering Architecture](./rendering-architecture.md): skinned mesh render
  extraction and joint palette binding.
- [Physics Architecture](./physics-architecture.md): ragdoll, hit detection, and
  animation/physics handoff.
- [Asset Pipeline](./asset-pipeline.md): clip import, compression, and cook.
- [Advanced Rendering Architecture](./advanced-rendering-architecture.md):
  meshlets, GPU-driven rendering, and virtual geometry boundaries.
