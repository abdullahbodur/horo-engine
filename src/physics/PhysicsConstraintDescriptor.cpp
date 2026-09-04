#include "Horo/Physics/PhysicsConstraintDescriptor.h"

#include <cmath>

namespace Horo::Physics {
    namespace {
        /** @brief Validates a body-local anchor without resolving or retaining the body. */
        Result<void> ValidateBodyAnchor(const PhysicsBodyAnchor &anchor, const PhysicsWorldId world) {
            auto owner = ValidatePhysicsHandleOwner(anchor.body, world);
            if (owner.HasError())
                return owner;
            return ValidatePhysicsPose(anchor.localFrame);
        }

        /** @brief Validates the explicit second endpoint and rejects self-constraints. */
        Result<void> ValidateSecondAnchor(const PhysicsConstraintDescriptor &descriptor, const PhysicsWorldId world) {
            const auto *body = std::get_if<PhysicsBodyAnchor>(&descriptor.second);
            if (body == nullptr)
                return ValidatePhysicsPose(std::get<PhysicsWorldAnchor>(descriptor.second).frame);
            auto valid = ValidateBodyAnchor(*body, world);
            if (valid.HasError())
                return valid;
            if (body->body == descriptor.first.body)
                return Result<void>::Failure(
                    MakeError(PhysicsErrors::DescriptorInvalid, "Constraint endpoints must name distinct bodies."));
            return Result<void>::Success();
        }

        /** @brief Checks finite non-negative ordered distance limits without clamping. */
        Result<void> ValidateDistance(const PhysicsDistanceConstraint &distance) {
            if (!std::isfinite(distance.minimumMeters) || !std::isfinite(distance.maximumMeters) || distance.minimumMeters < 0 ||
                distance.maximumMeters < distance.minimumMeters)
                return Result<void>::Failure(
                    MakeError(PhysicsErrors::DescriptorInvalid, "Constraint distances must be finite with 0 <= minimum <= maximum."));
            return Result<void>::Success();
        }
    }  // namespace

    /** @copydoc ValidatePhysicsConstraintDescriptor */
    Result<void> ValidatePhysicsConstraintDescriptor(const PhysicsConstraintDescriptor &descriptor, const PhysicsWorldId expectedWorld) {
        auto first = ValidateBodyAnchor(descriptor.first, expectedWorld);
        if (first.HasError())
            return first;
        auto second = ValidateSecondAnchor(descriptor, expectedWorld);
        if (second.HasError())
            return second;
        if (const auto *distance = std::get_if<PhysicsDistanceConstraint>(&descriptor.parameters))
            return ValidateDistance(*distance);
        return Result<void>::Success();
    }
}  // namespace Horo::Physics
