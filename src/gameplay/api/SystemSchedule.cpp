#include "Horo/Gameplay/GameplayErrors.h"
#include "SystemRegistryDetail.h"

#include <algorithm>
#include <optional>

namespace Horo::Gameplay::Detail {
    namespace {
        [[nodiscard]] bool WasSelected(const std::vector<std::size_t> &order, const std::size_t candidate) {
            return std::ranges::find(order, candidate) != order.end();
        }

        [[nodiscard]] std::optional<std::size_t> SelectNext(const std::span<const GameplaySystemRegistration> registrations,
                                                            const std::vector<std::size_t> &incoming,
                                                            const std::vector<std::size_t> &order) {
            std::optional<std::size_t> selected;
            for (std::size_t candidate = 0; candidate < registrations.size(); ++candidate) {
                if (incoming[candidate] != 0 || WasSelected(order, candidate))
                    continue;
                const GameplaySystemDescriptor &descriptor = registrations[candidate].descriptor;
                if (!selected || descriptor.phase < registrations[*selected].descriptor.phase ||
                    (descriptor.phase == registrations[*selected].descriptor.phase &&
                     descriptor.id < registrations[*selected].descriptor.id))
                    selected = candidate;
            }
            return selected;
        }
    }  // namespace

    Result<std::vector<std::size_t>> BuildSystemOrder(const std::span<const GameplaySystemRegistration> registrations,
                                                      const SystemEdges &edges) {
        std::vector<std::size_t> incoming(registrations.size());
        for (const std::vector<std::size_t> &dependants : edges) {
            for (const std::size_t dependant : dependants)
                ++incoming[dependant];
        }
        std::vector<std::size_t> order;
        order.reserve(registrations.size());
        while (order.size() < registrations.size()) {
            const std::optional<std::size_t> selected = SelectNext(registrations, incoming, order);
            if (!selected.has_value())
                return Result<std::vector<std::size_t>>::Failure(MakeError(GameplayErrors::SystemScheduleCycle));
            order.push_back(*selected);
            for (const std::size_t dependant : edges[*selected])
                --incoming[dependant];
        }
        return Result<std::vector<std::size_t>>::Success(std::move(order));
    }
}  // namespace Horo::Gameplay::Detail
