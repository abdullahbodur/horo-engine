#include "Horo/Physics/PhysicsPose.h"

#include "Horo/Physics/PhysicsErrors.h"

#include <cmath>

namespace Horo::Physics {
    /** @copydoc ValidatePhysicsPose */
    Result<void> ValidatePhysicsPose(const PhysicsPose &pose) {
        if (!Math::IsFinite(pose.translation) || !Math::IsFinite(pose.rotation))
            return Result<void>::Failure(
                MakeError(PhysicsErrors::DescriptorInvalid, "Physics pose coordinates and rotation must be finite."));
        const double x = pose.rotation.x;
        const double y = pose.rotation.y;
        const double z = pose.rotation.z;
        const double w = pose.rotation.w;
        const double squaredNorm = x * x + y * y + z * z + w * w;
        if (std::abs(squaredNorm - 1.0) > PhysicsRotationSquaredNormTolerance)
            return Result<void>::Failure(
                MakeError(PhysicsErrors::DescriptorInvalid, "Physics rotation must have unit squared norm within 1e-6."));
        return Result<void>::Success();
    }
}  // namespace Horo::Physics
