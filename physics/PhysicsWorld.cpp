#include "physics/PhysicsWorld.h"

#include <algorithm>

#include "math/MathUtils.h"
#include "physics/BoxCollider.h"
#include "physics/RigidBody.h"
#include "physics/SphereCollider.h"
#include "physics/broadphase/BruteForce.h"
#include "physics/integration/SemiImplicitEuler.h"
#include "physics/narrowphase/GJK.h"
#include "physics/narrowphase/SAT.h"

namespace Horo {
    RigidBody *PhysicsWorld::AddBody(RigidBody body) {
        m_bodies.push_back(std::make_unique<RigidBody>(std::move(body)));
        return m_bodies.back().get();
    }

    void PhysicsWorld::RemoveBody(const RigidBody *body) {
        const auto removed = std::ranges::remove_if(
            m_bodies, [body](const auto &p) { return p.get() == body; });
        m_bodies.erase(removed.begin(), removed.end());
    }

    void PhysicsWorld::Clear() { m_bodies.clear(); }

    void PhysicsWorld::Step(float dt) const {
        ApplyForces();

        std::vector<ContactConstraint> contacts;
        DetectCollisions(contacts);

        m_solver.Solve(contacts);

        for (const auto &body: m_bodies) {
            SemiImplicitEuler::Integrate(*body, dt);
            body->ClearForces();
        }
    }

    void PhysicsWorld::ApplyForces() const {
        for (const auto &body: m_bodies) {
            if (!body->IsStatic()) {
                body->AddForce(m_gravity * body->mass);
            }
        }
    }

    void PhysicsWorld::DetectCollisions(
        std::vector<ContactConstraint> &contacts) const {
        static const float FLOOR_Y = 0.0f;

        // Analytic floor tests for dynamic bodies
        for (const auto &body: m_bodies) {
            if (body->IsStatic()) {
                continue;
            }
            if (!body->collider) {
                continue;
            }

            if (body->collider->type == ColliderType::Sphere) {
                SolveSpherePlane(*body, FLOOR_Y, contacts);
            } else if (body->collider->type == ColliderType::Box) {
                SolveBoxPlane(*body, FLOOR_Y, contacts);
            }
        }

        // General broadphase + narrowphase
        std::vector<RigidBody *> ptrs;
        ptrs.reserve(m_bodies.size());
        for (const auto &b: m_bodies) {
            ptrs.push_back(b.get());
        }

        auto pairs = BruteForce::FindOverlappingPairs(ptrs);
        for (const auto &[i, j]: pairs) {
            RigidBody *a = ptrs[i];
            RigidBody *b = ptrs[j];

            // Skip static-static pairs — no impulse response possible
            if (a->IsStatic() && b->IsStatic()) {
                continue;
            }
            if (!a->collider || !b->collider) {
                continue;
            }

            ContactManifold manifold;

            if (a->collider->type == ColliderType::Box &&
                b->collider->type == ColliderType::Box) {
                manifold = SAT::TestBoxBox(*a, *b);
            } else {
                manifold = GJK::Test(*a, *b);
            }

            if (manifold.hasContact()) {
                ContactConstraint c;
                c.bodyA = a;
                c.bodyB = b;
                c.manifold = manifold;
                contacts.push_back(c);
            }
        }
    }

    void PhysicsWorld::SolveSpherePlane(
        RigidBody &sphere, float planeY,
        std::vector<ContactConstraint> & /*contacts*/) const {
        const auto *sc = static_cast<const SphereCollider *>(sphere.collider.get());
        float bottom = sphere.position.y - sc->radius;

        if (bottom < planeY) {
            float pen = planeY - bottom;

            if (const float vn = Vec3::Dot(sphere.velocity, Vec3::Up()); vn < 0) {
                float e = sphere.restitution;
                float jn = -(1.0f + e) * vn * sphere.mass;
                sphere.velocity += Vec3::Up() * (jn * sphere.invMass);
            }
            sphere.position.y += pen;
        }
    }

    void PhysicsWorld::SolveBoxPlane(
        RigidBody &box, float planeY,
        std::vector<ContactConstraint> & /*contacts*/) const {
        const auto *bc = static_cast<const BoxCollider *>(box.collider.get());
        float bottom = box.position.y - bc->halfExtents.y;

        if (bottom < planeY) {
            float pen = planeY - bottom;

            if (const float vn = Vec3::Dot(box.velocity, Vec3::Up()); vn < 0) {
                float e = box.restitution;
                float jn = -(1.0f + e) * vn * box.mass;
                box.velocity += Vec3::Up() * (jn * box.invMass);
            }
            box.position.y += pen;
            // Clamp downward velocity so the box doesn't sink through the floor
            if (box.velocity.y < 0.0f)
                box.velocity.y = 0.0f;
        }
    }
} // namespace Horo
