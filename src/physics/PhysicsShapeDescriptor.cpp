#include "Horo/Physics/PhysicsShapeDescriptor.h"

#include "Horo/Physics/PhysicsErrors.h"

#include <cmath>

namespace Horo::Physics {
    namespace {
        /** @brief Checks one positive finite dimension without imposing a motion-specific size profile. */
        bool PositiveFinite(const float value) {
            return std::isfinite(value) && value > 0;
        }

        /** @brief Validates each independently named box half dimension. */
        Result<void> ValidateShape(const PhysicsBoxShape &box) {
            if (!PositiveFinite(box.halfExtentsMeters.x) || !PositiveFinite(box.halfExtentsMeters.y) ||
                !PositiveFinite(box.halfExtentsMeters.z))
                return Result<void>::Failure(MakeError(PhysicsErrors::DescriptorInvalid, "Box half extents must be positive and finite."));
            return Result<void>::Success();
        }

        /** @brief Validates the sphere radius. */
        Result<void> ValidateShape(const PhysicsSphereShape &sphere) {
            if (!PositiveFinite(sphere.radiusMeters))
                return Result<void>::Failure(MakeError(PhysicsErrors::DescriptorInvalid, "Sphere radius must be positive and finite."));
            return Result<void>::Success();
        }

        /** @brief Validates radius and the non-degenerate cylindrical portion independently. */
        Result<void> ValidateShape(const PhysicsCapsuleShape &capsule) {
            if (!PositiveFinite(capsule.radiusMeters) || !PositiveFinite(capsule.cylindricalHalfHeightMeters))
                return Result<void>::Failure(
                    MakeError(PhysicsErrors::DescriptorInvalid, "Capsule radius and cylindrical half-height must be positive and finite."));
            return Result<void>::Success();
        }

        /** @brief Checks the plane equation without modifying its orientation or signed distance. */
        Result<void> ValidateShape(const PhysicsStaticPlaneShape &plane) {
            if (!Math::IsFinite(plane.normal) || !std::isfinite(plane.signedDistanceMeters))
                return Result<void>::Failure(
                    MakeError(PhysicsErrors::DescriptorInvalid, "Plane normal and signed distance must be finite."));
            const double x = plane.normal.x;
            const double y = plane.normal.y;
            const double z = plane.normal.z;
            if (std::abs(x * x + y * y + z * z - 1.0) > PhysicsPlaneNormalSquaredNormTolerance)
                return Result<void>::Failure(
                    MakeError(PhysicsErrors::DescriptorInvalid, "Plane normal must have unit squared norm within 1e-6."));
            return Result<void>::Success();
        }
    }  // namespace

    /** @copydoc ValidatePhysicsShapeDescriptor */
    Result<void> ValidatePhysicsShapeDescriptor(const PhysicsShapeDescriptor &descriptor) {
        return std::visit([](const auto &shape) {
            return ValidateShape(shape);
        }, descriptor);
    }
}  // namespace Horo::Physics
