#include "GameServiceRegistryDetail.h"
#include "GameplayIdentityValidation.h"
#include "Horo/Gameplay/GameplayErrors.h"

#include <algorithm>

namespace Horo::Gameplay::Detail {
    namespace {
        [[nodiscard]] bool BelongsToModule(const std::string_view value, const std::string_view moduleId) noexcept {
            return value.size() > moduleId.size() + 1 && value.starts_with(moduleId) && value[moduleId.size()] == '.';
        }

        [[nodiscard]] bool HasValidDescriptorShape(const GameplayServiceRegistration &registration, const std::string_view moduleId) {
            const GameplayServiceDescriptor &descriptor = registration.descriptor;
            return descriptor.id.IsValid() && BelongsToModule(descriptor.id.Value(), moduleId) && registration.factory.create != nullptr &&
                   registration.factory.destroy != nullptr && descriptor.dependencies.size() <= MaximumGameplayDependencies &&
                   descriptor.requiredCapabilities.size() <= MaximumGameplayCapabilities &&
                   descriptor.providedCapabilities.size() <= MaximumGameplayCapabilities && !descriptor.observabilityCategory.empty() &&
                   IsNamespacedId(descriptor.observabilityCategory, MaximumGameplayObservabilityCategoryBytes);
        }

        [[nodiscard]] bool HasValidScopePolicy(const GameplayServiceDescriptor &descriptor) noexcept {
            using enum GameplaySceneReplacementPolicy;
            using enum GameplayServiceScope;
            return (descriptor.scope == Project && descriptor.sceneReplacement == Preserve) ||
                   (descriptor.scope == Scene && descriptor.sceneReplacement == Restart);
        }
    }  // namespace

    Result<void> ValidateServiceRegistration(const GameplayServiceRegistration &registration, const std::string_view moduleId) {
        const GameplayServiceDescriptor &descriptor = registration.descriptor;
        if (!HasValidDescriptorShape(registration, moduleId) || !HasValidScopePolicy(descriptor) ||
            ContainsInvalidOrDuplicateIds<GameplayServiceId>(descriptor.dependencies) ||
            ContainsInvalidOrDuplicateIds<GameplayCapabilityId>(descriptor.requiredCapabilities) ||
            ContainsInvalidOrDuplicateIds<GameplayCapabilityId>(descriptor.providedCapabilities))
            return Result<void>::Failure(MakeError(GameplayErrors::InvalidServiceDescriptor));
        if (std::ranges::find(descriptor.dependencies, descriptor.id) != descriptor.dependencies.end())
            return Result<void>::Failure(MakeError(GameplayErrors::InvalidServiceDescriptor));
        if (std::ranges::any_of(descriptor.providedCapabilities, [moduleId](const GameplayCapabilityId &capability) {
            return !BelongsToModule(capability.Value(), moduleId);
        }))
            return Result<void>::Failure(MakeError(GameplayErrors::InvalidServiceDescriptor));
        return Result<void>::Success();
    }

    std::vector<GameplayCapabilityId> CollectProvidedCapabilities(const std::span<const GameplayServiceRegistration> registrations) {
        std::vector<GameplayCapabilityId> capabilities;
        for (const GameplayServiceRegistration &registration : registrations)
            capabilities.insert(capabilities.end(), registration.descriptor.providedCapabilities.begin(),
                                registration.descriptor.providedCapabilities.end());
        std::ranges::sort(capabilities);
        return capabilities;
    }
}  // namespace Horo::Gameplay::Detail
