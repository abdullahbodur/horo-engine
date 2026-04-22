#pragma once
#include <cstdint>
#include <limits>

namespace Monolith {
    using Entity = uint32_t;
    constexpr Entity INVALID_ENTITY = std::numeric_limits<uint32_t>::max();
} // namespace Monolith
