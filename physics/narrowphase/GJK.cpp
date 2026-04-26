#include "physics/narrowphase/GJK.h"

#include <array>
#include <cmath>

#include "math/MathUtils.h"
#include "physics/BoxCollider.h"
#include "physics/RigidBody.h"
#include "physics/SphereCollider.h"

namespace Horo::GJK {
    // Support function: furthest point in direction d for a convex shape
    static Vec3 Support(const RigidBody &body, const Vec3 &d) {
        if (!body.collider)
            return body.position;

        if (body.collider->type == ColliderType::Sphere) {
            auto *s = static_cast<const SphereCollider *>(body.collider.get());
            return body.position + d.Normalized() * s->radius;
        }
        if (body.collider->type == ColliderType::Box) {
            auto *box = static_cast<const BoxCollider *>(body.collider.get());
            Mat3 rot = body.orientation.ToMat3();
            Vec3 local{
                Vec3::Dot(d, rot.GetColumn(0)) > 0
                    ? box->halfExtents.x
                    : -box->halfExtents.x,
                Vec3::Dot(d, rot.GetColumn(1)) > 0
                    ? box->halfExtents.y
                    : -box->halfExtents.y,
                Vec3::Dot(d, rot.GetColumn(2)) > 0
                    ? box->halfExtents.z
                    : -box->halfExtents.z
            };
            return body.position + rot.GetColumn(0) * local.x +
                   rot.GetColumn(1) * local.y + rot.GetColumn(2) * local.z;
        }
        return body.position;
    }

    // Minkowski difference support
    static Vec3 MinkowskiSupport(const RigidBody &a, const RigidBody &b,
                                 const Vec3 &d) {
        return Support(a, d) - Support(b, -d);
    }

    ContactManifold Test(const RigidBody &a, const RigidBody &b) {
        // Stub — full GJK+EPA implementation is Phase 6.
        // Sphere-sphere fast path for milestone 1:
        ContactManifold result;
        if (!a.collider || !b.collider)
            return result;

        if (a.collider->type == ColliderType::Sphere &&
            b.collider->type == ColliderType::Sphere) {
            auto *sa = static_cast<const SphereCollider *>(a.collider.get());
            auto *sb = static_cast<const SphereCollider *>(b.collider.get());
            Vec3 delta = a.position - b.position;
            float dist = delta.Length();
            float sumR = sa->radius + sb->radius;
            if (dist < sumR) {
                float pen = sumR - dist;
                Vec3 normal = NearlyZero(dist) ? Vec3::Up() : delta * (1.0f / dist);
                Vec3 pt = b.position + normal * sb->radius;
                result.AddContact(pt, normal, pen);
            }
        }
        (void) &MinkowskiSupport; // suppress unused warning until full impl
        return result;
    }
} // namespace Horo::GJK
