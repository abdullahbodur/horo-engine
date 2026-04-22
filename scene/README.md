# Scene Module

`scene/` provides the ECS layer and engine systems that connect gameplay data to renderer/physics.

## Responsibilities

- Entity lifecycle and component storage (`Registry`, `ComponentPool<T>`)
- Scene-level orchestration (`Scene`)
- System interface and update pipelines (`System`)
- Built-in components (`scene/components/`)
- Built-in systems (`scene/systems/`)
- Typed scene/project contract shared by editor and runtime (`SceneProjectModel`)
- Engine-owned runtime build input and conversion (`RuntimeSceneDefinition`, `BuildRuntimeSceneDefinition`)
- Lifecycle-managed runtime orchestration (`SceneRuntimeCoordinator`)
- Engine-owned reference loading path for in-repo examples and tests (`SceneReferenceRuntime`)

## ECS Design

- Entities are lightweight integral IDs (`Entity`).
- `Registry` is type-indexed and creates component pools lazily on first `Add<T>()`.
- No manual component registration required.
- `Scene` holds two system lists:
    - update systems (fixed/variable gameplay updates)
    - render systems (render-time updates with interpolation alpha)

## Core Components

- `TransformComponent` (`current` + `previous` transform for interpolation)
- `MeshComponent`, `SkinnedMeshComponent`
- `RigidBodyComponent` (non-owning pointer to `PhysicsWorld` body)
- `CameraComponent`
- `BehaviorComponent` (owns polymorphic behavior object)
- `AnimationComponent`

## Built-in Systems

- `PhysicsSystem`
- `RenderSystem`
- `SkinnedRenderSystem`
- `CameraSystem`
- `BehaviorSystem`
- `AnimationSystem`

## Typed Scene Contract

- `SceneProjectModel` is the engine-owned typed scene/project contract for authoring-to-runtime work.
- `RuntimeSceneDefinition` is the engine-owned runtime build input that replaces ad hoc game-shaped conversion as the
  canonical target.
- `SceneRuntimeCoordinator` is the canonical lifecycle owner for load/reload/unload transitions over runtime scene
  definitions.
- `SceneReferenceRuntime` is the canonical in-repo example that applies authoring scenes into a plain `Scene` without
  depending on `game/` glue.
- `SceneDocument` remains the persisted editor format, but runtime-facing code should target the typed scene model
  instead of parsing string bags directly.
- Common built-in data is modeled explicitly:
    - scene metadata and typed spawn settings
    - asset definitions with typed `renderScale`
    - typed node kinds (`Panel`, `Prop`, `Light`, `Camera`)
    - typed camera, light, rigidbody, and script payloads
- Escape hatches remain available through `extraSettings`, `extraProps`, and `extraComponents` so the model can evolve
  without blocking existing content.
- `ValidateSceneProjectModel(...)` is the baseline validation entrypoint for schema version, ID uniqueness, asset
  references, and project scene references.
- `BuildRuntimeSceneDefinition(...)` turns typed authoring data into runtime-ready panels, props, lights, camera, and
  spawn settings without instantiating game behaviors in engine code.
- `SceneRuntimeCoordinator` applies runtime definitions through callback hooks while exposing explicit transition state
  and failure reporting to consumers.
- `SceneReferenceRuntime` wires `SceneDocument -> RuntimeSceneDefinition -> SceneRuntimeCoordinator` together for
  engine-only load, reload, and unload flows.

## Example

```cpp
Monolith::Scene scene;
Monolith::Entity e = scene.CreateEntity({0, 1, 0});

scene.registry.Add<Monolith::CameraComponent>(e, {});
scene.UpdateSystems(dt);
scene.RenderSystems(alpha);
```
