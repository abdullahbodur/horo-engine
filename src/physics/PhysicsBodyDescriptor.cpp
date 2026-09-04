#include "Horo/Physics/PhysicsBodyDescriptor.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace Horo::Physics {
    namespace {
        /** @brief Validates mass or density magnitude without silently clamping or deriving inertia. */
        Result<void> ValidateMassMagnitude(const float value, const float minimum, const float maximum) {
            if (!std::isfinite(value) || value < minimum || value > maximum)
                return Result<void>::Failure(
                    MakeError(PhysicsErrors::DescriptorInvalid, "Dynamic mass or density is outside CanonicalV1 bounds."));
            return Result<void>::Success();
        }

        /** @brief Requires an explicit dynamic policy, and absence of dynamic mass for other modes. */
        Result<void> ValidateMassPolicy(const PhysicsBodyDescriptor &descriptor) {
            const bool noMass = std::holds_alternative<PhysicsNoMass>(descriptor.mass);
            if (descriptor.motion != PhysicsMotionType::Dynamic) {
                if (!noMass)
                    return Result<void>::Failure(
                        MakeError(PhysicsErrors::DescriptorInvalid, "Static and kinematic bodies must use PhysicsNoMass."));
                return Result<void>::Success();
            }
            if (noMass)
                return Result<void>::Failure(
                    MakeError(PhysicsErrors::DescriptorInvalid, "Dynamic bodies require explicit mass or density."));
            if (const auto *mass = std::get_if<PhysicsMass>(&descriptor.mass))
                return ValidateMassMagnitude(mass->kilograms, MinimumPhysicsMassKilograms, MaximumPhysicsMassKilograms);
            return ValidateMassMagnitude(std::get<PhysicsDensity>(descriptor.mass).kilogramsPerCubicMeter, MinimumPhysicsDensity,
                                         MaximumPhysicsDensity);
        }

        /** @brief Checks finite velocities and the normative linear-speed magnitude with double intermediates. */
        Result<void> ValidateVelocity(const PhysicsBodyDescriptor &descriptor) {
            if (const std::array velocities{descriptor.linearVelocity, descriptor.angularVelocity};
                !std::ranges::all_of(velocities, [](const Math::Vec3 value) {
                return Math::IsFinite(value);
            }))
                return Result<void>::Failure(MakeError(PhysicsErrors::DescriptorInvalid, "Initial body velocities must be finite."));
            if (const double squaredSpeed = static_cast<double>(descriptor.linearVelocity.x) * descriptor.linearVelocity.x +
                                            static_cast<double>(descriptor.linearVelocity.y) * descriptor.linearVelocity.y +
                                            static_cast<double>(descriptor.linearVelocity.z) * descriptor.linearVelocity.z;
                squaredSpeed > MaximumPhysicsLinearSpeed * MaximumPhysicsLinearSpeed)
                return Result<void>::Failure(MakeError(PhysicsErrors::DescriptorInvalid, "Initial linear speed exceeds 500 m/s."));
            if (descriptor.motion == PhysicsMotionType::Static &&
                (descriptor.linearVelocity != Math::Vec3{} || descriptor.angularVelocity != Math::Vec3{}))
                return Result<void>::Failure(MakeError(PhysicsErrors::DescriptorInvalid, "Static bodies cannot have initial velocity."));
            return Result<void>::Success();
        }
    }  // namespace

    /** @copydoc ValidatePhysicsBodyDescriptor */
    Result<void> ValidatePhysicsBodyDescriptor(const PhysicsBodyDescriptor &descriptor, const PhysicsWorldId expectedWorld) {
        if (const auto owner = ValidatePhysicsHandleOwner(descriptor.shape, expectedWorld); owner.HasError())
            return owner;
        if (const auto pose = ValidatePhysicsPose(descriptor.pose); pose.HasError())
            return pose;
        if (descriptor.motion > PhysicsMotionType::Dynamic)
            return Result<void>::Failure(MakeError(PhysicsErrors::OperationUnsupported, "Unknown body motion mode."));
        if (const auto mass = ValidateMassPolicy(descriptor); mass.HasError())
            return mass;
        return ValidateVelocity(descriptor);
    }
}  // namespace Horo::Physics
