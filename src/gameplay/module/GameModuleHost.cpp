#include "Horo/Gameplay/GameModuleHost.h"

#include "Horo/Gameplay/GameplayErrors.h"
#include "Horo/Platform/DynamicLibrary.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <format>
#include <system_error>
#include <utility>

namespace Horo::Gameplay {
    namespace {
        template <typename Function>
        [[nodiscard]] Function Resolve(const Platform::DynamicLibrary &library, const std::string_view name) noexcept {
            void *symbol = library.GetSymbol(name);
            Function func{};
            static_assert(sizeof(func) == sizeof(symbol));
            std::memcpy(&func, &symbol, sizeof(func));
            return func;
        }

        [[nodiscard]] Result<void> ValidateText(const char *value, const std::string_view field) {
            if (value == nullptr || *value == '\0' || std::string_view{value}.size() > 256)
                return Result<void>::Failure(
                    MakeError(GameplayErrors::InvalidBehaviorComponent, std::format("Gameplay module {} is invalid.", field)));
            return Result<void>::Success();
        }

        [[nodiscard]] Error ShadowCopyError(const std::string &message) {
            return MakeError(GameplayErrors::InvalidBehaviorComponent, "Gameplay module shadow copy failed: " + message);
        }
    }  // namespace

    struct LoadedGameModule::Impl {
        std::unique_ptr<Platform::DynamicLibrary> library;
        std::unique_ptr<BehaviorRegistry> registry;
        GameRuntimeContext runtimeContext;
        IGameModule *gameplayModule{};
        DestroyGameModuleFunction destroy{};
        std::string moduleId;
        std::string buildFingerprint;
        std::uint64_t descriptorRevision{};
        std::filesystem::path loadedArtifactPath;
        bool removeArtifactOnUnload{};
        bool started{};
    };

    LoadedGameModule::LoadedGameModule(std::unique_ptr<Impl> impl) noexcept : impl_(std::move(impl)) {}

    LoadedGameModule::~LoadedGameModule() {
        if (!impl_)
            return;
        if (impl_->gameplayModule != nullptr) {
            if (impl_->started) {
                IGameModule *moduleToStop = impl_->gameplayModule;
                GameRuntimeContext ctx = impl_->runtimeContext;
                moduleToStop->Stop(ctx);  // NOSONAR: virtual module teardown is unrelated to filesystem path traversal.
            }
            impl_->destroy(impl_->gameplayModule);
            impl_->gameplayModule = nullptr;
        }
        impl_->registry.reset();
        impl_->library.reset();
        if (impl_->removeArtifactOnUnload) {
            std::error_code ignored;
            // This flag is set only for a canonical, host-created shadow-copy artifact.
            std::filesystem::remove(impl_->loadedArtifactPath, ignored);  // NOSONAR
        }
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

    /** @copydoc GameModuleHost::Load */
    Result<std::unique_ptr<LoadedGameModule>> GameModuleHost::Load(const std::filesystem::path &libraryPath,
                                                                   const std::string_view expectedBuildFingerprint) const {
        if (!libraryPath.is_absolute() || expectedBuildFingerprint.empty())
            return Result<std::unique_ptr<LoadedGameModule>>::Failure(
                MakeError(GameplayErrors::InvalidBehaviorComponent, "Gameplay module path or expected fingerprint is invalid."));

        auto loaded = Platform::LoadDynamicLibrary(libraryPath.string());
        if (loaded.HasError())
            return Result<std::unique_ptr<LoadedGameModule>>::Failure(loaded.ErrorValue());
        auto impl = std::make_unique<LoadedGameModule::Impl>();
        impl->library = std::move(loaded).Value();
        impl->loadedArtifactPath = libraryPath;

        const auto getDescriptor = Resolve<GetGameModuleDescriptorFunction>(*impl->library, GetGameModuleDescriptorSymbol);
        const auto getBundle = Resolve<GetGameplayDescriptorBundleFunction>(*impl->library, GetGameplayDescriptorBundleSymbol);
        const auto create = Resolve<CreateGameModuleFunction>(*impl->library, CreateGameModuleSymbol);
        impl->destroy = Resolve<DestroyGameModuleFunction>(*impl->library, DestroyGameModuleSymbol);
        if (getDescriptor == nullptr || getBundle == nullptr || create == nullptr || impl->destroy == nullptr)
            return Result<std::unique_ptr<LoadedGameModule>>::Failure(
                MakeError(GameplayErrors::InvalidBehaviorComponent, "Gameplay module is missing required exported symbols."));

        const GameModuleDescriptor *descriptor = getDescriptor();
        if (descriptor == nullptr || descriptor->structSize != sizeof(GameModuleDescriptor) ||
            descriptor->sdkBoundaryVersion != GameplaySdkBoundaryVersion || ValidateText(descriptor->moduleId, "moduleId").HasError() ||
            ValidateText(descriptor->buildFingerprint, "buildFingerprint").HasError() ||
            descriptor->buildFingerprint != expectedBuildFingerprint)
            return Result<std::unique_ptr<LoadedGameModule>>::Failure(
                MakeError(GameplayErrors::InvalidBehaviorComponent, "Gameplay module SDK boundary or build fingerprint is incompatible."));
        impl->moduleId = descriptor->moduleId;
        impl->buildFingerprint = descriptor->buildFingerprint;

        const GeneratedGameplayDescriptorBundle *bundle = getBundle();
        if (bundle == nullptr || bundle->structSize != sizeof(GeneratedGameplayDescriptorBundle) || bundle->descriptorRevision == 0 ||
            bundle->moduleId == nullptr || bundle->buildFingerprint == nullptr || bundle->moduleId != impl->moduleId ||
            bundle->buildFingerprint != impl->buildFingerprint || (bundle->behaviorCount != 0 && bundle->behaviors == nullptr))
            return Result<std::unique_ptr<LoadedGameModule>>::Failure(
                MakeError(GameplayErrors::InvalidBehaviorComponent, "Generated gameplay descriptor bundle is stale or incomplete."));
        impl->descriptorRevision = bundle->descriptorRevision;

        impl->registry = std::make_unique<BehaviorRegistry>();
        for (std::size_t index = 0; index < bundle->behaviorCount; ++index) {
            if (Result<void> registered = impl->registry->Register(bundle->behaviors[index]); registered.HasError())
                return Result<std::unique_ptr<LoadedGameModule>>::Failure(registered.ErrorValue());
        }
        if (Result<void> frozen = impl->registry->Freeze(); frozen.HasError())
            return Result<std::unique_ptr<LoadedGameModule>>::Failure(frozen.ErrorValue());

        impl->gameplayModule = create();
        if (impl->gameplayModule == nullptr)
            return Result<std::unique_ptr<LoadedGameModule>>::Failure(
                MakeError(GameplayErrors::InvalidBehaviorComponent, "Gameplay module factory returned no module object."));
        if (Result<void> started = impl->gameplayModule->Start(impl->runtimeContext); started.HasError()) {
            impl->gameplayModule->Stop(impl->runtimeContext);
            impl->destroy(impl->gameplayModule);
            impl->gameplayModule = nullptr;
            return Result<std::unique_ptr<LoadedGameModule>>::Failure(started.ErrorValue());
        }
        impl->started = true;
        return Result<std::unique_ptr<LoadedGameModule>>::Success(std::unique_ptr<LoadedGameModule>{new LoadedGameModule{std::move(impl)}});
    }

    /** @copydoc GameModuleHost::LoadShadowCopy */
    [[nodiscard]] bool IsSafeAbsolutePath(const std::filesystem::path &path) noexcept {
        return !path.empty() && path.is_absolute() && std::ranges::none_of(path, [](const std::filesystem::path &part) {
            return part == "..";
        });
    }

    Result<std::unique_ptr<LoadedGameModule>> GameModuleHost::LoadShadowCopy(const std::filesystem::path &libraryPath,
                                                                             const std::filesystem::path &shadowRoot,
                                                                             const std::string_view expectedBuildFingerprint) const {
        if (!IsSafeAbsolutePath(libraryPath) || !IsSafeAbsolutePath(shadowRoot))
            return Result<std::unique_ptr<LoadedGameModule>>::Failure(
                ShadowCopyError("source and destination paths must be absolute and without traversal."));

        std::error_code filesystemError;
        std::filesystem::create_directories(shadowRoot, filesystemError);  // NOSONAR
        if (filesystemError)
            return Result<std::unique_ptr<LoadedGameModule>>::Failure(ShadowCopyError(filesystemError.message()));

        const std::filesystem::path canonicalRoot = std::filesystem::weakly_canonical(shadowRoot, filesystemError);
        if (filesystemError || !IsSafeAbsolutePath(canonicalRoot))
            return Result<std::unique_ptr<LoadedGameModule>>::Failure(ShadowCopyError("shadow root validation failed"));
        static std::atomic<std::uint64_t> sequence{1};
        const auto timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
        const std::filesystem::path shadowPath =
            canonicalRoot / std::format("{}.horo_reload_{}_{}{}", libraryPath.stem().string(), timestamp, sequence.fetch_add(1),
                                        libraryPath.extension().string());
        const std::filesystem::path canonicalShadow = std::filesystem::weakly_canonical(shadowPath, filesystemError);
        if (const std::filesystem::path relativeShadow = canonicalShadow.lexically_relative(canonicalRoot);
            filesystemError || relativeShadow.empty() || relativeShadow.is_absolute() || *relativeShadow.begin() == "..")
            return Result<std::unique_ptr<LoadedGameModule>>::Failure(ShadowCopyError("shadow path validation failed"));
        if (!std::filesystem::copy_file(libraryPath, shadowPath, std::filesystem::copy_options::none, filesystemError)) {  // NOSONAR
            const std::string message = filesystemError ? filesystemError.message() : "candidate could not be copied.";
            return Result<std::unique_ptr<LoadedGameModule>>::Failure(ShadowCopyError(message));
        }

        Result<std::unique_ptr<LoadedGameModule>> loaded = Load(shadowPath, expectedBuildFingerprint);
        if (loaded.HasError()) {
            std::error_code ignored;
            std::filesystem::remove(shadowPath, ignored);  // NOSONAR
            return loaded;
        }
        loaded.Value()->impl_->removeArtifactOnUnload = true;
        return loaded;
    }
}  // namespace Horo::Gameplay
