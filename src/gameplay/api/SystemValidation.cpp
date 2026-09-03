#include "GameplayIdentityValidation.h"
#include "Horo/Gameplay/GameplayErrors.h"
#include "SystemRegistryDetail.h"

#include <algorithm>

namespace Horo::Gameplay::Detail {
    namespace {
        [[nodiscard]] bool HasValidBounds(const GameplaySystemDescriptor &descriptor) noexcept {
            return descriptor.access.reads.size() <= MaximumGameplayComponentAccesses &&
                   descriptor.access.writes.size() <= MaximumGameplayComponentAccesses &&
                   descriptor.after.size() <= MaximumGameplayDependencies && descriptor.before.size() <= MaximumGameplayDependencies &&
                   descriptor.requiredServices.size() <= MaximumGameplayDependencies &&
                   descriptor.requiredCapabilities.size() <= MaximumGameplayCapabilities;
        }

        [[nodiscard]] bool HasInvalidPhaseWrites(const GameplaySystemDescriptor &descriptor) noexcept {
            using enum GameplaySystemPhase;
            return !descriptor.access.writes.empty() && (descriptor.phase == Presentation || descriptor.phase == RenderExtraction);
        }

        [[nodiscard]] bool IsValidPhase(const GameplaySystemPhase phase) noexcept {
            return static_cast<std::uint8_t>(phase) <= static_cast<std::uint8_t>(GameplaySystemPhase::RenderExtraction);
        }

        [[nodiscard]] bool HasValidFactoryAndAccess(const GameplaySystemRegistration &registration) {
            const GameplaySystemDescriptor &descriptor = registration.descriptor;
            return registration.factory.create != nullptr && registration.factory.destroy != nullptr && IsValidPhase(descriptor.phase) &&
                   HasValidBounds(descriptor) && HasValidSystemAccess(descriptor) && !HasInvalidPhaseWrites(descriptor);
        }

        [[nodiscard]] bool HasValidDependencies(const GameplaySystemDescriptor &descriptor) {
            return !ContainsInvalidOrDuplicateIds<GameplaySystemId>(descriptor.after) &&
                   !ContainsInvalidOrDuplicateIds<GameplaySystemId>(descriptor.before) &&
                   !ContainsInvalidOrDuplicateIds<GameplayServiceId>(descriptor.requiredServices) &&
                   !ContainsInvalidOrDuplicateIds<GameplayCapabilityId>(descriptor.requiredCapabilities) &&
                   std::ranges::find(descriptor.after, descriptor.id) == descriptor.after.end() &&
                   std::ranges::find(descriptor.before, descriptor.id) == descriptor.before.end();
        }
    }  // namespace

    Result<void> ValidateSystemRegistration(const GameplaySystemRegistration &registration, const std::string_view moduleId) {
        if (const GameplaySystemDescriptor &descriptor = registration.descriptor;
            !HasValidSystemIdentity(descriptor, moduleId) || !HasValidFactoryAndAccess(registration) || !HasValidDependencies(descriptor))
            return Result<void>::Failure(MakeError(GameplayErrors::InvalidSystemDescriptor));
        return Result<void>::Success();
    }
}  // namespace Horo::Gameplay::Detail
