#include "physics/constraints/ContactConstraint.h"

#include "math/MathUtils.h"
#include "physics/RigidBody.h"

namespace Monolith {
    namespace {
        void ApplyFrictionImpulse(RigidBody *bodyA, RigidBody *bodyB, const Vec3 &rA,
                                  const Vec3 &rB, const Vec3 &relV, const Vec3 &n,
                                  const float normalImpulse) {
            Vec3 vt = relV - n * Vec3::Dot(relV, n);
            float vtLen = vt.Length();
            if (NearlyZero(vtLen))
                return;

            Vec3 t = vt * (1.0f / vtLen);
            Vec3 rAxT = Vec3::Cross(rA, t);
            Vec3 rBxT = Vec3::Cross(rB, t);
            float kt = bodyA->invMass + bodyB->invMass +
                       Vec3::Dot(bodyA->inertiaTensorInv * rAxT, rAxT) +
                       Vec3::Dot(bodyB->inertiaTensorInv * rBxT, rBxT);
            if (NearlyZero(kt))
                return;

            float mu = (bodyA->friction + bodyB->friction) * 0.5f;
            float jt = -Vec3::Dot(relV, t) / kt;
            jt = Clamp(jt, -mu * normalImpulse, mu * normalImpulse);

            Vec3 impulseT = t * jt;
            if (!bodyA->IsStatic()) {
                bodyA->velocity += impulseT * bodyA->invMass;
                bodyA->angularVelocity +=
                        bodyA->inertiaTensorInv * Vec3::Cross(rA, impulseT);
            }
            if (!bodyB->IsStatic()) {
                bodyB->velocity -= impulseT * bodyB->invMass;
                bodyB->angularVelocity -=
                        bodyB->inertiaTensorInv * Vec3::Cross(rB, impulseT);
            }
        }
    } // namespace

    void ContactConstraint::Solve() {
        if (!bodyA || !bodyB || !manifold.hasContact())
            return;

        float e = Min(bodyA->restitution, bodyB->restitution);

        Vec3 totalPosCorr = Vec3::Zero();
        float totalCorrMass = bodyA->invMass + bodyB->invMass;
        int contactsUsed = 0;

        for (int ci = 0; ci < manifold.count; ++ci) {
            const ContactPoint &cp = manifold.contacts[ci];
            const Vec3 &n = cp.normal;

            Vec3 rA = cp.point - bodyA->position;
            Vec3 rB = cp.point - bodyB->position;
            Vec3 vA = bodyA->velocity + Vec3::Cross(bodyA->angularVelocity, rA);
            Vec3 vB = bodyB->velocity + Vec3::Cross(bodyB->angularVelocity, rB);
            Vec3 relV = vA - vB;
            float vn = Vec3::Dot(relV, n);

            // Only resolve if objects are approaching
            if (vn > 0)
                continue;

            // Effective mass along normal
            Vec3 rAxN = Vec3::Cross(rA, n);
            Vec3 rBxN = Vec3::Cross(rB, n);
            float kn = bodyA->invMass + bodyB->invMass +
                       Vec3::Dot(bodyA->inertiaTensorInv * rAxN, rAxN) +
                       Vec3::Dot(bodyB->inertiaTensorInv * rBxN, rBxN);
            if (NearlyZero(kn))
                continue;

            float jn = -(1.0f + e) * vn / kn;
            jn = Max(jn, 0.0f);

            Vec3 impulseN = n * jn;
            if (!bodyA->IsStatic()) {
                bodyA->velocity += impulseN * bodyA->invMass;
                bodyA->angularVelocity +=
                        bodyA->inertiaTensorInv * Vec3::Cross(rA, impulseN);
            }
            if (!bodyB->IsStatic()) {
                bodyB->velocity -= impulseN * bodyB->invMass;
                bodyB->angularVelocity -=
                        bodyB->inertiaTensorInv * Vec3::Cross(rB, impulseN);
            }

            // Accumulate Baumgarte position correction
            const float SLOP = 0.005f;
            const float FACTOR = 0.2f;
            float pen = Max(cp.penetration - SLOP, 0.0f) * FACTOR;
            totalPosCorr += n * pen;
            ++contactsUsed;

            ApplyFrictionImpulse(bodyA, bodyB, rA, rB, relV, n, jn);
        }

        // Apply averaged position correction
        if (contactsUsed > 0 && !NearlyZero(totalCorrMass)) {
            const auto correctionScale =
                    1.0f / (static_cast<float>(contactsUsed) * totalCorrMass);
            auto corrVec = totalPosCorr * correctionScale;
            if (!bodyA->IsStatic())
                bodyA->position += corrVec * bodyA->invMass;
            if (!bodyB->IsStatic())
                bodyB->position -= corrVec * bodyB->invMass;
        }
    }
} // namespace Monolith
