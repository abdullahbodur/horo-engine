#pragma once

/**
 * @file GameModule.h
 * @brief Exact-SDK-generation native project gameplay module boundary.
 */

#include "Horo/Gameplay/Behavior.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#if defined(_WIN32)
#define HORO_GAME_EXPORT __declspec(dllexport)
#else
#define HORO_GAME_EXPORT __attribute__((visibility("default")))
#endif

namespace Horo::Gameplay {
    inline constexpr std::uint32_t GameplaySdkBoundaryVersion = 2;
    inline constexpr std::uint32_t GameplayDescriptorBundleSchemaVersion = 1;
    inline constexpr std::size_t MaximumGeneratedBehaviorDescriptors = 4096;
    inline constexpr std::size_t MaximumGeneratedDescriptorDiagnostics = 256;
    inline constexpr std::size_t MaximumGeneratedDiagnosticCodeBytes = 160;
    inline constexpr std::size_t MaximumGeneratedDiagnosticMessageBytes = 1024;
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
    using CreateGameModuleFunction = IGameModule *(*)() noexcept;
    using DestroyGameModuleFunction = void (*)(IGameModule *) noexcept;

    /** @brief Module-owned implementation binding paired with one generated behavior identity. */
    struct GeneratedBehaviorFactoryBinding {
        BehaviorTypeId typeId;
        BehaviorFactoryBinding factory;
    };

    /** @brief Bounded code and context emitted when descriptor generation cannot produce an activatable snapshot. */
    struct GeneratedDescriptorDiagnostic {
        const char *code{};
        const char *message{};
    };

    /** @brief Module-owned lifecycle callbacks validated before project code activation. */
    struct GameModuleLifecycleCallbacks {
        CreateGameModuleFunction create{};
        DestroyGameModuleFunction destroy{};
    };

    /** @brief Complete generated behavior snapshot tied to one module build fingerprint. */
    struct GeneratedGameplayDescriptorBundle {
        std::uint32_t structSize{sizeof(GeneratedGameplayDescriptorBundle)};
        std::uint32_t schemaVersion{GameplayDescriptorBundleSchemaVersion};
        std::uint32_t sdkBoundaryVersion{GameplaySdkBoundaryVersion};
        const char *moduleId{};
        const char *buildFingerprint{};
        std::uint64_t descriptorRevision{};
        const BehaviorDescriptor *behaviors{};
        std::size_t behaviorCount{};
        const GeneratedBehaviorFactoryBinding *nativeFactoryBindings{};
        std::size_t nativeFactoryBindingCount{};
        const GeneratedDescriptorDiagnostic *diagnostics{};
        std::size_t diagnosticCount{};
        GameModuleLifecycleCallbacks lifecycle;
    };

    using GetGameplayDescriptorBundleFunction = const GeneratedGameplayDescriptorBundle *(*)() noexcept;

    /** @brief Exact manifest identity that a candidate must satisfy before its lifecycle starts. */
    struct GameModuleLoadExpectation {
        std::string_view moduleId;
        std::string_view buildFingerprint;
        std::uint64_t descriptorRevision{};
    };

    /**
     * @brief Validates static module compatibility metadata before generated records are read.
     * @param descriptor Candidate module descriptor.
     * @param expectation Identity selected by the validated artifact manifest.
     * @return Success or a stable module-descriptor compatibility error.
     */
    [[nodiscard]] Result<void> ValidateGameModuleDescriptor(const GameModuleDescriptor &descriptor,
                                                            const GameModuleLoadExpectation &expectation);

    /**
     * @brief Validates a complete generated snapshot and all module-owned bindings before activation.
     * @param bundle Candidate generated descriptor bundle.
     * @param expectation Identity selected by the validated artifact manifest.
     * @return Success or a stable bundle validation error.
     */
    [[nodiscard]] Result<void> ValidateGeneratedGameplayDescriptorBundle(const GeneratedGameplayDescriptorBundle &bundle,
                                                                         const GameModuleLoadExpectation &expectation);
}  // namespace Horo::Gameplay
