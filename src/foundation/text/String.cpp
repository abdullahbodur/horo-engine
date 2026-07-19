#include "Horo/Foundation/String.h"

namespace Horo::Text {
    /** @copydoc IsBlank */
    bool IsBlank(const std::string_view value) noexcept {
        for (const unsigned char character: value) {
            if (std::isspace(character) == 0) {
                return false;
            }
        }
        return true;
    }
}
