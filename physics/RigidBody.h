#pragma once
#include <memory>

#include "math/Mat3.h"
#include "math/Quaternion.h"
#include "math/Vec3.h"
#include "physics/Collider.h"

namespace Monolith {
    class RigidBody {
    public:
        // Position and orientation
        Vec3 position = Vec3::Zero();
        Quaternion orientation = Quaternion::Identity();

        // Linear motion
        Vec3 velocity = Vec3::Zero();
        Vec3 forceAccum = Vec3::Zero();
        float mass = 1.0f;
        float invMass = 1.0f; // 0 = static (infinite mass)

        // Angular motion
        Vec3 angularVelocity = Vec3::Zero();
        Vec3 torqueAccum = Vec3::Zero();
        Mat3 inertiaTensorInv = Mat3::Identity(); // in world space

        // Material
        float restitution = 0.6f;
        float friction = 0.5f;
        float linearDamping = 0.01f;
        float angularDamping = 0.05f;

        // Collider
        std::shared_ptr<Collider> collider;

        // Create a static body (invMass = 0)
        static RigidBody MakeStatic();

        // Create a sphere body and compute inertia tensor
        static RigidBody MakeSphere(float radius, float mass);

        // Create a box body and compute inertia tensor
        static RigidBody MakeBox(const Vec3 &halfExtents, float mass);

        void AddForce(const Vec3 &f) { forceAccum += f; }
        void AddTorque(const Vec3 &t) { torqueAccum += t; }

        void AddForceAtPoint(const Vec3 &f, const Vec3 &worldPoint);

        void ClearForces() {
            forceAccum = Vec3::Zero();
            torqueAccum = Vec3::Zero();
        }

        bool IsStatic() const { return invMass == 0.0f; }

        void SetMass(float m);

        void SetSphereInertia(float radius);

        void SetBoxInertia(const Vec3 &halfExtents);

        void UpdateWorldInertia() const;
    };
} // namespace Monolith
