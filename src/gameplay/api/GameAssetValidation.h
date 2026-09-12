#pragma once

#include "Horo/Gameplay/GameAsset.h"

#include <string_view>

namespace Horo::Gameplay::Detail {
    [[nodiscard]] Result<void> ValidateGameAssetRegistration(const GameAssetTypeRegistration &registration, std::string_view moduleId);
}
