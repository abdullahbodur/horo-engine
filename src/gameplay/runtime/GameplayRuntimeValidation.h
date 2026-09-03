#pragma once

#include "Horo/Gameplay/GameplayRegistration.h"

namespace Horo::Gameplay::Detail {
    [[nodiscard]] Result<void> ValidateServiceRuntimeDependencies(const GameplayServiceDescriptor &descriptor,
                                                                  std::span<const GameplayServiceId> activeServices,
                                                                  std::span<const GameplayCapabilityId> capabilities);
    [[nodiscard]] Result<void> ValidateSystemRuntimeDependencies(const GameplaySystemDescriptor &descriptor,
                                                                 std::span<const GameplayServiceId> activeServices,
                                                                 std::span<const GameplayCapabilityId> capabilities);
}  // namespace Horo::Gameplay::Detail
