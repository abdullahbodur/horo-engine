#pragma once

#include "Horo/Gameplay/BehaviorRegistry.h"
#include "Horo/Gameplay/ComponentRegistry.h"
#include "Horo/Gameplay/GameModuleHost.h"
#include "Horo/Gameplay/GameServiceRegistry.h"
#include "Horo/Gameplay/GameplayRegistrationRuntime.h"
#include "Horo/Gameplay/SystemRegistry.h"
#include "Horo/Platform/DynamicLibrary.h"

#include <filesystem>
#include <memory>
#include <string>

namespace Horo::Gameplay {
    struct LoadedGameModule::Impl {
        ~Impl();

        [[nodiscard]] Result<void> RegisterAndStart(std::span<const GameplayCapabilityId> hostCapabilities);
        void Shutdown() noexcept;

        std::unique_ptr<Platform::DynamicLibrary> library;
        std::unique_ptr<BehaviorRegistry> registry;
        std::unique_ptr<ComponentRegistry> components;
        std::unique_ptr<GameServiceRegistry> services;
        std::unique_ptr<SystemRegistry> systems;
        std::unique_ptr<GameplayServiceRuntime> projectServices;
        GameRuntimeContext runtimeContext;
        IGameModule *gameplayModule{};
        DestroyGameModuleFunction destroy{};
        std::string moduleId;
        std::string buildFingerprint;
        std::uint64_t descriptorRevision{};
        std::filesystem::path loadedArtifactPath;
        bool removeArtifactOnUnload{};
        bool startAttempted{};
        bool shutdown{};
    };
}  // namespace Horo::Gameplay
