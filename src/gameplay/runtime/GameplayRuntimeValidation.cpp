#include "GameplayRuntimeValidation.h"

#include "Horo/Gameplay/GameplayErrors.h"

#include <algorithm>

namespace Horo::Gameplay::Detail {
    namespace {
        template <typename Id> [[nodiscard]] bool Contains(const std::span<const Id> values, const Id &required) {
            return std::ranges::find(values, required) != values.end();
        }

        [[nodiscard]] Result<void> ValidateRequirements(const std::span<const GameplayServiceId> requiredServices,
                                                        const std::span<const GameplayCapabilityId> requiredCapabilities,
                                                        const std::span<const GameplayServiceId> activeServices,
                                                        const std::span<const GameplayCapabilityId> capabilities,
                                                        const ErrorCodeDescriptor &missingService) {
            if (std::ranges::any_of(requiredServices, [activeServices](const GameplayServiceId &required) {
                return !Contains(activeServices, required);
            }))
                return Result<void>::Failure(MakeError(missingService));
            if (std::ranges::any_of(requiredCapabilities, [capabilities](const GameplayCapabilityId &required) {
                return !Contains(capabilities, required);
            }))
                return Result<void>::Failure(MakeError(GameplayErrors::CapabilityMissing));
            return Result<void>::Success();
        }
    }  // namespace

    Result<void> ValidateServiceRuntimeDependencies(const GameplayServiceDescriptor &descriptor,
                                                    const std::span<const GameplayServiceId> activeServices,
                                                    const std::span<const GameplayCapabilityId> capabilities) {
        return ValidateRequirements(descriptor.dependencies, descriptor.requiredCapabilities, activeServices, capabilities,
                                    GameplayErrors::ServiceDependencyMissing);
    }

    Result<void> ValidateSystemRuntimeDependencies(const GameplaySystemDescriptor &descriptor,
                                                   const std::span<const GameplayServiceId> activeServices,
                                                   const std::span<const GameplayCapabilityId> capabilities) {
        return ValidateRequirements(descriptor.requiredServices, descriptor.requiredCapabilities, activeServices, capabilities,
                                    GameplayErrors::SystemDependencyMissing);
    }
}  // namespace Horo::Gameplay::Detail
