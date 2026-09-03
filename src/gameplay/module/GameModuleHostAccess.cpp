#include "GameModuleHostDetail.h"
#include "Horo/Gameplay/GameModuleHost.h"

#include <utility>

namespace Horo::Gameplay {
    LoadedGameModule::LoadedGameModule(std::unique_ptr<Impl> impl) noexcept : impl_(std::move(impl)) {}

    LoadedGameModule::~LoadedGameModule() {
        if (impl_)
            impl_->Shutdown();
    }

    /** @copydoc LoadedGameModule::ModuleId */
    const std::string &LoadedGameModule::ModuleId() const noexcept {
        return impl_->moduleId;
    }

    /** @copydoc LoadedGameModule::BuildFingerprint */
    const std::string &LoadedGameModule::BuildFingerprint() const noexcept {
        return impl_->buildFingerprint;
    }

    /** @copydoc LoadedGameModule::DescriptorRevision */
    std::uint64_t LoadedGameModule::DescriptorRevision() const noexcept {
        return impl_->descriptorRevision;
    }

    /** @copydoc LoadedGameModule::LoadedArtifactPath */
    const std::filesystem::path &LoadedGameModule::LoadedArtifactPath() const noexcept {
        return impl_->loadedArtifactPath;
    }

    /** @copydoc LoadedGameModule::Registry */
    const BehaviorRegistry &LoadedGameModule::Registry() const noexcept {
        return *impl_->registry;
    }

    /** @copydoc LoadedGameModule::Components */
    const ComponentRegistry &LoadedGameModule::Components() const noexcept {
        return *impl_->components;
    }

    /** @copydoc LoadedGameModule::Services */
    const GameServiceRegistry &LoadedGameModule::Services() const noexcept {
        return *impl_->services;
    }

    /** @copydoc LoadedGameModule::Systems */
    const SystemRegistry &LoadedGameModule::Systems() const noexcept {
        return *impl_->systems;
    }

    /** @copydoc LoadedGameModule::ActiveServices */
    std::span<const GameplayServiceId> LoadedGameModule::ActiveServices() const noexcept {
        return impl_->projectServices->ActiveServices();
    }

    /** @copydoc LoadedGameModule::Capabilities */
    std::span<const GameplayCapabilityId> LoadedGameModule::Capabilities() const noexcept {
        return impl_->projectServices->Capabilities();
    }

    /** @copydoc LoadedGameModule::Cancellation */
    CancellationToken LoadedGameModule::Cancellation() const noexcept {
        return impl_->projectServices->Cancellation();
    }

    /** @copydoc GameModuleHost::GameModuleHost */
    GameModuleHost::GameModuleHost(std::vector<GameplayCapabilityId> hostCapabilities) : hostCapabilities_(std::move(hostCapabilities)) {}
}  // namespace Horo::Gameplay
