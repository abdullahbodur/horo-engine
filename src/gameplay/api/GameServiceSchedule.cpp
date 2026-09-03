#include "GameServiceRegistryDetail.h"
#include "Horo/Gameplay/GameplayErrors.h"

#include <algorithm>
#include <optional>
#include <unordered_map>
#include <unordered_set>

namespace Horo::Gameplay::Detail {
    namespace {
        struct ServiceGraph {
            std::vector<std::vector<std::size_t>> dependants;
            std::vector<std::size_t> incoming;
        };

        void AddDependencyEdge(const std::size_t provider, const std::size_t dependant, std::vector<std::vector<std::size_t>> &dependants,
                               std::vector<std::size_t> &incoming) {
            if (std::ranges::find(dependants[provider], dependant) != dependants[provider].end())
                return;
            dependants[provider].push_back(dependant);
            ++incoming[dependant];
        }

        [[nodiscard]] ServiceGraph BuildGraph(const std::span<const GameplayServiceRegistration> registrations,
                                              const std::span<const GameplayCapabilityId> hostCapabilities) {
            std::unordered_map<std::string_view, std::size_t> index;
            std::unordered_map<std::string_view, std::size_t> capabilityProviders;
            std::unordered_set<std::string_view> hostCapabilityIndex;
            index.reserve(registrations.size());
            for (const GameplayCapabilityId &capability : hostCapabilities)
                hostCapabilityIndex.emplace(capability.Value());
            for (std::size_t position = 0; position < registrations.size(); ++position) {
                index.try_emplace(registrations[position].descriptor.id.Value(), position);
                for (const GameplayCapabilityId &capability : registrations[position].descriptor.providedCapabilities)
                    capabilityProviders.try_emplace(capability.Value(), position);
            }

            ServiceGraph graph{std::vector<std::vector<std::size_t>>(registrations.size()), std::vector<std::size_t>(registrations.size())};
            for (std::size_t dependant = 0; dependant < registrations.size(); ++dependant) {
                for (const GameplayServiceId &dependency : registrations[dependant].descriptor.dependencies)
                    AddDependencyEdge(index.at(dependency.Value()), dependant, graph.dependants, graph.incoming);
                for (const GameplayCapabilityId &capability : registrations[dependant].descriptor.requiredCapabilities) {
                    if (!hostCapabilityIndex.contains(capability.Value()))
                        AddDependencyEdge(capabilityProviders.at(capability.Value()), dependant, graph.dependants, graph.incoming);
                }
            }
            return graph;
        }

        [[nodiscard]] std::optional<std::size_t> SelectNextService(const std::span<const GameplayServiceRegistration> registrations,
                                                                   const std::span<const std::size_t> incoming,
                                                                   const std::span<const std::size_t> order) {
            std::optional<std::size_t> selected;
            for (std::size_t candidate = 0; candidate < registrations.size(); ++candidate) {
                if (incoming[candidate] != 0 || std::ranges::find(order, candidate) != order.end())
                    continue;
                if (!selected || registrations[candidate].descriptor.id < registrations[*selected].descriptor.id)
                    selected = candidate;
            }
            return selected;
        }
    }  // namespace

    Result<std::vector<std::size_t>> BuildServiceOrder(const std::span<const GameplayServiceRegistration> registrations,
                                                       const std::span<const GameplayCapabilityId> hostCapabilities) {
        ServiceGraph graph = BuildGraph(registrations, hostCapabilities);

        std::vector<std::size_t> order;
        order.reserve(registrations.size());
        while (order.size() < registrations.size()) {
            const std::optional<std::size_t> selected = SelectNextService(registrations, graph.incoming, order);
            if (!selected.has_value())
                return Result<std::vector<std::size_t>>::Failure(MakeError(GameplayErrors::ServiceDependencyCycle));
            order.push_back(*selected);
            for (const std::size_t dependant : graph.dependants[*selected])
                --graph.incoming[dependant];
        }
        return Result<std::vector<std::size_t>>::Success(std::move(order));
    }
}  // namespace Horo::Gameplay::Detail
