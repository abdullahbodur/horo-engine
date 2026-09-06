#include "Horo/Physics/CharacterControllerContracts.h"

#include <cmath>
#include <type_traits>

namespace Horo::Character {
    namespace {
        constexpr auto KnownCollisionFlags = CharacterCollisionFlags::Sides | CharacterCollisionFlags::Ground |
                                             CharacterCollisionFlags::Ceiling | CharacterCollisionFlags::Step;

        /** @brief Checks a finite unit vector against the public Character tolerance. */
        [[nodiscard]] bool IsUnit(const Math::Vec3 value) noexcept {
            return Math::IsFinite(value) &&
                   std::abs(static_cast<double>(Math::Dot(value, value)) - 1.0) <= CharacterUnitSquaredNormTolerance;
        }

        /** @brief Checks a finite unit quaternion without normalizing caller evidence. */
        [[nodiscard]] bool IsUnit(const Math::Quaternion value) noexcept {
            const double squaredNorm = static_cast<double>(value.x) * value.x + static_cast<double>(value.y) * value.y +
                                       static_cast<double>(value.z) * value.z + static_cast<double>(value.w) * value.w;
            return Math::IsFinite(value) && std::abs(squaredNorm - 1.0) <= CharacterUnitSquaredNormTolerance;
        }

        /** @brief Checks copied material identity and generation evidence. */
        [[nodiscard]] bool IsMaterialValid(const Physics::PhysicsQueryMaterial &material) noexcept {
            return material.asset.IsValid() && material.assetGeneration != 0 && material.slot.IsValid();
        }

        /** @brief Validates one active contact against the descriptor's Physics world. */
        [[nodiscard]] Result<void> ValidateContact(const CharacterSurfaceContact &contact, const Physics::PhysicsWorldId expectedWorld) {
            if (!contact.shape.IsValid() || contact.shape.world != expectedWorld)
                return Result<void>::Failure(
                    MakeError(CharacterErrors::DescriptorInvalid, "Contact shape does not belong to the descriptor world."));
            if (contact.body.has_value()) {
                const auto owner = Physics::ValidatePhysicsHandleOwner(*contact.body, expectedWorld);
                if (owner.HasError())
                    return Result<void>::Failure(
                        MakeError(CharacterErrors::DescriptorInvalid, "Contact body does not belong to the descriptor world."));
            }
            if (!Math::IsFinite(contact.point) || !IsUnit(contact.normal) || !IsMaterialValid(contact.material) ||
                !std::isfinite(contact.penetrationDepthMeters))
                return Result<void>::Failure(
                    MakeError(CharacterErrors::DescriptorInvalid, "Contact evidence contains invalid numeric or material data."));
            return Result<void>::Success();
        }
    }  // namespace

    /** @copydoc CharacterWorldId::Create */
    Result<CharacterWorldId> CharacterWorldId::Create(const std::uint64_t value) {
        if (value == 0)
            return Result<CharacterWorldId>::Failure(MakeError(CharacterErrors::WorldInvalid));
        return Result<CharacterWorldId>::Success(CharacterWorldId{value});
    }

    /** @copydoc ValidateCharacterControllerHandleOwner */
    Result<void> ValidateCharacterControllerHandleOwner(const CharacterControllerHandle &handle,
                                                        const std::uint64_t expectedSceneGeneration, const CharacterWorldId expectedWorld) {
        if (expectedSceneGeneration == 0 || !expectedWorld.IsValid())
            return Result<void>::Failure(MakeError(CharacterErrors::WorldInvalid));
        if (!handle.IsValid())
            return Result<void>::Failure(MakeError(CharacterErrors::HandleMalformed));
        if (handle.sceneGeneration != expectedSceneGeneration || handle.world != expectedWorld)
            return Result<void>::Failure(MakeError(CharacterErrors::HandleWorldMismatch));
        return Result<void>::Success();
    }

    /** @copydoc ValidateCharacterControllerDescriptor */
    Result<void> ValidateCharacterControllerDescriptor(const CharacterControllerDescriptor &descriptor) {
        if (descriptor.sceneGeneration == 0 || !descriptor.characterWorld.IsValid() || !descriptor.physicsWorld.IsValid())
            return Result<void>::Failure(MakeError(CharacterErrors::WorldInvalid));
        const auto capsule = Physics::ValidatePhysicsShapeDescriptor(Physics::PhysicsShapeDescriptor{descriptor.capsule});
        if (capsule.HasError())
            return Result<void>::Failure(MakeError(CharacterErrors::DescriptorInvalid, "Controller capsule dimensions are invalid."));
        if (!Math::IsFinite(descriptor.collisionRootPosition) || !IsUnit(descriptor.up) || !Math::IsFinite(descriptor.gravity) ||
            !descriptor.collisionProfile.IsValid() || !descriptor.queryChannel.IsValid() || !IsMaterialValid(descriptor.defaultMaterial))
            return Result<void>::Failure(MakeError(CharacterErrors::DescriptorInvalid));
        if (!std::isfinite(descriptor.skinWidthMeters) || descriptor.skinWidthMeters <= 0 ||
            !std::isfinite(descriptor.minimumMoveDistanceMeters) || descriptor.minimumMoveDistanceMeters < 0 ||
            !std::isfinite(descriptor.maximumStepHeightMeters) || descriptor.maximumStepHeightMeters < 0 ||
            !std::isfinite(descriptor.maximumSlopeDegrees) || descriptor.maximumSlopeDegrees < 0 || descriptor.maximumSlopeDegrees > 90)
            return Result<void>::Failure(MakeError(CharacterErrors::DescriptorInvalid));
        if (descriptor.maximumContacts == 0 || descriptor.maximumContacts > MaximumCharacterContacts)
            return Result<void>::Failure(MakeError(CharacterErrors::CapacityExceeded));
        return Result<void>::Success();
    }

    /** @copydoc ValidateCharacterMovementRequest */
    Result<void> ValidateCharacterMovementRequest(const CharacterMovementRequest &request, const std::uint64_t expectedSceneGeneration,
                                                  const CharacterWorldId expectedWorld) {
        auto owner = ValidateCharacterControllerHandleOwner(request.controller, expectedSceneGeneration, expectedWorld);
        if (owner.HasError())
            return owner;
        if (request.tick == 0 || request.sequence == 0 ||
            (request.desiredVelocityMetersPerSecond.has_value() && !Math::IsFinite(*request.desiredVelocityMetersPerSecond)) ||
            (request.desiredHeading.has_value() && !IsUnit(*request.desiredHeading)))
            return Result<void>::Failure(MakeError(CharacterErrors::RequestInvalid));
        switch (request.stance) {
            using enum CharacterStanceIntent;
            case Keep:
            case Stand:
            case Crouch:
                return Result<void>::Success();
        }
        return Result<void>::Failure(MakeError(CharacterErrors::OperationUnsupported));
    }

    /** @copydoc ValidateCharacterMovementResult */
    Result<void> ValidateCharacterMovementResult(const CharacterMovementResult &result, const CharacterControllerDescriptor &descriptor) {
        const auto descriptorValidation = ValidateCharacterControllerDescriptor(descriptor);
        if (descriptorValidation.HasError())
            return descriptorValidation;
        auto owner = ValidateCharacterControllerHandleOwner(result.controller, descriptor.sceneGeneration, descriptor.characterWorld);
        if (owner.HasError())
            return owner;
        if (result.tick == 0 || result.sequence == 0 || !Math::IsFinite(result.finalPosition) || !IsUnit(result.finalHeading) ||
            !Math::IsFinite(result.achievedVelocityMetersPerSecond) || !IsUnit(result.up) || !std::isfinite(result.groundSlopeDegrees) ||
            result.groundSlopeDegrees < 0 || result.groundSlopeDegrees > 180)
            return Result<void>::Failure(MakeError(CharacterErrors::DescriptorInvalid, "Movement result metadata is invalid."));
        using FlagValue = std::underlying_type_t<CharacterCollisionFlags>;
        if ((static_cast<FlagValue>(result.collisions) & ~static_cast<FlagValue>(KnownCollisionFlags)) != 0)
            return Result<void>::Failure(MakeError(CharacterErrors::OperationUnsupported));
        if (result.contactCount > descriptor.maximumContacts || result.contactCount > result.contacts.size())
            return Result<void>::Failure(MakeError(CharacterErrors::CapacityExceeded));
        if (result.truncated && result.contactCount != descriptor.maximumContacts)
            return Result<void>::Failure(
                MakeError(CharacterErrors::DescriptorInvalid, "Truncation requires a full admitted contact prefix."));
        if (result.grounded) {
            if (!IsUnit(result.groundNormal) || !result.groundMaterial.has_value() || !IsMaterialValid(*result.groundMaterial))
                return Result<void>::Failure(
                    MakeError(CharacterErrors::DescriptorInvalid, "Grounded result lacks valid surface evidence."));
        } else if (result.groundMaterial.has_value()) {
            return Result<void>::Failure(
                MakeError(CharacterErrors::DescriptorInvalid, "Airborne result cannot claim ground material evidence."));
        }
        for (std::uint32_t index = 0; index < result.contactCount; ++index) {
            const auto contact = ValidateContact(result.contacts[index], descriptor.physicsWorld);
            if (contact.HasError())
                return contact;
        }
        return Result<void>::Success();
    }
}  // namespace Horo::Character
