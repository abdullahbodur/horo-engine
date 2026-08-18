#pragma once

#include "Horo/Assets/AssetImporter.h"
#include "Horo/Extensions/ExtensionAbi.h"
#include "Horo/Extensions/ExtensionManifest.h"
#include "Horo/Platform/DynamicLibrary.h"

#include <memory>
#include <vector>

namespace Horo::Extensions {
    /** @brief Shared library/module lease retained by every external contribution adapter. */
    struct ExtensionModuleLifetime final {
        ExtensionModuleLifetime() = default;
        ~ExtensionModuleLifetime();
        ExtensionModuleLifetime(const ExtensionModuleLifetime &) = delete;
        ExtensionModuleLifetime &operator=(const ExtensionModuleLifetime &) = delete;
        ExtensionModuleLifetime(ExtensionModuleLifetime &&) noexcept = default;
        ExtensionModuleLifetime &operator=(ExtensionModuleLifetime &&) noexcept = default;

        std::shared_ptr<Platform::DynamicLibrary> library;
        HoroExtensionModuleApi moduleApi{};
        HoroExtensionUnloadFunc unload{};
        bool loaded{};
    };

    /** @brief Host-side state used only during one module load transaction. */
    struct AssetImporterRegistrationSession final {
        const ExtensionManifest *manifest{};
        const ExtensionModuleManifest *extensionModule{};
        std::shared_ptr<ExtensionModuleLifetime> lifetime;
        std::vector<Assets::AssetImporterContribution> contributions;
        Error error;
        bool failed{};
    };

    /**
     * @brief Copies and validates one C ABI importer descriptor into a load transaction.
     * @param hostContext Pointer to AssetImporterRegistrationSession.
     * @param descriptor Borrowed module-owned descriptor.
     * @return C ABI status; failure leaves the live host catalog untouched.
     */
    HoroExtensionStatus RegisterExternalAssetImporter(void *hostContext, const HoroAssetImporterDescriptor *descriptor) noexcept;
}  // namespace Horo::Extensions
