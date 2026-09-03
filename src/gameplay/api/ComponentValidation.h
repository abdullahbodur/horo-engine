#pragma once

#include "Horo/Gameplay/Component.h"

namespace Horo::Gameplay::Detail {
    [[nodiscard]] Result<void> ValidateComponentDescriptor(const ComponentDescriptor &descriptor);
}  // namespace Horo::Gameplay::Detail
