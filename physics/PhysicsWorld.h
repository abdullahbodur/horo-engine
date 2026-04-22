#pragma once
#include <memory>
#include <vector>

#include "math/Vec3.h"
#include "physics/constraints/ConstraintSolver.h"

namespace Monolith {
    class RigidBody;

    class PhysicsWorld {
    public:
        PhysicsWorld() = default;

        ~PhysicsWorld() = default;

        void SetGravity(const Vec3 &g) { m_gravity = g; }
        Vec3 GetGravity() const { return m_gravity; }

        // Add a body and return a non-owning pointer
        RigidBody *AddBody(RigidBody body);

        void RemoveBody(const RigidBody *body);

        void Clear();

        // Advance simulation by dt seconds
        void Step(float dt) const;

        const std::vector<std::unique_ptr<RigidBody> > &GetBodies() const {
            return m_bodies;
        }

    private:
        Vec3 m_gravity = {0.0f, -9.81f, 0.0f};
        std::vector<std::unique_ptr<RigidBody> > m_bodies;
        [[no_unique_address]] ConstraintSolver m_solver;

        void ApplyForces() const;

        void DetectCollisions(std::vector<ContactConstraint> &contacts) const;

        void SolveSpherePlane(RigidBody &sphere, float planeY,
                              std::vector<ContactConstraint> &contacts) const;

        void SolveBoxPlane(RigidBody &box, float planeY,
                           std::vector<ContactConstraint> &contacts) const;
    };
} // namespace Monolith
