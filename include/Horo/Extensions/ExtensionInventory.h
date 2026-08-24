#pragma once

/**
 * @file ExtensionInventory.h
 * @brief Installed extension-package inventory, desired activation state, and local package installation.
 */

#include "Horo/Extensions/ExtensionManifest.h"
#include "Horo/Foundation/Result.h"
#include "Horo/Foundation/TransparentString.h"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace Horo::Extensions {
    /** @brief Origin of an extension package visible to editor tooling. */
    enum class ExtensionOrigin {
        BuiltIn,
        UserInstalled,
    };

    /** @brief Immutable editor-facing projection of one extension package. */
    struct ExtensionInventoryEntry {
        std::string packageId;                                    /**< Stable package identity. */
        std::string displayName;                                  /**< Human-facing package name. */
        std::string description;                                  /**< Human-facing package summary. */
        std::string version;                                      /**< Canonical package semantic version. */
        std::string author;                                       /**< Declared publisher/author, when present. */
        ExtensionOrigin origin{};                                 /**< Built-in or user-installed source. */
        std::filesystem::path absoluteRootPath;                   /**< Absolute package root; empty for compiled built-ins. */
        std::filesystem::path absoluteManifestPath;               /**< Absolute manifest path; empty for compiled built-ins. */
        std::vector<ExtensionModuleManifest> modules;             /**< Declared executable/declarative modules. */
        std::vector<ExtensionContributionManifest> contributions; /**< Declared typed contributions. */
        bool enabled{};                                           /**< Desired activation state for the next composition. */
        bool runtimeActive{};                                     /**< Whether this process activated the package. */
        bool locallyTrusted{};                                    /**< Explicit local enable/trust decision. */
        std::string loadError;                                    /**< Current-process activation diagnostic, when any. */
        std::string compositionVersion;                           /**< Fingerprint of currently discovered package/module versions. */
        std::string runtimeCompositionVersion;                    /**< Fingerprint activated in this process. */

        /** @brief Reports whether a restart is needed to match desired and active states. */
        [[nodiscard]] bool RestartRequired() const noexcept {
            return enabled != runtimeActive || (runtimeActive && compositionVersion != runtimeCompositionVersion);
        }
    };

    /**
     * @brief Owner-thread service for installed extension discovery and state.
     *
     * User packages live directly below one absolute install root. Installation
     * validates and stages a complete package directory before publishing it.
     * Enabling a user package is also the explicit local trust decision; actual
     * native activation occurs during the next host composition.
     */
    class ExtensionInventory final {
    public:
        /**
         * @brief Creates an inventory for the default or supplied install root.
         * @param absoluteInstallRoot Absolute user extension root, or empty to use the platform user default.
         */
        explicit ExtensionInventory(std::filesystem::path absoluteInstallRoot = {});

        /** @brief Returns the platform user extension directory as an absolute path. */
        [[nodiscard]] static std::filesystem::path DefaultInstallRoot();

        /** @brief Rescans built-ins and installed package manifests without loading native code. */
        [[nodiscard]] Result<void> Refresh();

        /** @brief Returns the current deterministic package projection. */
        [[nodiscard]] const std::vector<ExtensionInventoryEntry> &Entries() const noexcept;

        /** @brief Returns the absolute user extension install root. */
        [[nodiscard]] const std::filesystem::path &InstallRoot() const noexcept;

        /**
         * @brief Installs one package from an absolute directory into the user extension root.
         * @param absoluteSourceDirectory Absolute directory containing `extension.json`.
         * @return Installed package ID, or a typed validation/filesystem error.
         */
        [[nodiscard]] Result<std::string> InstallFromDirectory(const std::filesystem::path &absoluteSourceDirectory);

        /**
         * @brief Persists the desired activation state.
         * @param packageId Exact installed or built-in package ID.
         * @param enabled Desired next-composition state; enabling grants local trust.
         * @return Success after durable state publication.
         */
        [[nodiscard]] Result<void> SetEnabled(std::string_view packageId, bool enabled);

        /** @brief Marks a package active in this process after successful composition. */
        void MarkRuntimeActive(std::string_view packageId);

        /** @brief Records a current-process activation failure for presentation. */
        void SetLoadError(std::string_view packageId, std::string message);

        /** @brief Returns absolute roots of enabled, locally trusted user packages. */
        [[nodiscard]] std::vector<std::filesystem::path> EnabledUserPackageRoots() const;

        /** @brief Reports the desired activation state of a package. */
        [[nodiscard]] bool IsEnabled(std::string_view packageId) const noexcept;

    private:
        [[nodiscard]] Result<void> LoadState();
        [[nodiscard]] Result<void> SaveState() const;
        void AddBuiltInPackages();

        std::filesystem::path installRoot_;
        std::filesystem::path statePath_;
        std::vector<ExtensionInventoryEntry> entries_;
        TransparentStringSet enabled_;
        TransparentStringSet trusted_;
    };
}  // namespace Horo::Extensions
