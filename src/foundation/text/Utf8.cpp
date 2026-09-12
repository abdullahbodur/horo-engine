#include "Horo/Foundation/Utf8.h"

#include <cstdint>

namespace Horo {
    /** @copydoc IsValidUtf8ScalarSequence */
    bool IsValidUtf8ScalarSequence(const std::string_view text) noexcept {
        const auto byteAt = [&text](const std::size_t index) {
            return static_cast<std::uint8_t>(text[index]);
        };
        const auto continuation = [&byteAt](const std::size_t index) {
            return (byteAt(index) & 0xc0U) == 0x80U;
        };
        std::size_t index{};
        while (index < text.size()) {
            const std::uint8_t lead = byteAt(index);
            if (lead <= 0x7fU) {
                ++index;
                continue;
            }
            if (lead >= 0xc2U && lead <= 0xdfU) {
                if (text.size() - index < 2 || !continuation(index + 1))
                    return false;
                index += 2;
                continue;
            }
            if (lead >= 0xe0U && lead <= 0xefU) {
                if (text.size() - index < 3 || !continuation(index + 1) || !continuation(index + 2))
                    return false;
                const std::uint8_t second = byteAt(index + 1);
                if ((lead == 0xe0U && second < 0xa0U) || (lead == 0xedU && second > 0x9fU))
                    return false;
                index += 3;
                continue;
            }
            if (lead >= 0xf0U && lead <= 0xf4U) {
                if (text.size() - index < 4 || !continuation(index + 1) || !continuation(index + 2) || !continuation(index + 3))
                    return false;
                const std::uint8_t second = byteAt(index + 1);
                if ((lead == 0xf0U && second < 0x90U) || (lead == 0xf4U && second > 0x8fU))
                    return false;
                index += 4;
                continue;
            }
            if (lead >= 0x80U)
                return false;
        }
        return true;
    }
}  // namespace Horo
