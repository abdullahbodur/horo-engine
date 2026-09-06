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
        Result<void> ValidateMassPolicy(const PhysicsMotionType motion, const PhysicsMassPolicy &policy) {
            const bool noMass = std::holds_alternative<PhysicsNoMass>(policy);
            if (motion != PhysicsMotionType::Dynamic) {
                if (!noMass)
                    return Result<void>::Failure(
                        MakeError(PhysicsErrors::DescriptorInvalid, "Static and kinematic bodies must use PhysicsNoMass."));
                return Result<void>::Success();
            }
            if (noMass)
                return Result<void>::Failure(
                    MakeError(PhysicsErrors::DescriptorInvalid, "Dynamic bodies require explicit mass or density."));
            if (const auto *mass = std::get_if<PhysicsMass>(&policy))
                return ValidateMassMagnitude(mass->kilograms, MinimumPhysicsMassKilograms, MaximumPhysicsMassKilograms);
            return ValidateMassMagnitude(std::get<PhysicsDensity>(policy).kilogramsPerCubicMeter, MinimumPhysicsDensity,
                                         MaximumPhysicsDensity);
        }

        /** @brief Checks finite velocities and the normative linear-speed magnitude with double intermediates. */
        Result<void> ValidateInitialVelocity(const PhysicsMotionType motion, const Math::Vec3 linearVelocity,
                                             const Math::Vec3 angularVelocity) {
            if (const std::array velocities{linearVelocity, angularVelocity}; !std::ranges::all_of(velocities, [](const Math::Vec3 value) {
                return Math::IsFinite(value);
            }))
                return Result<void>::Failure(MakeError(PhysicsErrors::DescriptorInvalid, "Initial body velocities must be finite."));
            if (const double squaredSpeed = static_cast<double>(linearVelocity.x) * linearVelocity.x +
                                            static_cast<double>(linearVelocity.y) * linearVelocity.y +
                                            static_cast<double>(linearVelocity.z) * linearVelocity.z;
                squaredSpeed > MaximumPhysicsLinearSpeed * MaximumPhysicsLinearSpeed)
                return Result<void>::Failure(MakeError(PhysicsErrors::DescriptorInvalid, "Initial linear speed exceeds 500 m/s."));
            if (motion == PhysicsMotionType::Static && (linearVelocity != Math::Vec3{} || angularVelocity != Math::Vec3{}))
                return Result<void>::Failure(MakeError(PhysicsErrors::DescriptorInvalid, "Static bodies cannot have initial velocity."));
            return Result<void>::Success();
        }

        /** @brief Checks finite observed velocities without applying authored admission limits to solver output. */
        bool FiniteStateVelocity(const PhysicsBodyState &state) noexcept {
            return Math::IsFinite(state.linearVelocity) && Math::IsFinite(state.angularVelocity);
        }
    }  // namespace

    /** @copydoc ValidatePhysicsAuthoredBodyDescriptor */
    Result<void> ValidatePhysicsAuthoredBodyDescriptor(const PhysicsAuthoredBodyDescriptor &descriptor) {
        if (descriptor.motion > PhysicsMotionType::Dynamic)
            return Result<void>::Failure(MakeError(PhysicsErrors::OperationUnsupported, "Unknown authored body motion mode."));
        if (const auto mass = ValidateMassPolicy(descriptor.motion, descriptor.mass); mass.HasError())
            return mass;
        return ValidateInitialVelocity(descriptor.motion, descriptor.initialLinearVelocity, descriptor.initialAngularVelocity);
    }

    /** @copydoc ResolvePhysicsBodyDescriptor */
    Result<PhysicsBodyDescriptor> ResolvePhysicsBodyDescriptor(const PhysicsAuthoredBodyDescriptor &authored, const ShapeHandle shape,
                                                               const PhysicsPose pose, const PhysicsWorldId expectedWorld) {
        if (const auto intent = ValidatePhysicsAuthoredBodyDescriptor(authored); intent.HasError())
            return Result<PhysicsBodyDescriptor>::Failure(intent.ErrorValue());
        PhysicsBodyDescriptor resolved{shape,
                                       pose,
                                       authored.motion,
                                       authored.mass,
                                       authored.initialLinearVelocity,
                                       authored.initialAngularVelocity};
        if (const auto runtime = ValidatePhysicsBodyDescriptor(resolved, expectedWorld); runtime.HasError())
            return Result<PhysicsBodyDescriptor>::Failure(runtime.ErrorValue());
        return Result<PhysicsBodyDescriptor>::Success(std::move(resolved));
    }

    /** @copydoc ValidatePhysicsBodyDescriptor */
    Result<void> ValidatePhysicsBodyDescriptor(const PhysicsBodyDescriptor &descriptor, const PhysicsWorldId expectedWorld) {
        if (const auto owner = ValidatePhysicsHandleOwner(descriptor.shape, expectedWorld); owner.HasError())
            return owner;
        if (const auto pose = ValidatePhysicsPose(descriptor.pose); pose.HasError())
            return pose;
        if (descriptor.motion > PhysicsMotionType::Dynamic)
            return Result<void>::Failure(MakeError(PhysicsErrors::OperationUnsupported, "Unknown body motion mode."));
        if (const auto mass = ValidateMassPolicy(descriptor.motion, descriptor.mass); mass.HasError())
            return mass;
        return ValidateInitialVelocity(descriptor.motion, descriptor.linearVelocity, descriptor.angularVelocity);
    }

    /** @copydoc ValidatePhysicsBodyState */
    Result<void> ValidatePhysicsBodyState(const PhysicsBodyState &state, const PhysicsWorldId expectedWorld) {
        if (const auto owner = ValidatePhysicsHandleOwner(state.body, expectedWorld); owner.HasError())
            return owner;
        if (const auto pose = ValidatePhysicsPose(state.pose); pose.HasError())
            return pose;
        if (state.activity > PhysicsBodyActivity::Sleeping)
            return Result<void>::Failure(MakeError(PhysicsErrors::OperationUnsupported, "Unknown body activity state."));
        if (!FiniteStateVelocity(state))
            return Result<void>::Failure(MakeError(PhysicsErrors::DescriptorInvalid, "Observed body velocities must be finite."));
        return Result<void>::Success();
    }
}  // namespace Horo::Physics
