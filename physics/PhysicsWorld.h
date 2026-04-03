#pragma once
#include <memory>
#include <vector>

#include "math/Vec3.h"
#include "physics/constraints/ConstraintSolver.h"

namespace Horo {

class RigidBody;

class PhysicsWorld {
 public:
  Vec3 gravity = {0.0f, -9.81f, 0.0f};

  PhysicsWorld() = default;
  ~PhysicsWorld() = default;

  // Add a body and return a non-owning pointer
  RigidBody* AddBody(RigidBody body);
  void RemoveBody(RigidBody* body);
  void Clear();

  // Advance simulation by dt seconds
  void Step(float dt);

  const std::vector<std::unique_ptr<RigidBody>>& GetBodies() const { return m_bodies; }

 private:
  std::vector<std::unique_ptr<RigidBody>> m_bodies;
  ConstraintSolver m_solver;

  void ApplyForces();
  void DetectCollisions(std::vector<ContactConstraint>& contacts);
  void SolveSpherePlane(RigidBody& sphere, float planeY, std::vector<ContactConstraint>& contacts);
  void SolveBoxPlane(RigidBody& box, float planeY, std::vector<ContactConstraint>& contacts);
};

}  // namespace Horo
