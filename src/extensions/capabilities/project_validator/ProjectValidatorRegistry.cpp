#include "Horo/Extensions/ProjectValidatorRegistry.h"

#include "../../ExtensionAuthorityIdentityValidation.h"
#include "Horo/Extensions/ExtensionErrors.h"
#include "Horo/Foundation/String.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <mutex>
#include <optional>
#include <ranges>
#include <tuple>
#include <utility>

namespace Horo::Extensions {
    struct ProjectValidatorProviderState final {
        ProjectValidatorProviderDescriptor descriptor;
        std::shared_ptr<const IProjectValidator> provider;
        std::atomic_bool registered{true};
    };

    struct ProjectValidatorRegistryState final {
        // The mutex protects publication ordering, removal, and shutdown admission.
        // ValidateAll copies strong provider leases while holding it, then invokes
        // callbacks only after release. Any thread may register, validate, reset, or
        // begin shutdown; admitted synchronous callbacks drain on their copied leases.
        std::mutex mutex;
        std::vector<std::shared_ptr<ProjectValidatorProviderState>> providers;
        ErrorCodeRegistry errors;
        ValidationResultLimits findingLimits;
        bool shutdown{};
    };

    namespace {
        [[nodiscard]] bool ValidMode(const ProjectValidationMode mode) noexcept {
            return mode >= ProjectValidationMode::ReadOnly && mode <= ProjectValidationMode::Build;
        }

        [[nodiscard]] bool ValidDescriptor(const ProjectValidatorProviderDescriptor &descriptor) noexcept {
            return Detail::IsCanonicalExtensionAuthorityId(descriptor.validatorId.value) &&
                   Detail::IsCanonicalExtensionAuthorityId(descriptor.providerId.value) && descriptor.providerGeneration != 0U;
        }

        [[nodiscard]] Result<void> ValidateSnapshot(const ProjectValidationSnapshot &snapshot) {
            if (!ValidMode(snapshot.mode) || Text::IsBlank(snapshot.projectId) ||
                snapshot.projectId.size() > ProjectValidatorRegistry::MaximumProjectIdBytes ||
                snapshot.resources.size() > ProjectValidatorRegistry::MaximumResources)
                return Result<void>::Failure(MakeError(ExtensionErrors::ProjectValidatorRegistryInvalid));

            std::uint64_t totalBytes = 0U;
            std::string_view previous;
            for (const ProjectValidationResourceView &resource : snapshot.resources) {
                const std::string_view path = resource.path.String();
                if (path.empty() || (!previous.empty() && path <= previous) ||
                    resource.bytes.size() > ProjectValidatorRegistry::MaximumInputBytes - totalBytes)
                    return Result<void>::Failure(MakeError(ExtensionErrors::ProjectValidatorRegistryInvalid));
                previous = path;
                totalBytes += resource.bytes.size();
            }
            return Result<void>::Success();
        }

        [[nodiscard]] bool IsRootedSource(const std::string_view source) noexcept {
            if (source.starts_with('/') || source.starts_with('\\'))
                return true;
            return source.size() >= 2U && std::isalpha(static_cast<unsigned char>(source.front())) != 0 && source[1] == ':';
        }

        [[nodiscard]] Error ProviderFailure(const ProjectValidatorProviderDescriptor &provider, Error cause = {}) {
            const std::string detail = "Project validator failed: " + provider.providerId.value + "@" +
                                       std::to_string(provider.providerGeneration) + " (" + provider.validatorId.value + ").";
            return cause.code.Value().empty() ? MakeError(ExtensionErrors::ProjectValidatorInvocationFailed, detail)
                                              : WrapError(ExtensionErrors::ProjectValidatorInvocationFailed, std::move(cause), detail);
        }

        [[nodiscard]] Error CancellationFailure(const ProjectValidatorProviderDescriptor *provider) {
            if (provider == nullptr)
                return MakeError(ExtensionErrors::ProjectValidationCancelled);
            return MakeError(ExtensionErrors::ProjectValidationCancelled,
                             "Project validation cancelled at provider: " + provider->providerId.value + "@" +
                                 std::to_string(provider->providerGeneration) + ".");
        }

        void RemoveProvider(const std::shared_ptr<ProjectValidatorRegistryState> &registry,
                            const std::shared_ptr<ProjectValidatorProviderState> &provider) {
            std::scoped_lock lock{registry->mutex};
            provider->registered.store(false, std::memory_order_release);
            std::erase(registry->providers, provider);
        }

        [[nodiscard]] std::optional<std::vector<std::shared_ptr<ProjectValidatorProviderState>>> SnapshotProviders(
            const std::shared_ptr<ProjectValidatorRegistryState> &state) {
            std::scoped_lock lock{state->mutex};
            if (state->shutdown)
                return std::nullopt;
            return state->providers;
        }
    }  // namespace

    ProjectValidationFindingSink::ProjectValidationFindingSink(ValidationResultBuilder builder) noexcept : builder_(std::move(builder)) {}

    /** @copydoc ProjectValidationFindingSink::Add */
    Result<void> ProjectValidationFindingSink::Add(const ErrorCodeDescriptor &descriptor, std::string message, SourceLocation location) {
        if (IsRootedSource(location.source))
            return builder_.Add(descriptor, std::move(message), {});
        auto normalized = ProjectPath::Parse(location.source);
        if (normalized.HasError() || normalized.Value().String().empty())
            return builder_.Add(descriptor, std::move(message), {});
        location.source = normalized.Value().String();
        return builder_.Add(descriptor, std::move(message), std::move(location));
    }

    Result<ValidationResult> ProjectValidationFindingSink::Complete() {
        return builder_.Complete();
    }

    ProjectValidatorRegistration::ProjectValidatorRegistration(std::weak_ptr<ProjectValidatorRegistryState> registry,
                                                               std::shared_ptr<ProjectValidatorProviderState> provider) noexcept
        : registry_(std::move(registry)), provider_(std::move(provider)) {}

    ProjectValidatorRegistration::~ProjectValidatorRegistration() noexcept {
        try {
            Reset();
        } catch (...) {  // A destructor cannot surface a platform mutex failure.
            if (provider_ != nullptr)
                provider_->registered.store(false, std::memory_order_release);
        }
    }

    ProjectValidatorRegistration::ProjectValidatorRegistration(ProjectValidatorRegistration &&other) noexcept
        : registry_(std::move(other.registry_)), provider_(std::move(other.provider_)) {}

    ProjectValidatorRegistration &ProjectValidatorRegistration::operator=(ProjectValidatorRegistration &&other) {
        if (this == &other)
            return *this;
        Reset();
        registry_ = std::move(other.registry_);
        provider_ = std::move(other.provider_);
        return *this;
    }

    /** @copydoc ProjectValidatorRegistration::Reset */
    void ProjectValidatorRegistration::Reset() {
        if (provider_ == nullptr)
            return;
        if (auto registry = registry_.lock())
            RemoveProvider(registry, provider_);
        else
            provider_->registered.store(false, std::memory_order_release);
        provider_.reset();
        registry_.reset();
    }

    /** @copydoc ProjectValidatorRegistration::IsRegistered */
    bool ProjectValidatorRegistration::IsRegistered() const noexcept {
        return provider_ != nullptr && provider_->registered.load(std::memory_order_acquire);
    }

    ProjectValidatorRegistry::ProjectValidatorRegistry(ErrorCodeRegistry errors, const ValidationResultLimits findingLimits)
        : state_(std::make_shared<ProjectValidatorRegistryState>()) {
        state_->errors = std::move(errors);
        state_->findingLimits = findingLimits;
        state_->providers.reserve(MaximumProviders);
    }

    /** @copydoc ProjectValidatorRegistry::Create */
    Result<ProjectValidatorRegistry> ProjectValidatorRegistry::Create(ErrorCodeRegistry errors,
                                                                      const ValidationResultLimits findingLimits) {
        if (findingLimits.maximumDiagnostics == 0U || findingLimits.maximumDiagnostics > ValidationResultLimits::HardMaximumDiagnostics)
            return Result<ProjectValidatorRegistry>::Failure(
                MakeError(ExtensionErrors::ProjectValidatorRegistryInvalid, "Project-validator finding limits are invalid."));
        return Result<ProjectValidatorRegistry>::Success(ProjectValidatorRegistry{std::move(errors), findingLimits});
    }

    ProjectValidatorRegistry::~ProjectValidatorRegistry() noexcept {
        try {
            BeginShutdown();
        } catch (...) {  // A destructor cannot surface a platform mutex failure.
            state_.reset();
        }
    }

    ProjectValidatorRegistry &ProjectValidatorRegistry::operator=(ProjectValidatorRegistry &&other) {
        if (this == &other)
            return *this;
        BeginShutdown();
        state_ = std::move(other.state_);
        return *this;
    }

    /** @copydoc ProjectValidatorRegistry::Register */
    Result<ProjectValidatorRegistration> ProjectValidatorRegistry::Register(ProjectValidatorProviderDescriptor descriptor,
                                                                            std::shared_ptr<const IProjectValidator> provider) {
        if (!ValidDescriptor(descriptor) || provider == nullptr)
            return Result<ProjectValidatorRegistration>::Failure(MakeError(ExtensionErrors::ProjectValidatorRegistryInvalid));
        if (state_ == nullptr)
            return Result<ProjectValidatorRegistration>::Failure(MakeError(ExtensionErrors::ProjectValidatorRegistryShutdown));

        std::scoped_lock lock{state_->mutex};
        if (state_->shutdown)
            return Result<ProjectValidatorRegistration>::Failure(MakeError(ExtensionErrors::ProjectValidatorRegistryShutdown));
        const auto insertion = std::ranges::lower_bound(state_->providers, descriptor.validatorId.value, {}, [](const auto &candidate) {
            return candidate->descriptor.validatorId.value;
        });
        if (insertion != state_->providers.end() && (*insertion)->descriptor.validatorId == descriptor.validatorId)
            return Result<ProjectValidatorRegistration>::Failure(MakeError(ExtensionErrors::ProjectValidatorRegistryDuplicate));
        if (state_->providers.size() >= MaximumProviders)
            return Result<ProjectValidatorRegistration>::Failure(MakeError(ExtensionErrors::ProjectValidatorRegistryCapacityExceeded));

        auto published = std::make_shared<ProjectValidatorProviderState>();
        published->descriptor = std::move(descriptor);
        published->provider = std::move(provider);
        state_->providers.insert(insertion, published);
        return Result<ProjectValidatorRegistration>::Success(ProjectValidatorRegistration{state_, std::move(published)});
    }

    /** @copydoc ProjectValidatorRegistry::ValidateAll */
    Result<std::vector<AttributedProjectValidationResult>> ProjectValidatorRegistry::ValidateAll(
        const ProjectValidationSnapshot &snapshot, const CancellationToken &cancellation) const {
        const auto state = state_;
        const auto validSnapshot = ValidateSnapshot(snapshot);
        if (validSnapshot.HasError())
            return Result<std::vector<AttributedProjectValidationResult>>::Failure(validSnapshot.ErrorValue());
        if (state == nullptr)
            return Result<std::vector<AttributedProjectValidationResult>>::Failure(
                MakeError(ExtensionErrors::ProjectValidatorRegistryShutdown));
        auto providers = SnapshotProviders(state);
        if (!providers)
            return Result<std::vector<AttributedProjectValidationResult>>::Failure(
                MakeError(ExtensionErrors::ProjectValidatorRegistryShutdown));

        std::vector<AttributedProjectValidationResult> results;
        results.reserve(providers->size());
        for (const auto &provider : *providers) {
            if (cancellation.IsCancellationRequested())
                return Result<std::vector<AttributedProjectValidationResult>>::Failure(CancellationFailure(&provider->descriptor));
            auto builder = ValidationResultBuilder::Create(state->errors, state->findingLimits);
            if (builder.HasError())
                return Result<std::vector<AttributedProjectValidationResult>>::Failure(
                    ProviderFailure(provider->descriptor, builder.ErrorValue()));
            ProjectValidationFindingSink findings{std::move(builder).Value()};
            try {
                auto validated = provider->provider->Validate(snapshot, findings, cancellation);
                if (validated.HasError())
                    return Result<std::vector<AttributedProjectValidationResult>>::Failure(
                        ProviderFailure(provider->descriptor, validated.ErrorValue()));
            } catch (...) {  // NOSONAR(cpp:S1181) Trusted extension callback exception boundary.
                return Result<std::vector<AttributedProjectValidationResult>>::Failure(ProviderFailure(provider->descriptor));
            }
            if (cancellation.IsCancellationRequested())
                return Result<std::vector<AttributedProjectValidationResult>>::Failure(CancellationFailure(&provider->descriptor));
            auto completed = findings.Complete();
            if (completed.HasError())
                return Result<std::vector<AttributedProjectValidationResult>>::Failure(
                    ProviderFailure(provider->descriptor, completed.ErrorValue()));
            results.push_back({provider->descriptor, std::move(completed).Value()});
        }
        if (cancellation.IsCancellationRequested())
            return Result<std::vector<AttributedProjectValidationResult>>::Failure(CancellationFailure(nullptr));
        return Result<std::vector<AttributedProjectValidationResult>>::Success(std::move(results));
    }

    /** @copydoc ProjectValidatorRegistry::BeginShutdown */
    void ProjectValidatorRegistry::BeginShutdown() {
        if (state_ == nullptr)
            return;
        std::scoped_lock lock{state_->mutex};
        state_->shutdown = true;
        for (const auto &provider : state_->providers)
            provider->registered.store(false, std::memory_order_release);
        state_->providers.clear();
    }

    /** @copydoc ProjectValidatorRegistry::IsShutdown */
    bool ProjectValidatorRegistry::IsShutdown() const {
        if (state_ == nullptr)
            return true;
        std::scoped_lock lock{state_->mutex};
        return state_->shutdown;
    }
}  // namespace Horo::Extensions
