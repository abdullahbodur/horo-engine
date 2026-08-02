#pragma once

#include <string>
#include <string_view>

namespace Horo::Editor
{
    /** @brief Headless interface for resolving localized strings. */
    class ILocalizationService
    {
    public:
        virtual ~ILocalizationService() = default;

        /**
         * @brief Returns a localized string reference for the given key.
         *
         * If the key is not found, the service returns a formatted missing key string
         * rather than crashing, ensuring safe usage in immediate-mode GUIs.
         */
        [[nodiscard]] virtual const std::string &Get(std::string_view namespaceId, std::string_view localKey) const = 0;
    };
} // namespace Horo::Editor
