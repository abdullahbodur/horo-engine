#pragma once

#include "Horo/Extensions/ExtensionManifest.h"
#include "Horo/Foundation/Result.h"
#include "Horo/Foundation/TransparentString.h"

#include <memory>
#include <string>
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
     * @brief Manages loading and lifecycle of extensions.
     */
    class ExtensionManager {
    public:
        /**
         * @brief Creates an extension manager bound to an optional unsealed importer catalog.
         * @param importerCatalog Host-owned candidate catalog receiving transactional asset.importer registrations.
         */
        explicit ExtensionManager(Assets::AssetImporterCatalog *importerCatalog = nullptr);
        ~ExtensionManager();
        ExtensionManager(const ExtensionManager &) = delete;
        ExtensionManager &operator=(const ExtensionManager &) = delete;
        ExtensionManager(ExtensionManager &&) noexcept;
        ExtensionManager &operator=(ExtensionManager &&) noexcept;

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
        TransparentStringMap<std::unique_ptr<LoadedExtension>> m_loadedExtensions;
    };

}  // namespace Horo::Extensions
