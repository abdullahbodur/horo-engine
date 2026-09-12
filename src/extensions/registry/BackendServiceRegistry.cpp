#include "Horo/Extensions/BackendServiceRegistry.h"

#include "../ExtensionAuthorityIdentityValidation.h"
#include "Horo/Extensions/ExtensionErrors.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <condition_variable>
#include <format>
#include <mutex>
#include <ranges>
#include <thread>
#include <utility>
#include <vector>

namespace Horo::Extensions {
    enum class BackendServiceProviderLifecycle : std::uint8_t {
        Active,
        Retiring,
        AwaitingOwnerFinalization,
        RetainedRestartRequired,
        Shutdown,
    };

    struct BackendServiceProviderState final {
        BackendServiceDescriptor descriptor;
        std::shared_ptr<void> service;
        const void *typeTag{};
        void (*shutdown)(void *) noexcept {};
        std::thread::id ownerThread;
        CancellationSource cancellation;
        std::mutex mutex;
        std::condition_variable drained;
        std::size_t activeCalls{};
        std::atomic_bool registered{true};
        BackendServiceProviderLifecycle lifecycle{BackendServiceProviderLifecycle::Active};
    };

    struct BackendServiceRegistryState final {
        std::mutex mutex;
        std::vector<std::shared_ptr<BackendServiceProviderState>> providers;
        std::vector<std::shared_ptr<BackendServiceProviderState>> retiredProviders;
        BackendServiceRegistryConfig config;
        bool shutdown{};
    };

    namespace {
        struct ExecutingProviderStack final {
            std::array<BackendServiceProviderState *, BackendServiceRegistry::MaximumServices> providers{};
            std::size_t size{};
        };

        thread_local ExecutingProviderStack ExecutingProviders;

        struct ServiceShutdown final {
            std::shared_ptr<void> service;
            void (*callback)(void *) noexcept {};

            void Invoke() const noexcept {
                if (service != nullptr && callback != nullptr)
                    callback(service.get());
            }
        };

        [[nodiscard]] bool IsExecuting(const BackendServiceProviderState *provider) noexcept {
            const auto end = ExecutingProviders.providers.begin() + static_cast<std::ptrdiff_t>(ExecutingProviders.size);
            return std::ranges::find(ExecutingProviders.providers.begin(), end, provider) != end;
        }

        void MergeDisposition(BackendServiceRetirementDisposition &aggregate,
                              const BackendServiceRetirementDisposition candidate) noexcept {
            aggregate = std::max(aggregate, candidate);
        }

        [[nodiscard]] bool ValidThreadRule(const BackendServiceThreadRule rule) noexcept {
            return rule == BackendServiceThreadRule::AnyThread || rule == BackendServiceThreadRule::ProviderOwnerThread;
        }

        [[nodiscard]] bool ValidDescriptor(const BackendServiceDescriptor &descriptor) noexcept {
            return Detail::IsCanonicalExtensionAuthorityId(descriptor.serviceId.value) &&
                   Detail::IsCanonicalExtensionAuthorityId(descriptor.contractId.value) &&
                   Detail::IsCanonicalExtensionAuthorityId(descriptor.capability.value) &&
                   Detail::IsCanonicalExtensionAuthorityId(descriptor.providerId) && descriptor.version != ApplicationCapabilityVersion{} &&
                   descriptor.providerGeneration != 0U && ValidThreadRule(descriptor.threadRule);
        }

        [[nodiscard]] bool CanFinalizeHere(const BackendServiceProviderState &provider) noexcept {
            return provider.descriptor.threadRule == BackendServiceThreadRule::AnyThread ||
                   provider.ownerThread == std::this_thread::get_id();
        }

        [[nodiscard]] ServiceShutdown PrepareShutdown(BackendServiceProviderState &provider) noexcept {
            provider.lifecycle = BackendServiceProviderLifecycle::Shutdown;
            return {std::move(provider.service), provider.shutdown};
        }

        [[nodiscard]] BackendServiceRetirementDisposition StopProvider(const std::shared_ptr<BackendServiceProviderState> &provider,
                                                                       const std::chrono::steady_clock::time_point deadline) noexcept {
            std::unique_lock lock{provider->mutex};
            if (provider->lifecycle == BackendServiceProviderLifecycle::Shutdown)
                return BackendServiceRetirementDisposition::ShutdownComplete;
            if (provider->lifecycle == BackendServiceProviderLifecycle::RetainedRestartRequired)
                return BackendServiceRetirementDisposition::RestartRequired;

            provider->registered.store(false, std::memory_order_release);
            provider->cancellation.RequestCancellation();
            if (provider->lifecycle == BackendServiceProviderLifecycle::Active)
                provider->lifecycle = BackendServiceProviderLifecycle::Retiring;
            if (IsExecuting(provider.get()))
                return BackendServiceRetirementDisposition::DeferredUntilCallExit;
            if (!provider->drained.wait_until(lock, deadline, [&provider] {
                return provider->activeCalls == 0U;
            })) {
                provider->lifecycle = BackendServiceProviderLifecycle::RetainedRestartRequired;
                return BackendServiceRetirementDisposition::RestartRequired;
            }
            if (!CanFinalizeHere(*provider)) {
                provider->lifecycle = BackendServiceProviderLifecycle::AwaitingOwnerFinalization;
                return BackendServiceRetirementDisposition::OwnerThreadFinalizationRequired;
            }
            ServiceShutdown shutdown = PrepareShutdown(*provider);
            lock.unlock();
            shutdown.Invoke();
            return BackendServiceRetirementDisposition::ShutdownComplete;
        }

        [[nodiscard]] BackendServiceRetirementDisposition FinalizeRetiredProvider(
            const std::shared_ptr<BackendServiceProviderState> &provider) noexcept {
            std::unique_lock lock{provider->mutex};
            if (provider->lifecycle == BackendServiceProviderLifecycle::Shutdown)
                return BackendServiceRetirementDisposition::ShutdownComplete;
            if (provider->lifecycle == BackendServiceProviderLifecycle::RetainedRestartRequired)
                return BackendServiceRetirementDisposition::RestartRequired;
            if (provider->activeCalls != 0U)
                return BackendServiceRetirementDisposition::DeferredUntilCallExit;
            if (!CanFinalizeHere(*provider))
                return BackendServiceRetirementDisposition::OwnerThreadFinalizationRequired;
            ServiceShutdown shutdown = PrepareShutdown(*provider);
            lock.unlock();
            shutdown.Invoke();
            return BackendServiceRetirementDisposition::ShutdownComplete;
        }

        [[nodiscard]] BackendServiceRetirementDisposition RetireProvider(
            const std::shared_ptr<BackendServiceRegistryState> &registry,
            const std::shared_ptr<BackendServiceProviderState> &provider) noexcept {
            {
                std::scoped_lock lock{registry->mutex};
                std::erase(registry->providers, provider);
            }
            const auto disposition = StopProvider(provider, std::chrono::steady_clock::now() + registry->config.drainDeadline);
            if (disposition != BackendServiceRetirementDisposition::ShutdownComplete) {
                std::scoped_lock lock{registry->mutex};
                if (std::ranges::find(registry->retiredProviders, provider) == registry->retiredProviders.end())
                    registry->retiredProviders.push_back(provider);
            }
            return disposition;
        }

        [[nodiscard]] bool MatchesAuthority(const BackendServiceDescriptor &service,
                                            const ApplicationCapabilityProviderDescriptor &authority) noexcept {
            return service.capability == authority.capability && service.version == authority.version &&
                   service.providerId == authority.providerId && service.providerGeneration == authority.providerGeneration;
        }
    }  // namespace

    BackendServiceCallContext::BackendServiceCallContext(const BackendServiceDescriptor &provider, CancellationToken caller,
                                                         CancellationToken providerCancellation) noexcept
        : provider_(&provider), caller_(std::move(caller)), providerCancellation_(std::move(providerCancellation)) {}

    /** @copydoc BackendServiceCallContext::Provider */
    const BackendServiceDescriptor &BackendServiceCallContext::Provider() const noexcept {
        return *provider_;
    }

    /** @copydoc BackendServiceCallContext::IsCancellationRequested */
    bool BackendServiceCallContext::IsCancellationRequested() const noexcept {
        return caller_.IsCancellationRequested() || providerCancellation_.IsCancellationRequested();
    }

    BackendServiceCallAdmission::BackendServiceCallAdmission(std::shared_ptr<BackendServiceProviderState> provider,
                                                             BackendServiceCallContext context) noexcept
        : provider_(std::move(provider)), context_(std::move(context)), ownsExecutionSlot_(true) {
        ExecutingProviders.providers[ExecutingProviders.size++] = provider_.get();
    }

    BackendServiceCallAdmission::~BackendServiceCallAdmission() {
        Reset();
    }

    BackendServiceCallAdmission::BackendServiceCallAdmission(BackendServiceCallAdmission &&other) noexcept
        : provider_(std::move(other.provider_)), context_(std::move(other.context_)),
          ownsExecutionSlot_(std::exchange(other.ownsExecutionSlot_, false)) {}

    BackendServiceCallAdmission &BackendServiceCallAdmission::operator=(BackendServiceCallAdmission &&other) noexcept {
        if (this == &other)
            return *this;
        Reset();
        provider_ = std::move(other.provider_);
        context_ = std::move(other.context_);
        ownsExecutionSlot_ = std::exchange(other.ownsExecutionSlot_, false);
        return *this;
    }

    /** @copydoc BackendServiceCallAdmission::Context */
    const BackendServiceCallContext &BackendServiceCallAdmission::Context() const noexcept {
        return context_;
    }

    void BackendServiceCallAdmission::Reset() noexcept {
        if (provider_ == nullptr)
            return;
        ServiceShutdown shutdown;
        {
            std::scoped_lock lock{provider_->mutex};
            --provider_->activeCalls;
            if (provider_->activeCalls == 0U && provider_->lifecycle == BackendServiceProviderLifecycle::Retiring &&
                CanFinalizeHere(*provider_))
                shutdown = PrepareShutdown(*provider_);
        }
        provider_->drained.notify_all();
        if (ownsExecutionSlot_) {
            --ExecutingProviders.size;
            ExecutingProviders.providers[ExecutingProviders.size] = nullptr;
            ownsExecutionSlot_ = false;
        }
        shutdown.Invoke();
        provider_.reset();
    }

    namespace Detail {
        Result<BackendServiceCallAdmission> BeginBackendServiceCall(const std::shared_ptr<BackendServiceProviderState> &provider,
                                                                    const CancellationToken &caller) {
            std::scoped_lock lock{provider->mutex};
            if (!provider->registered.load(std::memory_order_acquire))
                return Result<BackendServiceCallAdmission>::Failure(MakeError(ExtensionErrors::BackendServiceUnavailable));
            if (provider->descriptor.threadRule == BackendServiceThreadRule::ProviderOwnerThread &&
                provider->ownerThread != std::this_thread::get_id())
                return Result<BackendServiceCallAdmission>::Failure(MakeError(ExtensionErrors::BackendServiceThreadViolation));
            if (caller.IsCancellationRequested() || provider->cancellation.Token().IsCancellationRequested())
                return Result<BackendServiceCallAdmission>::Failure(BackendServiceCancellationError(provider->descriptor));
            if (ExecutingProviders.size >= ExecutingProviders.providers.size())
                return Result<BackendServiceCallAdmission>::Failure(MakeError(ExtensionErrors::BackendServiceCapacityExceeded));
            ++provider->activeCalls;
            return Result<BackendServiceCallAdmission>::Success(
                BackendServiceCallAdmission{provider,
                                            BackendServiceCallContext{provider->descriptor, caller, provider->cancellation.Token()}});
        }

        void *BackendServiceObject(const std::shared_ptr<BackendServiceProviderState> &provider) noexcept {
            return provider->service.get();
        }

        Error AttributeBackendServiceError(const BackendServiceDescriptor &provider, Error cause) {
            const std::string detail = std::format("Backend service failed: {}@{} ({}).", provider.providerId, provider.providerGeneration,
                                                   provider.serviceId.value);
            return cause.code.Value().empty() ? MakeError(ExtensionErrors::BackendServiceInvocationFailed, detail)
                                              : WrapError(ExtensionErrors::BackendServiceInvocationFailed, std::move(cause), detail);
        }

        Error BackendServiceCancellationError(const BackendServiceDescriptor &provider) {
            return MakeError(ExtensionErrors::BackendServiceCancelled,
                             std::format("Backend service call cancelled: {}@{} ({}).", provider.providerId, provider.providerGeneration,
                                         provider.serviceId.value));
        }

        Error BackendServiceCancellationError(const ApplicationCapabilityProviderDescriptor &provider) {
            return MakeError(ExtensionErrors::BackendServiceCancelled,
                             std::format("Backend service call cancelled: {}@{}.", provider.providerId, provider.providerGeneration));
        }
    }  // namespace Detail

    BackendServiceRegistration::BackendServiceRegistration(std::weak_ptr<BackendServiceRegistryState> registry,
                                                           std::shared_ptr<BackendServiceProviderState> provider) noexcept
        : registry_(std::move(registry)), provider_(std::move(provider)) {}

    BackendServiceRegistration::~BackendServiceRegistration() {
        static_cast<void>(Reset());
    }

    BackendServiceRegistration::BackendServiceRegistration(BackendServiceRegistration &&other) noexcept
        : registry_(std::move(other.registry_)), provider_(std::move(other.provider_)) {}

    BackendServiceRegistration &BackendServiceRegistration::operator=(BackendServiceRegistration &&other) noexcept {
        if (this == &other)
            return *this;
        static_cast<void>(Reset());
        registry_ = std::move(other.registry_);
        provider_ = std::move(other.provider_);
        return *this;
    }

    /** @copydoc BackendServiceRegistration::Reset */
    Result<BackendServiceRetirementDisposition> BackendServiceRegistration::Reset() noexcept {
        if (provider_ == nullptr)
            return Result<BackendServiceRetirementDisposition>::Success(BackendServiceRetirementDisposition::ShutdownComplete);
        BackendServiceRetirementDisposition disposition;
        if (auto registry = registry_.lock()) {
            disposition = RetireProvider(registry, provider_);
        } else {
            disposition = StopProvider(provider_, std::chrono::steady_clock::now() + BackendServiceRegistryConfig{}.drainDeadline);
            if (disposition != BackendServiceRetirementDisposition::ShutdownComplete)
                return Result<BackendServiceRetirementDisposition>::Success(disposition);
        }
        provider_.reset();
        registry_.reset();
        return Result<BackendServiceRetirementDisposition>::Success(disposition);
    }

    /** @copydoc BackendServiceRegistration::IsRegistered */
    bool BackendServiceRegistration::IsRegistered() const noexcept {
        return provider_ != nullptr && provider_->registered.load(std::memory_order_acquire);
    }

    BackendServiceRegistry::BackendServiceRegistry(BackendServiceRegistryConfig config)
        : state_(std::make_shared<BackendServiceRegistryState>()) {
        if (config.drainDeadline <= std::chrono::milliseconds::zero())
            config.drainDeadline = std::chrono::milliseconds{1};
        state_->config = config;
        state_->providers.reserve(MaximumServices);
        state_->retiredProviders.reserve(MaximumServices);
    }

    BackendServiceRegistry::~BackendServiceRegistry() {
        static_cast<void>(BeginShutdown());
    }

    BackendServiceRegistry &BackendServiceRegistry::operator=(BackendServiceRegistry &&other) noexcept {
        if (this == &other)
            return *this;
        static_cast<void>(BeginShutdown());
        state_ = std::move(other.state_);
        return *this;
    }

    /** @copydoc BackendServiceRegistry::Register */
    Result<BackendServiceRegistration> BackendServiceRegistry::RegisterErased(BackendServiceDescriptor descriptor,
                                                                              std::shared_ptr<void> service, const void *typeTag,
                                                                              const ShutdownFunction shutdown) {
        if (!ValidDescriptor(descriptor) || service == nullptr || typeTag == nullptr || shutdown == nullptr)
            return Result<BackendServiceRegistration>::Failure(MakeError(ExtensionErrors::BackendServiceInvalid));
        if (state_ == nullptr)
            return Result<BackendServiceRegistration>::Failure(MakeError(ExtensionErrors::BackendServiceShutdown));
        std::scoped_lock lock{state_->mutex};
        if (state_->shutdown)
            return Result<BackendServiceRegistration>::Failure(MakeError(ExtensionErrors::BackendServiceShutdown));
        const auto duplicate = std::ranges::find_if(state_->providers, [&descriptor](const auto &provider) {
            return provider->descriptor.serviceId == descriptor.serviceId && provider->descriptor.version == descriptor.version;
        });
        if (duplicate != state_->providers.end())
            return Result<BackendServiceRegistration>::Failure(MakeError(ExtensionErrors::BackendServiceDuplicate));
        if (state_->providers.size() >= MaximumServices)
            return Result<BackendServiceRegistration>::Failure(MakeError(ExtensionErrors::BackendServiceCapacityExceeded));

        auto provider = std::make_shared<BackendServiceProviderState>();
        provider->descriptor = std::move(descriptor);
        provider->service = std::move(service);
        provider->typeTag = typeTag;
        provider->shutdown = shutdown;
        provider->ownerThread = std::this_thread::get_id();
        state_->providers.push_back(provider);
        return Result<BackendServiceRegistration>::Success(BackendServiceRegistration{state_, std::move(provider)});
    }

    /** @copydoc BackendServiceRegistry::Resolve */
    Result<std::shared_ptr<BackendServiceProviderState>> BackendServiceRegistry::ResolveErased(
        const ApplicationCapabilityProviderDescriptor &authority, const BackendServiceId &serviceId,
        const BackendServiceContractId &contractId, const void *typeTag) const {
        if (!Detail::IsCanonicalExtensionAuthorityId(serviceId.value) || !Detail::IsCanonicalExtensionAuthorityId(contractId.value) ||
            typeTag == nullptr)
            return Result<std::shared_ptr<BackendServiceProviderState>>::Failure(MakeError(ExtensionErrors::BackendServiceInvalid));
        if (state_ == nullptr)
            return Result<std::shared_ptr<BackendServiceProviderState>>::Failure(MakeError(ExtensionErrors::BackendServiceShutdown));
        std::scoped_lock lock{state_->mutex};
        if (state_->shutdown)
            return Result<std::shared_ptr<BackendServiceProviderState>>::Failure(MakeError(ExtensionErrors::BackendServiceShutdown));
        std::shared_ptr<BackendServiceProviderState> found;
        bool serviceExists = false;
        for (const auto &provider : state_->providers) {
            if (provider->descriptor.serviceId != serviceId)
                continue;
            serviceExists = true;
            if (MatchesAuthority(provider->descriptor, authority)) {
                found = provider;
                break;
            }
        }
        if (found == nullptr && !serviceExists)
            return Result<std::shared_ptr<BackendServiceProviderState>>::Failure(MakeError(ExtensionErrors::BackendServiceUnavailable));
        if (found == nullptr || found->descriptor.contractId != contractId)
            return Result<std::shared_ptr<BackendServiceProviderState>>::Failure(
                MakeError(ExtensionErrors::BackendServiceContractMismatch));
        if (found->typeTag != typeTag)
            return Result<std::shared_ptr<BackendServiceProviderState>>::Failure(MakeError(ExtensionErrors::BackendServiceTypeMismatch));
        return Result<std::shared_ptr<BackendServiceProviderState>>::Success(std::move(found));
    }

    /** @copydoc BackendServiceRegistry::BeginShutdown */
    Result<BackendServiceRetirementDisposition> BackendServiceRegistry::BeginShutdown() noexcept {
        if (state_ == nullptr)
            return Result<BackendServiceRetirementDisposition>::Success(BackendServiceRetirementDisposition::ShutdownComplete);
        std::vector<std::shared_ptr<BackendServiceProviderState>> providers;
        bool alreadyShutdown{};
        {
            std::scoped_lock lock{state_->mutex};
            alreadyShutdown = state_->shutdown;
            if (!alreadyShutdown) {
                state_->shutdown = true;
                providers = std::move(state_->providers);
            }
        }
        if (alreadyShutdown)
            return FinalizeRetiredOnOwnerThread();
        auto aggregate = BackendServiceRetirementDisposition::ShutdownComplete;
        const auto deadline = std::chrono::steady_clock::now() + state_->config.drainDeadline;
        for (auto iterator = providers.rbegin(); iterator != providers.rend(); ++iterator) {
            const auto disposition = StopProvider(*iterator, deadline);
            MergeDisposition(aggregate, disposition);
            if (disposition != BackendServiceRetirementDisposition::ShutdownComplete) {
                std::scoped_lock lock{state_->mutex};
                state_->retiredProviders.push_back(*iterator);
            }
        }
        return Result<BackendServiceRetirementDisposition>::Success(aggregate);
    }

    /** @copydoc BackendServiceRegistry::FinalizeRetiredOnOwnerThread */
    Result<BackendServiceRetirementDisposition> BackendServiceRegistry::FinalizeRetiredOnOwnerThread() noexcept {
        if (state_ == nullptr)
            return Result<BackendServiceRetirementDisposition>::Success(BackendServiceRetirementDisposition::ShutdownComplete);
        std::vector<std::shared_ptr<BackendServiceProviderState>> retired;
        {
            std::scoped_lock lock{state_->mutex};
            retired = state_->retiredProviders;
        }
        auto aggregate = BackendServiceRetirementDisposition::ShutdownComplete;
        for (const auto &provider : retired) {
            const auto disposition = FinalizeRetiredProvider(provider);
            MergeDisposition(aggregate, disposition);
            if (disposition == BackendServiceRetirementDisposition::ShutdownComplete) {
                std::scoped_lock lock{state_->mutex};
                std::erase(state_->retiredProviders, provider);
            }
        }
        return Result<BackendServiceRetirementDisposition>::Success(aggregate);
    }

    /** @copydoc BackendServiceRegistry::IsShutdown */
    bool BackendServiceRegistry::IsShutdown() const noexcept {
        if (state_ == nullptr)
            return true;
        std::scoped_lock lock{state_->mutex};
        return state_->shutdown;
    }
}  // namespace Horo::Extensions
