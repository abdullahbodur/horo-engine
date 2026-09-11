#pragma once

#include "Horo/Runtime/Ui/UiIdentity.h"

#include <cstdint>

namespace Horo::Runtime::Ui::Test {
    template <typename Id> Id Stable(const std::uint8_t marker) {
        SerializedUiId bytes{};
        bytes.back() = marker;
        return Id::Create(bytes).Value();
    }
}  // namespace Horo::Runtime::Ui::Test
