# Character Controller Architecture

## Purpose

This document defines Horo Engine's character controller and surface interaction
runtime. It specifies the kinematic character controller, slope and step
handling, moving platforms, physics surface events, and the contract between
animation root motion, gameplay movement, and the physics world.

The goal is to give gameplay systems a stable, deterministic way to move
characters in a physics world without exposing the full complexity of rigid-body
dynamics to every gameplay module.

[ADR-061](../../adr/061-animation-ownership-update-order-and-clock.md) owns the
fixed-tick animation/root-motion ordering and pose handoff. This document owns
movement admission, collision resolution, and the resulting transform.

## Scope

Covered:

- kinematic character controller
- capsule controller geometry and queries
- ground detection and slope handling
- step climbing and step-down behavior
- moving platform attachment and velocity transfer
- surface material detection and events
- integration with animation root motion
- integration with scene runtime and physics world

Not covered:

- AI pathfinding and navigation mesh (see future navigation document)
- vehicle physics
- ragdoll physics (see [Physics Architecture](./physics-architecture.md))
- full gameplay locomotion state machines (gameplay module concern)

## Core Decisions

- The character controller is kinematic. It queries physics and moves by
  explicit position updates, not by simulating a dynamic body.
- The controller owns a single capsule collider used for collision queries.
- Gameplay sets desired velocity; the controller resolves collisions and
  reports the final displacement.
- Slope and step handling are deterministic and configurable per controller.
- Moving platforms transfer velocity and optionally angular velocity to
  standing characters.
- Surface materials drive friction, footstep audio, VFX, and gameplay events.
- Root motion from animation may feed into the controller as a delta request,
  but the controller decides the final transform.

## Character Controller Component

```cpp
struct CharacterControllerDescriptor {
    float radius;
    float height;
    float stepOffset;
    float slopeLimitDegrees;
    float skinWidth;
    float minMoveDistance;
    Vec3 gravity;
    PhysicsMaterialId defaultMaterial;
};
```

The component attaches to a scene object. The transform component owns position
and orientation. The controller reads and writes the transform each frame.

## Update Order

```text
Attempted Fixed Tick
  Gameplay stages desired movement and animation parameters
  Animation evaluates and stages the exact tick's root motion request
  Character controller resolves movement
  Character transform and moving-platform result are published
  Physics steps once
  Post-physics animation pose override/finalization runs
  Tick commit publishes previous/current state
```

The controller runs once after animation root motion is staged and before each
physics fixed step. Catch-up repeats the complete sequence; it does not reuse one
render-frame root delta. This ensures gameplay-driven movement is resolved into a
final transform before physics reacts during the same attempted tick.

## Movement Resolution

### Input

```cpp
struct MovementRequest {
    Vec3 desiredVelocity;       // world-space, meters/second
    Quat desiredOrientation;
    bool jumpRequested;
    bool crouchRequested;
    FixedDeltaTime deltaTime;
};
```

### Output

```cpp
struct SurfaceContact {
    Vec3 point;                  // contact point in world space
    Vec3 normal;                 // surface normal at contact
    SurfaceMaterialId material;  // resolved surface material (default fallback applied)
    PhysicsBodyId body;          // optional; invalid if hit static world
    float penetrationDepth;      // signed depth; negative for separation
};

struct MovementResult {
    Vec3 finalPosition;
    Quat finalOrientation;
    bool isGrounded;
    float groundSlopeDegrees;
    SurfaceMaterialId groundSurface;  // never invalid; falls back to defaultMaterial
    Vec3 groundNormal;
    bool hitCeiling;
    std::vector<SurfaceContact> contacts;
};
```

### Collision Pass

The controller resolves movement in passes:

1. **Ground detection** — cast a sphere/capsule probe downward to find standing
   surface.
2. **Step up** — if blocked by a vertical obstacle within `stepOffset`, attempt
   to step onto it.
3. **Horizontal movement** — sweep the capsule along desired horizontal velocity,
   slide against obstacles.
4. **Vertical movement** — apply gravity and jump velocity, sweep vertically.
5. **Step down** — after horizontal movement, snap to ground if within step
   offset.
6. **Surface material query** — read surface material from touched colliders.

All sweeps use the physics query API, not direct transform mutation.

## Ground Detection

Ground detection determines whether the character is standing on a surface.

Rules:

- a downward sweep within `skinWidth + smallEpsilon` must hit a surface
- the hit normal's angle from world up must be less than or equal to
  `slopeLimitDegrees`
- the hit collider must be on the ground collision layer
- dynamic bodies do not count as ground unless configured

If no valid ground is found, the character is airborne.

## Slope Handling

Slopes within `slopeLimitDegrees` are walkable. Slopes above the limit block
movement unless the character is sliding down.

Options:

- `preventSlidingOnWalkableSlopes` — apply counter-force along slope normal
- `slideDownSteepSlopes` — apply gravity along the steep surface
- `preserveHorizontalSpeedOnSlopes` — adjust velocity to maintain requested
  ground-plane speed

## Step Handling

Step climbing:

- detect obstacle in movement direction
- test whether the top of the obstacle is within `stepOffset`
- move the capsule up by step height
- sweep horizontally
- step down onto the new surface

Step down:

- after horizontal movement, sweep down by `stepOffset`
- if a valid ground is found, snap to it
- preserve momentum if the drop is significant

Step behavior is configurable:

- `maxStepHeight`
- `minStepDepth`
- `stepSpeed` — how fast the character visually ascends

## Moving Platforms

When the character is grounded on a moving platform, the controller must track
the platform's transform and velocity.

```cpp
struct MovingPlatformAttachment {
    PhysicsBodyId platformBody;
    Transform localTransformOnPlatform;
    Vec3 platformLinearVelocity;
    Vec3 platformAngularVelocity;
};
```

Rules:

- attachment is established on ground contact with a platform body
- local offset is stored in platform space
- next frame, the character is moved by platform delta before its own movement
- platform angular velocity may rotate the character
- detachment happens when the character leaves the platform, becomes airborne, or
  is teleported

## Surface Materials

Surface materials describe the physical and gameplay properties of a collider.

```cpp
struct SurfaceMaterial {
    SurfaceMaterialId id;
    float staticFriction;
    float dynamicFriction;
    float restitution;
    SurfaceType surfaceType;       // e.g., Metal, Wood, Concrete, Grass
    AssetId footstepSoundSet;      // variation container reference
    AssetId impactSoundSet;
    AssetId bulletImpactEffect;    // VFX asset reference
    AssetId footstepEffect;
};
```

Surface materials are assigned to colliders, not to meshes. A single mesh may
use multiple surface materials through material IDs or collision sub-shapes.

## Surface Events

The controller emits events based on contacts and state changes.

Events:

| Event | Trigger |
|---|---|
| `Footstep` | Foot touches ground during locomotion. |
| `Landed` | Transition from airborne to grounded. |
| `LeftGround` | Transition from grounded to airborne. |
| `HitWall` | Horizontal movement blocked by surface. |
| `HitCeiling` | Vertical movement blocked above. |
| `SurfaceChanged` | Ground surface material changed. |
| `SlideStart` / `SlideEnd` | Started/stopped sliding on steep slope. |

Events carry:

- surface material ID
- contact point and normal
- impact velocity
- controller reference

Gameplay and audio systems subscribe to these events. The controller does not
play sounds or spawn VFX directly.

## Audio And VFX Coupling

Surface events drive audio and VFX:

- `Footstep` -> play randomized footstep sound from `footstepSoundSet`
- `Landed` -> play landing sound scaled by impact velocity
- `HitWall` / `HitCeiling` -> play impact sound and optionally spawn decal/VFX
- `SurfaceChanged` -> update active movement audio loop (e.g., footstep material)

The controller emits events; the audio and VFX systems resolve the actual asset
playback. This preserves mixing, spatialization, and budgets.

## Root Motion Integration

Animation root motion may provide a movement delta.

```cpp
enum class RootMotionPriority {
    Suggest,     // root motion is applied only if gameplay input is zero
    Override     // root motion replaces gameplay input for this fixed tick
};

struct RootMotionRequest {
    Vec3 deltaTranslation;
    Quat deltaRotation;
    bool applyToPosition;
    bool applyToOrientation;
    RootMotionPriority priority = RootMotionPriority::Suggest;
};
```

The controller treats root motion as a movement request and resolves it through
the same collision passes. This ensures that animation-driven movement still
respects walls, slopes, and steps.

The request identifies its scene, animation instance, simulation tick, and root-
motion generation. Controller resolution stages a consumption marker in the tick
transaction; only successful tick commit makes that marker durable. Aborting an
attempt discards its marker and request lease, so a retry of the same simulation
tick can consume the newly staged equivalent request. After a successful commit,
another request with that identity is a duplicate and is rejected. Presentation/
editor-preview, stale, duplicate-after-commit, or leaked aborted-attempt requests
are invalid. Reverse player traversal may submit the inverse directed delta when
animation and gameplay policy admit it; the controller still performs ordinary
forward collision resolution and does not reverse physics.

If root motion and gameplay input conflict, gameplay input takes precedence
unless the root motion request has `RootMotionPriority::Override`. The animation
or gameplay system that produces the request decides the priority; the
controller only resolves the resulting movement.

## Physics Material Query

During sweeps, the controller reads the physics material from hit colliders.

```cpp
std::optional<SurfaceMaterialId> QuerySurfaceMaterial(const PhysicsHit& hit);
```

If a collider has no surface material, the controller uses its default material.
`MovementResult.groundSurface` and `SurfaceContact.material` always contain a
valid material ID; the fallback from `QuerySurfaceMaterial` to the descriptor's
`defaultMaterial` is applied inside the controller before the result is returned.

## Collider Filtering

The controller uses collision layers and masks:

- character controller layer
- static world layer
- dynamic body layer
- platform layer
- trigger/volume layer (ignored by movement sweeps; trigger enter/exit events are
  handled by the gameplay volume system or physics callbacks, not controller
  surface events)

Layer membership is declared on the collider, not on the controller.

## Crouching And Size Changes

The controller supports runtime size changes:

- crouch reduces capsule height
- a resize request checks for ceiling clearance
- if clearance is insufficient, crouch is rejected or the character is forced
  into crouch until space is available
- size changes do not alter `stepOffset`; if crouched geometry requires different
  step behavior, the gameplay system must use a separate controller descriptor or
  override the movement request accordingly

Size changes are gradual unless configured as immediate.

## Validation And Safety

- The controller rejects invalid descriptors (negative radius, zero height).
- If the capsule is inside geometry on spawn, it attempts depenetration within
  a budget; otherwise it reports an error.
- Teleports bypass collision but flag the controller as needing ground
  re-evaluation. A teleport also detaches the character from any moving platform.
- Extreme velocities are clamped before sweeping to avoid tunneling.

## Diagnostics

Debug visualization:

- capsule shape
- ground probe ray
- step probe arcs
- contact points and normals
- surface material labels
- moving platform attachment line

Runtime variables:

- `cc.drawDebug`
- `cc.showSurfaceEvents`
- `cc.logTunnelingWarnings`

## Testing Requirements

- Unit tests for capsule-sweep resolution against simple primitives.
- Slope limit tests.
- Step up/down tests on known geometry.
- Moving platform attachment and detachment tests.
- Surface event emission tests.
- Root motion collision tests.
- Root motion duplicate/stale generation and reverse-policy tests.
- Determinism tests for fixed-step playback under different render/catch-up grouping.
- Performance tests for many concurrent controllers.

## Related Documents

- [Character Setup UI Reference](./character-setup.html): capsule, movement parameters, camera, and input bindings panel.

- [Physics Architecture](./physics-architecture.md): collision queries,
  materials, rigid bodies, and fixed-step world.
- [Animation Architecture](./animation-architecture.md): root motion and
  animation-driven movement.
- [Audio Architecture](./audio-architecture.md): audio event routing and
  variation containers.
- [VFX And Particles Architecture](./vfx-and-particles-architecture.md): impact
  effects and decals.
- [Scene Runtime](./scene-runtime.md): transform ownership and update order.
