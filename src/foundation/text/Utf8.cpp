#include "Horo/Foundation/Utf8.h"

#include <limits>
#include <utf8proc.h>

namespace Horo {
    /** @copydoc IsValidUtf8ScalarSequence */
    bool IsValidUtf8ScalarSequence(const std::string_view text) noexcept {
        if (text.size() > static_cast<std::size_t>(std::numeric_limits<utf8proc_ssize_t>::max()))
            return false;
        const auto *cursor = reinterpret_cast<const utf8proc_uint8_t *>(text.data());
        auto remaining = static_cast<utf8proc_ssize_t>(text.size());
        while (remaining > 0) {
            utf8proc_int32_t codepoint{};
            const utf8proc_ssize_t decoded = utf8proc_iterate(cursor, remaining, &codepoint);
            if (decoded <= 0)
                return false;
            cursor += decoded;
            remaining -= decoded;
        }
        return true;
    }
}  // namespace Horo
