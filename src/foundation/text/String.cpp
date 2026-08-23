#include "Horo/Foundation/String.h"

#include <algorithm>
#include <cctype>

namespace Horo::Text {
    /** @copydoc IsBlank */
    bool IsBlank(const std::string_view value) noexcept {
        return std::ranges::all_of(value, [](const unsigned char character) noexcept {
            return std::isspace(character) != 0;
        });
    }
}  // namespace Horo::Text
