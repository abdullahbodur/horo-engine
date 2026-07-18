/**
 * @file String.h
 * @brief Declares dependency-free string inspection utilities.
 */

#pragma once

#include <string_view>

namespace Horo::Text
{
/**
 * @brief Checks whether a string is empty or contains only whitespace characters.
 * @param value String to inspect.
 * @return True when every character is classified as whitespace, including when the string is empty.
 */
[[nodiscard]] bool IsBlank(std::string_view value) noexcept;
}
