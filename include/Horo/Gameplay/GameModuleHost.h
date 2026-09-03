#pragma once

/**
 * @file GameModuleHost.h
 * @brief Validated ownership and unload ordering for one project gameplay dynamic library.
 */

#include "Horo/Gameplay/GameModule.h"

#include <filesystem>
#include <memory>
#include <string>

namespace Horo::Gameplay {
    class BehaviorRegistry;

    /** @brief Loaded module whose registry and callable objects are destroyed before its library unloads. */
    class LoadedGameModule final {
    public:
        ~LoadedGameModule();
        LoadedGameModule(const LoadedGameModule &) = delete;
        LoadedGameModule &operator=(const LoadedGameModule &) = delete;
        /** @brief Returns copied compatibility metadata. */
        [[nodiscard]] const std::string &ModuleId() const noexcept;
        /** @brief Returns copied build fingerprint. */
        [[nodiscard]] const std::string &BuildFingerprint() const noexcept;
        /** @brief Returns the complete generated descriptor revision loaded from the module. */
        [[nodiscard]] std::uint64_t DescriptorRevision() const noexcept;
        /** @brief Returns the artifact path actually loaded by the platform library loader. */
        [[nodiscard]] const std::filesystem::path &LoadedArtifactPath() const noexcept;
        /** @brief Returns the frozen descriptor registry while the module is loaded. */
        [[nodiscard]] const BehaviorRegistry &Registry() const noexcept;

    private:
        friend class GameModuleHost;
        struct Impl;
        explicit LoadedGameModule(std::unique_ptr<Impl> impl) noexcept;
        std::unique_ptr<Impl> impl_;
    };

    /** @brief Loader used by editor play sessions and packaged runtime composition. */
    class GameModuleHost final {
    public:
        /**
         * @brief Loads and starts one gameplay candidate after complete compatibility validation.
         * @param libraryPath Absolute dynamic-library artifact path.
         * @param expectation Manifest identity selected by the active engine/toolchain build.
         * @return Owned active module or a typed failure with no remaining project callable.
         */
        [[nodiscard]] Result<std::unique_ptr<LoadedGameModule>> Load(const std::filesystem::path &libraryPath,
                                                                     const GameModuleLoadExpectation &expectation) const;
        /**
         * @brief Copies a candidate to a unique artifact before validating and loading it.
         * @param libraryPath Absolute source artifact produced by the project build.
         * @param shadowRoot Absolute editor-owned directory for reload candidates.
         * @param expectation Manifest identity selected by the active engine/toolchain build.
         * @return Independently loaded candidate; its shadow artifact is removed after unload.
         *
         * The currently active module can remain loaded while this candidate is validated. The
         * caller still owns the fixed-tick safe-point swap and must destroy every old behavior
         * instance before releasing the previous LoadedGameModule.
         */
        [[nodiscard]] Result<std::unique_ptr<LoadedGameModule>> LoadShadowCopy(const std::filesystem::path &libraryPath,
                                                                               const std::filesystem::path &shadowRoot,
                                                                               const GameModuleLoadExpectation &expectation) const;
    };
}  // namespace Horo::Gameplay
