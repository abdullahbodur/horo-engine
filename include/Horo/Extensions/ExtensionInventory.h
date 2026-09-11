#pragma once

/**
 * @file ExtensionInventory.h
 * @brief Installed extension-package inventory, desired activation state, and local package installation.
 */

#include "Horo/Extensions/ExtensionActivationState.h"
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
        bool locallyTrusted{};                                    /**< Whether trust covers compositionVersion exactly. */
        ExtensionHostCompatibilityState hostCompatibility{ExtensionHostCompatibilityState::NotEvaluated}; /**< Host fit. */
        ExtensionActivationOutcome activationOutcome{ExtensionActivationOutcome::NotAttempted};           /**< Last activation attempt. */
        ExtensionActivationFailure activationFailure{}; /**< Typed failure plus bounded presentation detail. */
        std::string loadError;                          /**< Compatibility projection of activationFailure.message. */
        std::string compositionVersion;                 /**< Fingerprint of currently discovered package/module versions. */
        std::string trustedCompositionVersion;          /**< Exact composition covered by local trust. */
        std::string runtimeCompositionVersion;          /**< Fingerprint activated in this process. */
        std::uint64_t stateRevision{1};                 /**< Monotonic process-local projection revision. */
        std::uint64_t activationGeneration{};           /**< Monotonic host activation generation. */

        /**
         * @brief Builds the canonical typed lifecycle projection without creating another source of truth.
         * @return Complete typed lifecycle facts for this entry.
         */
        [[nodiscard]] ExtensionActivationProjection ActivationState() const;

        /** @brief Reports whether a restart is needed to match desired and active states. @return True when state is divergent. */
        [[nodiscard]] bool RestartRequired() const;
    };

    /**
     * @brief Owner-thread service for installed extension discovery and state.
     *
     * User packages live directly below one absolute install root. Installation
     * validates and stages a complete package directory before publishing it.
     * Enablement is portable project intent and never grants execution trust.
     * Explicit local trust is bound to the exact discovered composition; actual
     * native activation occurs during a later host composition.
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
         * @param enabled Desired next-composition state; this never changes trust.
         * @return Success after durable state publication.
         */
        [[nodiscard]] Result<void> SetEnabled(std::string_view packageId, bool enabled);

        /**
         * @brief Persists an explicit local trust decision for the exact current composition.
         * @param packageId Exact installed or built-in package ID.
         * @param trusted Whether to grant or revoke local execution trust.
         * @return Success after durable state publication.
         */
        [[nodiscard]] Result<void> SetTrusted(std::string_view packageId, bool trusted);

        /**
         * @brief Marks an eligible exact package composition active in a new host generation.
         * @param packageId Exact installed or built-in package ID.
         * @return Success, or a typed invalid-transition failure.
         */
        [[nodiscard]] Result<void> MarkRuntimeActive(std::string_view packageId);

        /**
         * @brief Records a typed current-process activation failure without changing trust or enablement.
         * @param packageId Exact installed or built-in package ID.
         * @param reason Stable control-flow failure reason.
         * @param message Bounded presentation-only diagnostic.
         * @return Success, or a typed invalid-transition failure.
         */
        [[nodiscard]] Result<void> RecordActivationFailure(std::string_view packageId, ExtensionActivationFailureReason reason,
                                                           std::string message);

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
        TransparentStringMap<std::string> trustedCompositions_;
        std::uint64_t nextActivationGeneration_{1};
    };
}  // namespace Horo::Extensions
