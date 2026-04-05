# Scene Module

`scene/` provides the ECS layer and engine systems that connect gameplay data to renderer/physics.

## Responsibilities

- Entity lifecycle and component storage (`Registry`, `ComponentPool<T>`)
- Scene-level orchestration (`Scene`)
- System interface and update pipelines (`System`)
- Built-in components (`scene/components/`)
- Built-in systems (`scene/systems/`)

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

## Example

```cpp
Monolith::Scene scene;
Monolith::Entity e = scene.CreateEntity({0, 1, 0});

scene.registry.Add<Monolith::CameraComponent>(e, {});
scene.UpdateSystems(dt);
scene.RenderSystems(alpha);
```
