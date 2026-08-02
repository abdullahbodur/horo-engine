#pragma once

/**
 * @file GameModule.h
 * @brief Exact-SDK-generation native project gameplay module boundary.
 */

#include "Horo/Gameplay/BehaviorRegistry.h"

#include <cstdint>
#include <span>
#include <string_view>

#if defined(_WIN32)
#define HORO_GAME_EXPORT __declspec(dllexport)
#else
#define HORO_GAME_EXPORT __attribute__((visibility("default")))
#endif

namespace Horo::Gameplay {
    inline constexpr std::uint32_t GameplaySdkBoundaryVersion = 1;
    inline constexpr std::string_view GetGameModuleDescriptorSymbol = "GetGameModuleDescriptor";
    inline constexpr std::string_view GetGameplayDescriptorBundleSymbol = "GetGameplayDescriptorBundle";
    inline constexpr std::string_view CreateGameModuleSymbol = "CreateGameModule";
    inline constexpr std::string_view DestroyGameModuleSymbol = "DestroyGameModule";

    /** @brief Returns the exact compiler, platform, configuration, and SDK fingerprint required by this host. */
    [[nodiscard]] std::string_view CurrentGameplayBuildFingerprint() noexcept;

    /** @brief Static compatibility metadata read before any project gameplay object is created. */
    struct GameModuleDescriptor {
        std::uint32_t structSize{sizeof(GameModuleDescriptor)};
        std::uint32_t sdkBoundaryVersion{GameplaySdkBoundaryVersion};
        const char *moduleId{};
        const char *buildFingerprint{};
    };

    /** @brief Complete generated behavior snapshot tied to one module build fingerprint. */
    struct GeneratedGameplayDescriptorBundle {
        std::uint32_t structSize{sizeof(GeneratedGameplayDescriptorBundle)};
        const char *moduleId{};
        const char *buildFingerprint{};
        std::uint64_t descriptorRevision{};
        const BehaviorRegistration *behaviors{};
        std::size_t behaviorCount{};
    };

    /** @brief Narrow startup context for project module-owned services. */
    struct GameRuntimeContext {};

    /** @brief Project-owned module lifecycle valid only for one exact compatible SDK generation. */
    class IGameModule {
    public:
        virtual ~IGameModule() = default;
        [[nodiscard]] virtual Result<void> Start(GameRuntimeContext &context) = 0;
        virtual void Stop(GameRuntimeContext &context) noexcept = 0;
    };

    using GetGameModuleDescriptorFunction = const GameModuleDescriptor *(*)() noexcept;
    using GetGameplayDescriptorBundleFunction = const GeneratedGameplayDescriptorBundle *(*)() noexcept;
    using CreateGameModuleFunction = IGameModule *(*)() noexcept;
    using DestroyGameModuleFunction = void (*)(IGameModule *) noexcept;
}  // namespace Horo::Gameplay
