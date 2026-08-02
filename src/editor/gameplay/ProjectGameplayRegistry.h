#pragma once

/**
 * @file ProjectGameplayRegistry.h
 * @brief Project-scoped discovery and ownership of editor-visible gameplay behaviors.
 */

#include "Horo/Gameplay/BehaviorRegistry.h"
#include "Horo/Gameplay/GameModuleHost.h"
#include "Horo/Gameplay/LuaBehavior.h"

#include <filesystem>
#include <memory>
#include <optional>
#include <vector>

namespace Horo::Editor {
    /** @brief One gameplay source diagnostic that blocks starting a play session. */
    struct ProjectGameplayDiagnostic {
        std::filesystem::path source;
        Error error;
    };

    /** @brief Owns discovered project behavior programs and their frozen registry snapshot. */
    class ProjectGameplayRegistry final {
    public:
        /** @brief Discovers bounded Lua behavior assets below `<project>/assets/scripts`. */
        [[nodiscard]] static std::unique_ptr<ProjectGameplayRegistry> Discover(const std::filesystem::path &projectRoot);

        ProjectGameplayRegistry(const ProjectGameplayRegistry &) = delete;
        ProjectGameplayRegistry &operator=(const ProjectGameplayRegistry &) = delete;

        /** @brief Returns the immutable registry used by authoring and new play sessions. */
        [[nodiscard]] const Gameplay::BehaviorRegistry &Registry() const noexcept;
        /** @brief Returns source-addressed compile and registration failures. */
        [[nodiscard]] const std::vector<ProjectGameplayDiagnostic> &Diagnostics() const noexcept;
        /** @brief Reports whether source failures prevent a coherent registry snapshot. */
        [[nodiscard]] bool HasBlockingDiagnostics() const noexcept;
        /** @brief Validates changed Lua candidates and swaps compatible programs at callback safe points. */
        [[nodiscard]] std::vector<ProjectGameplayDiagnostic> ReloadChangedLuaSources();
        /** @brief Consumes a native artifact-manifest create, replace, or remove transition. */
        [[nodiscard]] bool ConsumeNativeArtifactChange();

    private:
        struct LuaSourceStat {
            std::filesystem::file_time_type sourceWriteTime;
            std::filesystem::file_time_type metadataWriteTime;
            std::uintmax_t sourceSize{};
            std::uintmax_t metadataSize{};

            [[nodiscard]] auto operator<=>(const LuaSourceStat &) const = default;
        };

        [[nodiscard]] static LuaSourceStat ReadLuaSourceStat(const std::filesystem::path &source, std::error_code &error);

        ProjectGameplayRegistry() = default;

        std::unique_ptr<Gameplay::LoadedGameModule> nativeModule_;
        std::vector<std::unique_ptr<Gameplay::LuaBehaviorProgram>> luaPrograms_;
        std::vector<std::filesystem::path> luaSources_;
        std::vector<LuaSourceStat> luaSourceStats_;
        std::filesystem::path nativeManifestPath_;
        std::optional<std::filesystem::file_time_type> nativeManifestWriteTime_;
        Gameplay::BehaviorRegistry registry_;
        std::vector<ProjectGameplayDiagnostic> diagnostics_;
    };
}  // namespace Horo::Editor
