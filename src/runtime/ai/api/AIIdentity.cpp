#include "Horo/AI/AIIdentity.h"

#include <algorithm>
#include <limits>
#include <vector>

namespace Horo::AI {
    namespace {
        template <typename Identity> [[nodiscard]] Result<void> ValidateDomain(const std::span<const Identity> identities) {
            std::vector<std::uint64_t> orderedValues;
            orderedValues.reserve(identities.size());
            for (const Identity identity : identities) {
                if (!identity.IsValid())
                    return Result<void>::Failure(MakeError(AIErrors::IdentityInvalid));
                orderedValues.push_back(identity.Value());
            }
            std::ranges::sort(orderedValues);
            if (std::ranges::adjacent_find(orderedValues) != orderedValues.end())
                return Result<void>::Failure(MakeError(AIErrors::DescriptorConflict));
            return Result<void>::Success();
        }

        [[nodiscard]] bool ExceedsDescriptorLimit(const AiIdentityDescriptorSet &descriptors) noexcept {
            std::size_t remaining = MaximumAiIdentityDescriptors;
            const std::array sizes{descriptors.agents.size(), descriptors.controllerTypes.size(), descriptors.tasks.size(),
                                   descriptors.blackboardSchemas.size(), descriptors.blackboardKeys.size()};
            for (const std::size_t size : sizes) {
                if (size > remaining)
                    return true;
                remaining -= size;
            }
            return false;
        }
    }  // namespace

    /** @copydoc ValidateAiIdentityDescriptorSet */
    Result<void> ValidateAiIdentityDescriptorSet(const AiIdentityDescriptorSet &descriptors) {
        if (ExceedsDescriptorLimit(descriptors))
            return Result<void>::Failure(MakeError(AIErrors::DescriptorLimitExceeded));
        if (const Result<void> agents = ValidateDomain(descriptors.agents); agents.HasError())
            return agents;
        if (const Result<void> controllers = ValidateDomain(descriptors.controllerTypes); controllers.HasError())
            return controllers;
        if (const Result<void> tasks = ValidateDomain(descriptors.tasks); tasks.HasError())
            return tasks;
        if (const Result<void> schemas = ValidateDomain(descriptors.blackboardSchemas); schemas.HasError())
            return schemas;
        return ValidateDomain(descriptors.blackboardKeys);
    }

    /** @copydoc AiRuntimeIncarnation::Create */
    Result<AiRuntimeIncarnation> AiRuntimeIncarnation::Create(const std::uint64_t value) {
        if (value == 0)
            return Result<AiRuntimeIncarnation>::Failure(MakeError(AIErrors::HandleInvalid));
        return Result<AiRuntimeIncarnation>::Success(AiRuntimeIncarnation{value});
    }

    /** @copydoc AdvanceAiRuntimeGeneration */
    Result<std::uint32_t> AdvanceAiRuntimeGeneration(const std::uint32_t currentGeneration) {
        if (currentGeneration == 0)
            return Result<std::uint32_t>::Failure(MakeError(AIErrors::HandleInvalid));
        if (currentGeneration == std::numeric_limits<std::uint32_t>::max())
            return Result<std::uint32_t>::Failure(MakeError(AIErrors::GenerationExhausted));
        return Result<std::uint32_t>::Success(currentGeneration + 1U);
    }
}  // namespace Horo::AI
