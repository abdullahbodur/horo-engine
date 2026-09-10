#pragma once

#include <cstddef>

namespace Horo::Tests::AllocationProbe {
    /** @brief Returns the allocation count observed by the test executable. */
    [[nodiscard]] std::size_t Count() noexcept;
}  // namespace Horo::Tests::AllocationProbe
