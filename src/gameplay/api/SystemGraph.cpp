#include "Horo/Gameplay/GameplayErrors.h"
#include "SystemRegistryDetail.h"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>

namespace Horo::Gameplay::Detail {
    namespace {
        using SystemIndex = std::unordered_map<std::string_view, std::size_t>;

        [[nodiscard]] SystemIndex IndexSystems(const std::span<const GameplaySystemRegistration> registrations) {
            SystemIndex index;
            index.reserve(registrations.size());
            for (std::size_t position = 0; position < registrations.size(); ++position)
                index.try_emplace(registrations[position].descriptor.id.Value(), position);
            return index;
        }

        template <typename Id> [[nodiscard]] std::unordered_set<std::string_view> IndexIds(const std::span<const Id> ids) {
            std::unordered_set<std::string_view> index;
            index.reserve(ids.size());
            for (const Id &id : ids)
                index.emplace(id.Value());
            return index;
        }

        [[nodiscard]] Result<void> ValidateRequirements(const std::span<const GameplaySystemRegistration> registrations,
                                                        const std::span<const GameplayServiceId> services,
                                                        const std::span<const GameplayCapabilityId> capabilities) {
            const auto serviceIndex = IndexIds(services);
            const auto capabilityIndex = IndexIds(capabilities);
            for (const GameplaySystemRegistration &registration : registrations) {
                if (std::ranges::any_of(registration.descriptor.requiredServices, [&serviceIndex](const GameplayServiceId &required) {
                    return !serviceIndex.contains(required.Value());
                }))
                    return Result<void>::Failure(MakeError(GameplayErrors::SystemDependencyMissing));
                if (std::ranges::any_of(registration.descriptor.requiredCapabilities,
                                        [&capabilityIndex](const GameplayCapabilityId &required) {
                    return !capabilityIndex.contains(required.Value());
                }))
                    return Result<void>::Failure(MakeError(GameplayErrors::CapabilityMissing));
            }
            return Result<void>::Success();
        }

        [[nodiscard]] bool IsPhaseEdgeValid(const GameplaySystemPhase provider, const GameplaySystemPhase dependant) noexcept {
            return static_cast<std::uint8_t>(provider) <= static_cast<std::uint8_t>(dependant);
        }

        [[nodiscard]] Result<void> AddEdge(const std::size_t provider, const std::size_t dependant,
                                           const std::span<const GameplaySystemRegistration> registrations, SystemEdges &edges) {
            if (!IsPhaseEdgeValid(registrations[provider].descriptor.phase, registrations[dependant].descriptor.phase))
                return Result<void>::Failure(MakeError(GameplayErrors::InvalidSystemDescriptor));
            if (registrations[provider].descriptor.phase == registrations[dependant].descriptor.phase)
                edges[provider].push_back(dependant);
            return Result<void>::Success();
        }
    }  // namespace

    Result<SystemEdges> BuildSystemEdges(const std::span<const GameplaySystemRegistration> registrations,
                                         const std::span<const GameplayServiceId> availableServices,
                                         const std::span<const GameplayCapabilityId> availableCapabilities) {
        if (const Result<void> requirements = ValidateRequirements(registrations, availableServices, availableCapabilities);
            requirements.HasError())
            return Result<SystemEdges>::Failure(requirements.ErrorValue());

        const SystemIndex index = IndexSystems(registrations);
        SystemEdges edges(registrations.size());
        for (std::size_t current = 0; current < registrations.size(); ++current) {
            for (const GameplaySystemId &after : registrations[current].descriptor.after) {
                const auto provider = index.find(after.Value());
                if (provider == index.end())
                    return Result<SystemEdges>::Failure(MakeError(GameplayErrors::SystemDependencyMissing));
                if (const Result<void> added = AddEdge(provider->second, current, registrations, edges); added.HasError())
                    return Result<SystemEdges>::Failure(added.ErrorValue());
            }
            for (const GameplaySystemId &before : registrations[current].descriptor.before) {
                const auto dependant = index.find(before.Value());
                if (dependant == index.end())
                    return Result<SystemEdges>::Failure(MakeError(GameplayErrors::SystemDependencyMissing));
                if (const Result<void> added = AddEdge(current, dependant->second, registrations, edges); added.HasError())
                    return Result<SystemEdges>::Failure(added.ErrorValue());
            }
        }
        for (std::vector<std::size_t> &dependants : edges) {
            std::ranges::sort(dependants);
            dependants.erase(std::ranges::unique(dependants).begin(), dependants.end());
        }
        return Result<SystemEdges>::Success(std::move(edges));
    }
}  // namespace Horo::Gameplay::Detail
