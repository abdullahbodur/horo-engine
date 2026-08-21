#pragma once

#include "Horo/Extensions/ExtensionManifest.h"
#include "Horo/Foundation/Result.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace Horo::Assets {
    class AssetImporterCatalog;
}

namespace Horo::Extensions {
    struct ExtensionModuleLifetime;

    /** @brief Represents a loaded extension instance. */
    struct LoadedExtension {
        ExtensionManifest manifest;
        std::shared_ptr<ExtensionModuleLifetime> lifetime;
        std::string moduleId;
        std::string moduleVersion;
    };

    /**
     * @brief Manages discovery, loading, and lifecycle of extensions.
     */
    class ExtensionManager {
    public:
        /**
         * @brief Creates an extension manager bound to an optional unsealed importer catalog.
         * @param importerCatalog Host-owned candidate catalog receiving transactional asset.importer registrations.
         */
        explicit ExtensionManager(Assets::AssetImporterCatalog *importerCatalog = nullptr);
        ~ExtensionManager();

        /**
         * @brief Discovers extensions in the specified directory.
         * @param directoryPath Path to the extensions directory (e.g. ~/.horo/plugins).
         * @return A list of discovered manifest paths.
         */
        std::vector<std::string> DiscoverExtensions(const std::string &directoryPath);

        /**
         * @brief Loads an extension from the given directory path.
         * @param extensionDir Path to the extension's root directory.
         * @return Result containing a reference to the loaded extension ID or an error.
         */
        Result<std::string> LoadExtension(const std::string &extensionDir);

        /**
         * @brief Unloads a specific extension by its ID.
         * @param extensionId The ID of the extension to unload.
         */
        void UnloadExtension(const std::string &extensionId);

        /**
         * @brief Unloads all currently loaded extensions.
         */
        void UnloadAll();

        /**
         * @brief Gets a list of all currently loaded extension IDs.
         * @return Vector of extension IDs.
         */
        std::vector<std::string> GetLoadedExtensionIds() const;

    private:
        Assets::AssetImporterCatalog *m_importerCatalog{};
        std::unordered_map<std::string, std::unique_ptr<LoadedExtension>> m_loadedExtensions;
    };

}  // namespace Horo::Extensions
