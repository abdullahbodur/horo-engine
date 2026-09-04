#include "Horo/Packages/PackagePath.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <optional>
#include <span>
#include <utf8proc.h>
#include <utility>
#include <vector>

namespace Horo::Packages {
    namespace {
        const ErrorCodeDescriptor InvalidPath{
            .domain = ErrorDomainId{"packages"},
            .code = ErrorCode{"packages.path.invalid"},
            .defaultSeverity = ErrorSeverity::Error,
            .summary = "Package file path is not portable or canonical.",
            .remediationHint = "Use a bounded relative NFC UTF-8 path without reserved names, traversal or ambiguous components.",
        };

        /** @brief Normalizes bounded UTF-8 in caller-owned storage, including room for the native terminator. */
        [[nodiscard]] std::optional<std::string> Normalize(const std::string_view text, const utf8proc_option_t options) {
            const auto *input = reinterpret_cast<const utf8proc_uint8_t *>(text.data());
            const auto length = static_cast<utf8proc_ssize_t>(text.size());
            const auto count = utf8proc_decompose(input, length, nullptr, 0, options);
            if (count < 0) {
                return std::nullopt;
            }
            std::vector<utf8proc_int32_t> buffer(static_cast<std::size_t>(count) + 1);
            const auto decoded = utf8proc_decompose(input, length, buffer.data(), count, options);
            if (decoded < 0 || decoded > count) {
                return std::nullopt;
            }
            const auto encoded = utf8proc_reencode(buffer.data(), decoded, options);
            if (encoded < 0) {
                return std::nullopt;
            }
            const auto bytes = std::as_bytes(std::span{buffer}).first(static_cast<std::size_t>(encoded));
            std::string result(bytes.size(), '\0');
            std::ranges::transform(bytes, result.begin(), [](const std::byte value) {
                return std::to_integer<char>(value);
            });
            return result;
        }

        /** @brief Rejects non-visible Unicode controls and format characters in a validated UTF-8 sequence. */
        [[nodiscard]] bool HasVisibleCharacters(std::string_view text) {
            while (!text.empty()) {
                utf8proc_int32_t codepoint = 0;
                const auto count = utf8proc_iterate(reinterpret_cast<const utf8proc_uint8_t *>(text.data()),
                                                    static_cast<utf8proc_ssize_t>(text.size()), &codepoint);
                if (count <= 0) {
                    return false;
                }
                const auto category = utf8proc_category(codepoint);
                if (constexpr std::array forbidden{UTF8PROC_CATEGORY_CC, UTF8PROC_CATEGORY_CF, UTF8PROC_CATEGORY_ZL, UTF8PROC_CATEGORY_ZP};
                    std::ranges::find(forbidden, category) != forbidden.end()) {
                    return false;
                }
                text.remove_prefix(static_cast<std::size_t>(count));
            }
            return true;
        }

        /** @brief Recognizes Windows device basenames, including names with extensions. */
        [[nodiscard]] bool IsDeviceName(const std::string_view component) {
            auto stem = component.substr(0, component.find('.'));
            stem = stem.substr(0, stem.find_last_not_of(' ') + 1);
            constexpr std::array devices{"con",  "prn",  "aux",  "nul",  "conin$", "conout$", "clock$", "com1", "com2",
                                         "com3", "com4", "com5", "com6", "com7",   "com8",    "com9",   "lpt1", "lpt2",
                                         "lpt3", "lpt4", "lpt5", "lpt6", "lpt7",   "lpt8",    "lpt9"};
            return std::ranges::find(devices, stem) != devices.end();
        }

        /** @brief Checks slash-separated components without relying on host-specific filesystem parsing. */
        [[nodiscard]] bool HasPortableComponents(std::string_view text) {
            std::size_t depth = 0;
            while (!text.empty()) {
                const auto separator = text.find('/');
                ++depth;
                if (const auto component = text.substr(0, separator); component.empty() || component.size() > 255 ||
                                                                      component.back() == '.' || component.back() == ' ' || depth > 24 ||
                                                                      IsDeviceName(component)) {
                    return false;
                }
                if (separator == std::string_view::npos) {
                    return true;
                }
                text.remove_prefix(separator + 1);
            }
            return false;
        }

        /** @brief Checks portable punctuation and path components for both spelling and collision key. */
        [[nodiscard]] bool IsPortable(const std::string_view text) {
            return text.find_first_of(R"(\:*?"<>|)") == std::string_view::npos && HasPortableComponents(text);
        }
    }  // namespace

    /** @copydoc PackagePath::Parse */
    Result<PackagePath> PackagePath::Parse(const std::string_view text) {
        if (text.empty() || text.size() > 1024) {
            return Result<PackagePath>::Failure(MakeError(InvalidPath));
        }
        constexpr auto canonicalOptions = static_cast<utf8proc_option_t>(UTF8PROC_STABLE | UTF8PROC_COMPOSE | UTF8PROC_REJECTNA);
        if (const auto canonical = Normalize(text, canonicalOptions);
            !canonical || *canonical != text || !HasVisibleCharacters(text) || !IsPortable(text)) {
            return Result<PackagePath>::Failure(MakeError(InvalidPath));
        }
        constexpr auto keyOptions = static_cast<utf8proc_option_t>(canonicalOptions | UTF8PROC_COMPAT | UTF8PROC_CASEFOLD);
        auto key = Normalize(text, keyOptions);
        if (!key || !IsPortable(*key)) {
            return Result<PackagePath>::Failure(MakeError(InvalidPath));
        }
        return Result<PackagePath>::Success(PackagePath{std::string{text}, std::move(*key)});
    }

    /** @copydoc PackagePath::PackagePath */
    PackagePath::PackagePath(std::string value, std::string collisionKey)
        : m_value(std::move(value)), m_collisionKey(std::move(collisionKey)) {}

    /** @copydoc PackagePath::Value */
    const std::string &PackagePath::Value() const noexcept {
        return m_value;
    }

    /** @copydoc PackagePath::CollisionKey */
    const std::string &PackagePath::CollisionKey() const noexcept {
        return m_collisionKey;
    }
}  // namespace Horo::Packages
