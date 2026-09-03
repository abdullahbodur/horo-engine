#pragma once

#include "Horo/Gameplay/GameServiceRegistry.h"

#include <cstddef>
#include <span>
#include <string_view>
#include <vector>

namespace Horo::Gameplay::Detail {
    [[nodiscard]] Result<void> ValidateServiceRegistration(const GameplayServiceRegistration &registration, std::string_view moduleId);
    [[nodiscard]] Result<void> ValidateServiceGraph(std::span<const GameplayServiceRegistration> registrations,
                                                    std::span<const GameplayCapabilityId> hostCapabilities);
    [[nodiscard]] Result<std::vector<std::size_t>> BuildServiceOrder(std::span<const GameplayServiceRegistration> registrations,
                                                                     std::span<const GameplayCapabilityId> hostCapabilities);
    [[nodiscard]] std::vector<GameplayCapabilityId> CollectProvidedCapabilities(std::span<const GameplayServiceRegistration> registrations);
}  // namespace Horo::Gameplay::Detail
