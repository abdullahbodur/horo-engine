#include "Horo/Gameplay/GameModuleHost.h"

#include "Horo/Gameplay/BehaviorRegistry.h"
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

        [[nodiscard]] Error ShadowCopyError(const std::string &message) {
            return MakeError(GameplayErrors::InvalidBehaviorComponent, "Gameplay module shadow copy failed: " + message);
        }
    }  // namespace

    struct LoadedGameModule::Impl {
        std::unique_ptr<Platform::DynamicLibrary> library;
        std::unique_ptr<BehaviorRegistry> registry;
        [[no_unique_address]] GameRuntimeContext runtimeContext;
        IGameModule *gameplayModule{};

        DestroyGameModuleFunction destroy{};
        std::string moduleId;
        std::string buildFingerprint;
        std::uint64_t descriptorRevision{};
        std::filesystem::path loadedArtifactPath;
        bool removeArtifactOnUnload{};
        bool started{};
    };

    namespace {
        struct ValidatedModuleExports {
            const GameModuleDescriptor *descriptor{};
            const GeneratedGameplayDescriptorBundle *bundle{};
        };

        [[nodiscard]] Result<ValidatedModuleExports> ReadValidatedExports(const Platform::DynamicLibrary &library,
                                                                          const GameModuleLoadExpectation &expectation) {
            const auto getDescriptor = Resolve<GetGameModuleDescriptorFunction>(library, GetGameModuleDescriptorSymbol);
            const auto getBundle = Resolve<GetGameplayDescriptorBundleFunction>(library, GetGameplayDescriptorBundleSymbol);
            if (getDescriptor == nullptr || getBundle == nullptr)
                return Result<ValidatedModuleExports>::Failure(
                    MakeError(GameplayErrors::InvalidGameModuleDescriptor, "Gameplay module is missing required descriptor exports."));
            const GameModuleDescriptor *descriptor = getDescriptor();
            const GeneratedGameplayDescriptorBundle *bundle = getBundle();
            if (descriptor == nullptr)
                return Result<ValidatedModuleExports>::Failure(
                    MakeError(GameplayErrors::InvalidGameModuleDescriptor, "Gameplay module descriptor export returned null."));
            if (bundle == nullptr)
                return Result<ValidatedModuleExports>::Failure(
                    MakeError(GameplayErrors::InvalidGeneratedDescriptorBundle, "Generated gameplay descriptor export returned null."));
            if (const Result<void> valid = ValidateGameModuleDescriptor(*descriptor, expectation); valid.HasError())
                return Result<ValidatedModuleExports>::Failure(valid.ErrorValue());
            if (const Result<void> valid = ValidateGeneratedGameplayDescriptorBundle(*bundle, expectation); valid.HasError())
                return Result<ValidatedModuleExports>::Failure(valid.ErrorValue());
            return Result<ValidatedModuleExports>::Success({descriptor, bundle});
        }

        [[nodiscard]] Result<std::unique_ptr<BehaviorRegistry>> BuildRegistry(const GeneratedGameplayDescriptorBundle &bundle) {
            auto registry = std::make_unique<BehaviorRegistry>();
            for (std::size_t index = 0; index < bundle.behaviorCount; ++index) {
                BehaviorRegistration registration{
                    .descriptor = bundle.behaviors[index],
                    .factory = bundle.nativeFactoryBindings[index].factory,
                };
                if (Result<void> registered = registry->Register(std::move(registration)); registered.HasError())
                    return Result<std::unique_ptr<BehaviorRegistry>>::Failure(registered.ErrorValue());
            }
            if (Result<void> frozen = registry->Freeze(); frozen.HasError())
                return Result<std::unique_ptr<BehaviorRegistry>>::Failure(frozen.ErrorValue());
            return Result<std::unique_ptr<BehaviorRegistry>>::Success(std::move(registry));
        }
    }  // namespace

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
                                                                   const GameModuleLoadExpectation &expectation) const {
        if (!libraryPath.is_absolute())
            return Result<std::unique_ptr<LoadedGameModule>>::Failure(
                MakeError(GameplayErrors::InvalidGameModuleDescriptor, "Gameplay module path must be absolute."));

        auto loaded = Platform::LoadDynamicLibrary(libraryPath.string());
        if (loaded.HasError())
            return Result<std::unique_ptr<LoadedGameModule>>::Failure(loaded.ErrorValue());
        auto impl = std::make_unique<LoadedGameModule::Impl>();
        impl->library = std::move(loaded).Value();
        impl->loadedArtifactPath = libraryPath;

        auto exports = ReadValidatedExports(*impl->library, expectation);
        if (exports.HasError())
            return Result<std::unique_ptr<LoadedGameModule>>::Failure(exports.ErrorValue());
        const ValidatedModuleExports validated = exports.Value();
        impl->moduleId = validated.descriptor->moduleId;
        impl->buildFingerprint = validated.descriptor->buildFingerprint;
        impl->descriptorRevision = validated.bundle->descriptorRevision;
        impl->destroy = validated.bundle->lifecycle.destroy;

        auto registry = BuildRegistry(*validated.bundle);
        if (registry.HasError())
            return Result<std::unique_ptr<LoadedGameModule>>::Failure(registry.ErrorValue());
        impl->registry = std::move(registry).Value();

        impl->gameplayModule = validated.bundle->lifecycle.create();
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
        return Result<std::unique_ptr<LoadedGameModule>>::Success(
            std::unique_ptr<LoadedGameModule>{new LoadedGameModule{std::move(impl)}});  // NOSONAR(cpp:S5950)
    }

    /** @copydoc GameModuleHost::LoadShadowCopy */
    [[nodiscard]] bool IsSafeAbsolutePath(const std::filesystem::path &path) noexcept {
        return !path.empty() && path.is_absolute() && std::ranges::none_of(path, [](const std::filesystem::path &part) {
            return part == "..";
        });
    }

    namespace {
        [[nodiscard]] Result<std::filesystem::path> PrepareShadowRoot(const std::filesystem::path &shadowRoot) {
            std::error_code error;
            std::filesystem::create_directories(shadowRoot, error);  // NOSONAR
            if (error)
                return Result<std::filesystem::path>::Failure(ShadowCopyError(error.message()));
            const std::filesystem::path canonicalRoot = std::filesystem::weakly_canonical(shadowRoot, error);
            if (error || !IsSafeAbsolutePath(canonicalRoot))
                return Result<std::filesystem::path>::Failure(ShadowCopyError("shadow root validation failed"));
            return Result<std::filesystem::path>::Success(canonicalRoot);
        }

        [[nodiscard]] Result<std::filesystem::path> MakeShadowPath(const std::filesystem::path &libraryPath,
                                                                   const std::filesystem::path &canonicalRoot) {
            static std::atomic<std::uint64_t> sequence{1};
            const auto timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
            const std::filesystem::path shadowPath =
                canonicalRoot / std::format("{}.horo_reload_{}_{}{}", libraryPath.stem().string(), timestamp, sequence.fetch_add(1),
                                            libraryPath.extension().string());
            std::error_code error;
            const std::filesystem::path canonicalShadow = std::filesystem::weakly_canonical(shadowPath, error);
            if (const std::filesystem::path relativeShadow = canonicalShadow.lexically_relative(canonicalRoot);
                error || relativeShadow.empty() || relativeShadow.is_absolute() || *relativeShadow.begin() == "..")
                return Result<std::filesystem::path>::Failure(ShadowCopyError("shadow path validation failed"));
            return Result<std::filesystem::path>::Success(canonicalShadow);
        }

        [[nodiscard]] Result<void> CopyShadowArtifact(const std::filesystem::path &source, const std::filesystem::path &destination) {
            std::error_code error;
            if (std::filesystem::copy_file(source, destination, std::filesystem::copy_options::none, error))  // NOSONAR
                return Result<void>::Success();
            return Result<void>::Failure(ShadowCopyError(error ? error.message() : "candidate could not be copied."));
        }
    }  // namespace

    Result<std::unique_ptr<LoadedGameModule>> GameModuleHost::LoadShadowCopy(const std::filesystem::path &libraryPath,
                                                                             const std::filesystem::path &shadowRoot,
                                                                             const GameModuleLoadExpectation &expectation) const {
        if (!IsSafeAbsolutePath(libraryPath) || !IsSafeAbsolutePath(shadowRoot))
            return Result<std::unique_ptr<LoadedGameModule>>::Failure(
                ShadowCopyError("source and destination paths must be absolute and without traversal."));

        auto canonicalRoot = PrepareShadowRoot(shadowRoot);
        if (canonicalRoot.HasError())
            return Result<std::unique_ptr<LoadedGameModule>>::Failure(canonicalRoot.ErrorValue());
        auto shadowPath = MakeShadowPath(libraryPath, canonicalRoot.Value());
        if (shadowPath.HasError())
            return Result<std::unique_ptr<LoadedGameModule>>::Failure(shadowPath.ErrorValue());
        if (const Result<void> copied = CopyShadowArtifact(libraryPath, shadowPath.Value()); copied.HasError())
            return Result<std::unique_ptr<LoadedGameModule>>::Failure(copied.ErrorValue());

        Result<std::unique_ptr<LoadedGameModule>> loaded = Load(shadowPath.Value(), expectation);
        if (loaded.HasError()) {
            std::error_code ignored;
            std::filesystem::remove(shadowPath.Value(), ignored);  // NOSONAR
            return loaded;
        }
        loaded.Value()->impl_->removeArtifactOnUnload = true;
        return loaded;
    }
}  // namespace Horo::Gameplay
