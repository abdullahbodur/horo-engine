#pragma once

/**
 * @file Utf8.h
 * @brief Shared UTF-8 scalar-sequence validation without normalization.
 */

#include <string_view>

namespace Horo {
    /** @brief Validates a complete UTF-8 scalar sequence without normalizing it. @param text Bytes to validate. @return True for canonical
     * UTF-8 scalar encoding. */
    [[nodiscard]] bool IsValidUtf8ScalarSequence(std::string_view text) noexcept;
}  // namespace Horo
