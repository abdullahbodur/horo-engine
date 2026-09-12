#include "Horo/Extensions/ToolchainProviderRegistry.h"

#include "../../ExtensionAuthorityIdentityValidation.h"
#include "Horo/Extensions/ExtensionErrors.h"
#include "Horo/Foundation/String.h"

#include <algorithm>
#include <atomic>
#include <format>
#include <mutex>
#include <optional>
#include <ranges>
#include <string_view>
#include <utility>

namespace Horo::Extensions {
    struct ToolchainProviderState final {
        ToolchainProviderDescriptor descriptor;
        std::vector<std::shared_ptr<CancellationSource>> activeInvocations;
        std::atomic_bool registered{true};
    };

    struct ToolchainProviderRegistryState final {
        // The mutex protects provider publication, active invocation admission,
        // revocation, and shutdown. Policy and process calls occur after release.
        std::mutex mutex;
        std::vector<std::shared_ptr<ToolchainProviderState>> providers;
        const IToolchainInvocationPolicy *policy{};
        IExternalProcessRunner *processes{};
        bool shutdown{};
    };

    namespace {
        [[nodiscard]] bool ValidDescriptor(const ToolchainProviderDescriptor &descriptor) noexcept {
            if (!Detail::IsCanonicalExtensionAuthorityId(descriptor.contributionId) ||
                !Detail::IsCanonicalExtensionAuthorityId(descriptor.providerId) || descriptor.providerGeneration == 0U ||
                descriptor.tools.empty() || descriptor.tools.size() > ToolchainProviderRegistry::MaximumToolsPerProvider)
                return false;
            std::string_view previous;
            for (const ToolchainToolId &tool : descriptor.tools) {
                if (!Detail::IsCanonicalExtensionAuthorityId(tool.value) || (!previous.empty() && tool.value <= previous))
                    return false;
                previous = tool.value;
            }
            return true;
        }

        [[nodiscard]] bool ValidIntent(const ToolchainInvocationIntent &intent) noexcept {
            if (!Detail::IsCanonicalExtensionAuthorityId(intent.tool.value) ||
                intent.arguments.size() > ToolchainProviderRegistry::MaximumArguments)
                return false;
            std::size_t bytes = 0U;
            for (const std::string &argument : intent.arguments) {
                if (argument.find('\0') != std::string::npos || argument.size() > ToolchainProviderRegistry::MaximumArgumentBytes - bytes)
                    return false;
                bytes += argument.size();
            }
            return true;
        }

        [[nodiscard]] bool ValidProcessRequest(const ExternalProcessRequest &request) noexcept {
            if (Text::IsBlank(request.executable) || request.executable.find('\0') != std::string::npos || request.timeout.count() <= 0 ||
                request.gracefulTermination.count() < 0 || request.maximumLineBytes == 0U)
                return false;
            return std::ranges::none_of(request.arguments, [](const std::string &argument) {
                return argument.find('\0') != std::string::npos;
            });
        }

        [[nodiscard]] Result<ExternalProcessRequest> ResolveAndValidateRequest(const IToolchainInvocationPolicy &policy,
                                                                               const ToolchainProviderDescriptor &provider,
                                                                               const ToolchainInvocationIntent &intent) {
            Result<ExternalProcessRequest> resolved = [&]() {
                try {
                    return policy.Resolve(provider, intent);
                } catch (...) {  // NOSONAR(cpp:S1181) Host policy boundary must contain exceptions.
                    return Result<ExternalProcessRequest>::Failure(
                        MakeError(ExtensionErrors::ToolchainPolicyRejected, "Host toolchain policy threw an exception."));
                }
            }();
            if (resolved.HasError())
                return Result<ExternalProcessRequest>::Failure(
                    WrapError(ExtensionErrors::ToolchainPolicyRejected, resolved.ErrorValue(),
                              std::format("Host policy rejected tool '{}' for {}@{}.", intent.tool.value, provider.providerId,
                                          provider.providerGeneration)));
            if (!ValidProcessRequest(resolved.Value()))
                return Result<ExternalProcessRequest>::Failure(MakeError(ExtensionErrors::ToolchainProviderRegistryInvalid));
            return resolved;
        }

        [[nodiscard]] Result<ExternalProcessResult> RunProcess(IExternalProcessRunner &processes, const ExternalProcessRequest &request,
                                                               const CancellationToken &cancellation) {
            try {
                return processes.Run(request, cancellation);
            } catch (...) {  // NOSONAR(cpp:S1181) Platform adapter boundary must contain exceptions.
                return Result<ExternalProcessResult>::Failure(
                    MakeError(ExtensionErrors::ToolchainInvocationFailed, "Platform process runner threw an exception."));
            }
        }

        [[nodiscard]] Error ProviderUnavailable(const ToolchainInvocationAuthority &authority) {
            return MakeError(ExtensionErrors::ToolchainProviderUnavailable,
                             std::format("Toolchain provider is unavailable: {}@{}.", authority.contributionId,
                                         authority.providerGeneration));
        }

        [[nodiscard]] Result<std::shared_ptr<ToolchainProviderState>> FindProvider(
            const std::shared_ptr<ToolchainProviderRegistryState> &state, const ToolchainInvocationAuthority &authority) {
            std::scoped_lock lock{state->mutex};
            if (state->shutdown)
                return Result<std::shared_ptr<ToolchainProviderState>>::Failure(
                    MakeError(ExtensionErrors::ToolchainProviderRegistryShutdown));
            const auto found = std::ranges::lower_bound(state->providers, authority.contributionId, {}, [](const auto &candidate) {
                return candidate->descriptor.contributionId;
            });
            if (found == state->providers.end() || (*found)->descriptor.contributionId != authority.contributionId ||
                (*found)->descriptor.providerGeneration != authority.providerGeneration)
                return Result<std::shared_ptr<ToolchainProviderState>>::Failure(ProviderUnavailable(authority));
            return Result<std::shared_ptr<ToolchainProviderState>>::Success(*found);
        }

        void CancelInvocations(const std::shared_ptr<ToolchainProviderState> &provider) noexcept {
            for (const auto &invocation : provider->activeInvocations)
                invocation->RequestCancellation();
        }

        void RemoveProvider(const std::shared_ptr<ToolchainProviderRegistryState> &registry,
                            const std::shared_ptr<ToolchainProviderState> &provider) {
            std::scoped_lock lock{registry->mutex};
            provider->registered.store(false, std::memory_order_release);
            CancelInvocations(provider);
            std::erase(registry->providers, provider);
        }

        void RemoveInvocation(const std::shared_ptr<ToolchainProviderRegistryState> &registry,
                              const std::shared_ptr<ToolchainProviderState> &provider,
                              const std::shared_ptr<CancellationSource> &invocation) noexcept {
            try {
                std::scoped_lock lock{registry->mutex};
                std::erase(provider->activeInvocations, invocation);
            } catch (...) {
                invocation->RequestCancellation();
            }
        }

        [[nodiscard]] Result<std::shared_ptr<CancellationSource>> AdmitInvocation(
            const std::shared_ptr<ToolchainProviderRegistryState> &state, const std::shared_ptr<ToolchainProviderState> &provider,
            const ToolchainInvocationAuthority &authority, const CancellationToken &cancellation) {
            auto invocation = std::make_shared<CancellationSource>(cancellation);
            std::scoped_lock lock{state->mutex};
            if (state->shutdown)
                return Result<std::shared_ptr<CancellationSource>>::Failure(MakeError(ExtensionErrors::ToolchainProviderRegistryShutdown));
            if (!provider->registered.load(std::memory_order_acquire))
                return Result<std::shared_ptr<CancellationSource>>::Failure(ProviderUnavailable(authority));
            if (provider->activeInvocations.size() >= ToolchainProviderRegistry::MaximumActiveInvocationsPerProvider)
                return Result<std::shared_ptr<CancellationSource>>::Failure(
                    MakeError(ExtensionErrors::ToolchainProviderRegistryCapacityExceeded));
            provider->activeInvocations.push_back(invocation);
            return Result<std::shared_ptr<CancellationSource>>::Success(std::move(invocation));
        }
    }  // namespace

    ToolchainProviderRegistration::ToolchainProviderRegistration(std::weak_ptr<ToolchainProviderRegistryState> registry,
                                                                 std::shared_ptr<ToolchainProviderState> provider) noexcept
        : registry_(std::move(registry)), provider_(std::move(provider)) {}

    ToolchainProviderRegistration::~ToolchainProviderRegistration() noexcept {
        try {
            Reset();
        } catch (...) {
            if (provider_ != nullptr) {
                provider_->registered.store(false, std::memory_order_release);
                CancelInvocations(provider_);
            }
        }
    }

    ToolchainProviderRegistration::ToolchainProviderRegistration(ToolchainProviderRegistration &&other) noexcept
        : registry_(std::move(other.registry_)), provider_(std::move(other.provider_)) {}

    ToolchainProviderRegistration &ToolchainProviderRegistration::operator=(ToolchainProviderRegistration &&other) {
        if (this == &other)
            return *this;
        Reset();
        registry_ = std::move(other.registry_);
        provider_ = std::move(other.provider_);
        return *this;
    }

    /** @copydoc ToolchainProviderRegistration::Reset */
    void ToolchainProviderRegistration::Reset() {
        if (provider_ == nullptr)
            return;
        if (auto registry = registry_.lock())
            RemoveProvider(registry, provider_);
        else {
            provider_->registered.store(false, std::memory_order_release);
            CancelInvocations(provider_);
        }
        provider_.reset();
        registry_.reset();
    }

    /** @copydoc ToolchainProviderRegistration::IsRegistered */
    bool ToolchainProviderRegistration::IsRegistered() const noexcept {
        return provider_ != nullptr && provider_->registered.load(std::memory_order_acquire);
    }

    /** @copydoc ToolchainProviderRegistration::Authority */
    ToolchainInvocationAuthority ToolchainProviderRegistration::Authority() const {
        if (provider_ == nullptr)
            return {};
        return {provider_->descriptor.contributionId, provider_->descriptor.providerGeneration};
    }

    ToolchainProviderRegistry::ToolchainProviderRegistry(const IToolchainInvocationPolicy &policy, IExternalProcessRunner &processes)
        : state_(std::make_shared<ToolchainProviderRegistryState>()) {
        state_->providers.reserve(MaximumProviders);
        state_->policy = &policy;
        state_->processes = &processes;
    }

    ToolchainProviderRegistry::~ToolchainProviderRegistry() noexcept {
        try {
            BeginShutdown();
        } catch (...) {
            state_.reset();
        }
    }

    ToolchainProviderRegistry &ToolchainProviderRegistry::operator=(ToolchainProviderRegistry &&other) {
        if (this == &other)
            return *this;
        BeginShutdown();
        state_ = std::move(other.state_);
        return *this;
    }

    /** @copydoc ToolchainProviderRegistry::Register */
    Result<ToolchainProviderRegistration> ToolchainProviderRegistry::Register(  // NOSONAR(cpp:S5817) Publication mutates owner state.
        ToolchainProviderDescriptor descriptor) {
        if (!ValidDescriptor(descriptor))
            return Result<ToolchainProviderRegistration>::Failure(MakeError(ExtensionErrors::ToolchainProviderRegistryInvalid));
        if (state_ == nullptr)
            return Result<ToolchainProviderRegistration>::Failure(MakeError(ExtensionErrors::ToolchainProviderRegistryShutdown));
        std::scoped_lock lock{state_->mutex};
        if (state_->shutdown)
            return Result<ToolchainProviderRegistration>::Failure(MakeError(ExtensionErrors::ToolchainProviderRegistryShutdown));
        const auto insertion = std::ranges::lower_bound(state_->providers, descriptor.contributionId, {}, [](const auto &provider) {
            return provider->descriptor.contributionId;
        });
        if (insertion != state_->providers.end() && (*insertion)->descriptor.contributionId == descriptor.contributionId)
            return Result<ToolchainProviderRegistration>::Failure(MakeError(ExtensionErrors::ToolchainProviderRegistryDuplicate));
        if (state_->providers.size() >= MaximumProviders)
            return Result<ToolchainProviderRegistration>::Failure(MakeError(ExtensionErrors::ToolchainProviderRegistryCapacityExceeded));
        auto provider = std::make_shared<ToolchainProviderState>();
        provider->descriptor = std::move(descriptor);
        state_->providers.insert(insertion, provider);
        return Result<ToolchainProviderRegistration>::Success(ToolchainProviderRegistration{state_, std::move(provider)});
    }

    /** @copydoc ToolchainProviderRegistry::Invoke */
    Result<ToolchainInvocationResult> ToolchainProviderRegistry::Invoke(const ToolchainInvocationAuthority &authority,
                                                                        const ToolchainInvocationIntent &intent,
                                                                        const CancellationToken &cancellation) const {
        if (!ValidIntent(intent) || !Detail::IsCanonicalExtensionAuthorityId(authority.contributionId) ||
            authority.providerGeneration == 0U)
            return Result<ToolchainInvocationResult>::Failure(MakeError(ExtensionErrors::ToolchainProviderRegistryInvalid));
        const auto state = state_;
        if (state == nullptr)
            return Result<ToolchainInvocationResult>::Failure(MakeError(ExtensionErrors::ToolchainProviderRegistryShutdown));

        auto providerResult = FindProvider(state, authority);
        if (providerResult.HasError())
            return Result<ToolchainInvocationResult>::Failure(providerResult.ErrorValue());
        const auto provider = std::move(providerResult).Value();
        if (!std::ranges::binary_search(provider->descriptor.tools, intent.tool.value, {}, &ToolchainToolId::value))
            return Result<ToolchainInvocationResult>::Failure(MakeError(ExtensionErrors::ToolchainProviderRegistryInvalid));

        Result<ExternalProcessRequest> resolved = ResolveAndValidateRequest(*state->policy, provider->descriptor, intent);
        if (resolved.HasError())
            return Result<ToolchainInvocationResult>::Failure(resolved.ErrorValue());

        auto admitted = AdmitInvocation(state, provider, authority, cancellation);
        if (admitted.HasError())
            return Result<ToolchainInvocationResult>::Failure(admitted.ErrorValue());
        const auto invocation = std::move(admitted).Value();

        Result<ExternalProcessResult> process = RunProcess(*state->processes, resolved.Value(), invocation->Token());
        RemoveInvocation(state, provider, invocation);
        if (process.HasError() && process.ErrorValue().code.Value() == ExtensionErrors::ToolchainInvocationFailed.code.Value())
            return Result<ToolchainInvocationResult>::Failure(process.ErrorValue());
        if (process.HasError())
            return Result<ToolchainInvocationResult>::Failure(
                WrapError(ExtensionErrors::ToolchainInvocationFailed, process.ErrorValue(),
                          std::format("Tool '{}' failed for {}@{}.", intent.tool.value, provider->descriptor.providerId,
                                      provider->descriptor.providerGeneration)));
        return Result<ToolchainInvocationResult>::Success({{provider->descriptor.contributionId, provider->descriptor.providerGeneration},
                                                           provider->descriptor.providerId,
                                                           intent.tool,
                                                           std::move(process).Value()});
    }

    /** @copydoc ToolchainProviderRegistry::BeginShutdown */
    void ToolchainProviderRegistry::BeginShutdown() {  // NOSONAR(cpp:S5817) Terminal admission mutation belongs to the owner facade.
        if (state_ == nullptr)
            return;
        std::scoped_lock lock{state_->mutex};
        state_->shutdown = true;
        for (const auto &provider : state_->providers) {
            provider->registered.store(false, std::memory_order_release);
            CancelInvocations(provider);
        }
        state_->providers.clear();
    }

    /** @copydoc ToolchainProviderRegistry::IsShutdown */
    bool ToolchainProviderRegistry::IsShutdown() const {
        if (state_ == nullptr)
            return true;
        std::scoped_lock lock{state_->mutex};
        return state_->shutdown;
    }
}  // namespace Horo::Extensions
