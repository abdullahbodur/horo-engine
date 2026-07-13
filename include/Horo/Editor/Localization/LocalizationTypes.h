#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace Horo::Editor
{
    /** @brief Normalized BCP 47 locale identity used by editor localization. */
    struct LocaleTag
    {
        std::string value;

        [[nodiscard]] static std::optional<LocaleTag> Parse(std::string_view tag);
        [[nodiscard]] bool operator==(const LocaleTag &) const = default;
    };

    /** @brief Stable namespace and semantic local key identifying one message. */
    struct MessageKey
    {
        std::string namespaceId;
        std::string localKey;

        [[nodiscard]] std::string Canonical() const;
        [[nodiscard]] bool operator==(const MessageKey &) const = default;
    };

    /** @brief Hash for MessageKey storage without constructing canonical lookup strings. */
    struct MessageKeyHash
    {
        [[nodiscard]] std::size_t operator()(const MessageKey &key) const noexcept;
    };

    /** @brief Value-owned deferred message reference resolved against the active snapshot. */
} // namespace Horo::Editor
