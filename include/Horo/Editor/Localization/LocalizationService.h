#pragma once

#include "Horo/Editor/Localization/ILocalizationService.h"
#include "Horo/Editor/Localization/LocalizationTypes.h"

#include <memory>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace Horo::Editor
{
    /** @brief Immutable message catalog for one normalized locale. */
    struct LocalizationCatalog
    {
        LocaleTag locale;
        std::unordered_map<MessageKey, std::string, MessageKeyHash> messages;
    };

    /** @brief Read-only localization view captured for one presentation snapshot. */
    struct LocalizationSnapshot
    {
        std::shared_ptr<const LocalizationCatalog> catalog;
        std::shared_ptr<const LocalizationCatalog> fallbackCatalog;
        std::uint64_t revision = 0;
    };

    /** @brief Editor-localization error returned when a candidate cannot activate. */
    struct LocalizationError
    {
        std::string code;
        std::string message;
    };

    /**
     * @brief Owns immutable editor localization snapshots and locale transitions.
     *
     * Catalog registration is performed before a locale is prepared. Prepare does
     * not mutate the active snapshot; ActivatePrepared is the only operation that
     * changes the visible locale and is intended to run at a frame boundary.
     */
    class LocalizationService final : public ILocalizationService
    {
    public:
        explicit LocalizationService(LocaleTag sourceFallback);

        /** @brief Adds or replaces a validated catalog before activation. */
        [[nodiscard]] bool RegisterCatalog(LocalizationCatalog catalog, LocalizationError *error = nullptr);

        /**
         * @brief Loads and validates one locale catalog from the editor resource format.
         * @param path JSON catalog path.
         * @param error Optional validation or I/O error output.
         * @return true when the catalog was parsed and registered.
         */
        [[nodiscard]] bool LoadCatalogFile(const std::filesystem::path &path, LocalizationError *error = nullptr);

        /** @brief Validates and stages a locale without changing the active snapshot. */
        [[nodiscard]] bool Prepare(LocaleTag locale, LocalizationError *error = nullptr);

        /** @brief Activates the staged catalog at the caller-selected frame boundary. */
        [[nodiscard]] bool ActivatePrepared(LocalizationError *error = nullptr);

        /** @brief Returns the active immutable snapshot by value. */
        [[nodiscard]] LocalizationSnapshot Snapshot() const noexcept;

        /** @brief Resolves a message. Returns a reference to the mapped string or a cached missing-key string. */
        [[nodiscard]] const std::string& Get(std::string_view namespaceId, std::string_view localKey) const override;

        /** @brief Returns the currently active locale. */
        [[nodiscard]] LocaleTag ActiveLocale() const;

    private:
        std::shared_ptr<const LocalizationCatalog> FindCatalog(const LocaleTag &locale) const;
        static void SetError(LocalizationError *error, std::string code, std::string message);

        LocaleTag m_sourceFallback;
        std::unordered_map<std::string, std::shared_ptr<const LocalizationCatalog>> m_catalogs;
        std::shared_ptr<const LocalizationCatalog> m_active;
        std::shared_ptr<const LocalizationCatalog> m_prepared;
        std::uint64_t m_revision = 0;
        mutable std::unordered_set<std::string> m_missingCache;
    };
} // namespace Horo::Editor
