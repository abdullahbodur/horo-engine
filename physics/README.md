# Physics Module

`physics/` implements rigid-body simulation and collision solving for gameplay objects.

## Responsibilities

- Rigid body state and mass/inertia handling (`RigidBody`)
- Collider abstractions (`Collider`, `SphereCollider`, `BoxCollider`)
- Simulation world orchestration (`PhysicsWorld`)
- Broadphase and narrowphase collision detection
  - broadphase: brute-force pairing
  - narrowphase: GJK + SAT
- Contact generation and iterative impulse solving
  - `ContactConstraint`, `ConstraintSolver`
- Time integration (`SemiImplicitEuler`)

## Main Types

- `PhysicsWorld`
  - Owns bodies via `std::unique_ptr` and advances simulation with `Step(dt)`
  - Applies gravity and resolves contacts each frame
- `RigidBody`
  - Position/orientation + linear/angular velocities
  - Material parameters: restitution, friction, damping
  - Factory helpers: `MakeStatic`, `MakeSphere`, `MakeBox`

## Usage Notes

- Bodies are owned by `PhysicsWorld`; components should store non-owning pointers.
- Keep simulation in fixed timestep (`OnFixedUpdate`) for determinism/stability.
- Static objects should use `invMass = 0` (via `MakeStatic`).

## Example

```cpp
Monolith::PhysicsWorld world;
world.gravity = {0.0f, -9.81f, 0.0f};

Monolith::RigidBody sphere = Monolith::RigidBody::MakeSphere(0.5f, 1.0f);
Monolith::RigidBody* body = world.AddBody(std::move(sphere));
body->position = {0.0f, 5.0f, 0.0f};

world.Step(1.0f / 120.0f);
```
