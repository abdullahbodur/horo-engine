#pragma once

#include "Horo/Gameplay/GameplayRegistration.h"

namespace Horo::Gameplay::Detail {
    [[nodiscard]] Result<IGameplayService *> CreateService(const GameplayServiceRegistration &registration) noexcept;
    [[nodiscard]] Result<void> StartService(IGameplayService &service, const GameplayServiceContext &context) noexcept;
    [[nodiscard]] Result<IGameplaySystem *> CreateSystem(const GameplaySystemRegistration &registration) noexcept;
    [[nodiscard]] Result<void> StartSystem(IGameplaySystem &system, const GameplaySystemContext &context) noexcept;
    [[nodiscard]] Result<void> ExecuteSystem(IGameplaySystem &system, const GameplaySystemContext &context) noexcept;
}  // namespace Horo::Gameplay::Detail
