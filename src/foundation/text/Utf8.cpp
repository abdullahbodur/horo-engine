#include "Horo/Foundation/Utf8.h"

#include <cstddef>
#include <cstdint>

namespace Horo {
    namespace {
        [[nodiscard]] std::byte ByteAt(const std::string_view text, const std::size_t index) noexcept {
            return static_cast<std::byte>(static_cast<unsigned char>(text[index]));
        }

        [[nodiscard]] std::uint8_t ByteValue(const std::string_view text, const std::size_t index) noexcept {
            return std::to_integer<std::uint8_t>(ByteAt(text, index));
        }

        [[nodiscard]] bool IsContinuation(const std::string_view text, const std::size_t index) noexcept {
            return (ByteAt(text, index) & std::byte{0xc0}) == std::byte{0x80};
        }

        [[nodiscard]] bool IsValidThreeByteScalar(const std::string_view text, const std::size_t index, const std::uint8_t lead) noexcept {
            if (text.size() - index < 3 || !IsContinuation(text, index + 1) || !IsContinuation(text, index + 2))
                return false;
            const std::uint8_t second = ByteValue(text, index + 1);
            return (lead != 0xe0U || second >= 0xa0U) && (lead != 0xedU || second <= 0x9fU);
        }

        [[nodiscard]] bool IsValidFourByteScalar(const std::string_view text, const std::size_t index, const std::uint8_t lead) noexcept {
            if (text.size() - index < 4 || !IsContinuation(text, index + 1) || !IsContinuation(text, index + 2) ||
                !IsContinuation(text, index + 3)) {
                return false;
            }
            const std::uint8_t second = ByteValue(text, index + 1);
            return (lead != 0xf0U || second >= 0x90U) && (lead != 0xf4U || second <= 0x8fU);
        }
    }  // namespace

    /** @copydoc IsValidUtf8ScalarSequence */
    bool IsValidUtf8ScalarSequence(const std::string_view text) noexcept {
        std::size_t index{};
        while (index < text.size()) {
            const std::uint8_t lead = ByteValue(text, index);
            if (lead <= 0x7fU) {
                ++index;
                continue;
            }
            if (lead >= 0xc2U && lead <= 0xdfU) {
                if (text.size() - index < 2 || !IsContinuation(text, index + 1))
                    return false;
                index += 2;
                continue;
            }
            if (lead >= 0xe0U && lead <= 0xefU) {
                if (!IsValidThreeByteScalar(text, index, lead))
                    return false;
                index += 3;
                continue;
            }
            if (lead >= 0xf0U && lead <= 0xf4U) {
                if (!IsValidFourByteScalar(text, index, lead))
                    return false;
                index += 4;
                continue;
            }
            return false;
        }
        return true;
    }
}  // namespace Horo
