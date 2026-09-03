#pragma once

#include "Horo/Gameplay/SystemRegistry.h"

#include <cstddef>
#include <span>
#include <string_view>
#include <vector>

namespace Horo::Gameplay::Detail {
    using SystemEdges = std::vector<std::vector<std::size_t>>;

    [[nodiscard]] Result<void> ValidateSystemRegistration(const GameplaySystemRegistration &registration, std::string_view moduleId);
    [[nodiscard]] bool HasValidSystemIdentity(const GameplaySystemDescriptor &descriptor, std::string_view moduleId) noexcept;
    [[nodiscard]] bool HasValidSystemAccess(const GameplaySystemDescriptor &descriptor);
    [[nodiscard]] Result<SystemEdges> BuildSystemEdges(std::span<const GameplaySystemRegistration> registrations,
                                                       std::span<const GameplayServiceId> availableServices,
                                                       std::span<const GameplayCapabilityId> availableCapabilities);
    [[nodiscard]] Result<void> ValidateSystemAccess(std::span<const GameplaySystemRegistration> registrations, const SystemEdges &edges);
    [[nodiscard]] std::vector<std::vector<bool>> BuildSystemReachability(const SystemEdges &edges);
    [[nodiscard]] Result<std::vector<std::size_t>> BuildSystemOrder(std::span<const GameplaySystemRegistration> registrations,
                                                                    const SystemEdges &edges);
}  // namespace Horo::Gameplay::Detail
