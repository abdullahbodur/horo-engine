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
#include <tuple>
#include <utility>
#include <vector>

namespace Horo::Extensions {
    enum class BackendServiceProviderLifecycle : std::uint8_t {
        Active,
        Retiring,
        AwaitingOwnerFinalization,
        Finalizing,
        RetainedRestartRequired,
        Shutdown,
    };

    struct BackendServiceProviderState final {
        BackendServiceDescriptor descriptor;
        std::shared_ptr<void> service;
        BackendServiceCodeLease codeLease{BackendServiceCodeLease::Retain(std::shared_ptr<const void>{})};
        const void *typeTag{};
        void (*shutdown)(void *) noexcept {};
        std::thread::id ownerThread;
        CancellationSource cancellation;
        std::mutex mutex;
        std::condition_variable drained;
        std::size_t activeCalls{};
        std::atomic_bool registered{true};
        BackendServiceProviderLifecycle lifecycle{BackendServiceProviderLifecycle::Active};
        std::weak_ptr<BackendServiceRegistryState> registry;
        std::uint64_t registrationSequence{};
        bool retirementQueued{};
    };

    struct BackendServiceRegistryState final {
        std::mutex mutex;
        std::vector<std::shared_ptr<BackendServiceProviderState>> providers;
        std::vector<std::shared_ptr<BackendServiceProviderState>> retiredProviders;
        BackendServiceRegistryConfig config;
        std::timed_mutex retirementMutex;
        std::atomic_bool restartRequired{};
        std::atomic_bool stagingShutdown{};
        std::uint64_t nextRegistrationSequence{};
        std::shared_ptr<BackendServiceRegistryState> restartQuarantine;
        BackendServiceRegistryState *nextQuarantined{};
        bool quarantined{};
        bool shutdown{};
    };

    namespace {
        struct ExecutingProviderStack final {
            std::array<BackendServiceProviderState *, BackendServiceRegistry::MaximumServices> providers{};
            std::size_t size{};
        };

        thread_local ExecutingProviderStack ExecutingProviders;

        struct RetiredProviderStorage final {
            BackendServiceCodeLease codeLease;
            std::shared_ptr<void> service;
        };

        [[nodiscard]] bool IsExecuting(const BackendServiceProviderState *provider) noexcept {
            const auto end = ExecutingProviders.providers.begin() + static_cast<std::ptrdiff_t>(ExecutingProviders.size);
            return std::ranges::find(ExecutingProviders.providers.begin(), end, provider) != end;
        }

        [[nodiscard]] bool ValidDescriptor(const BackendServiceDescriptor &descriptor) noexcept {
            const std::array valid{
                Detail::IsCanonicalExtensionAuthorityId(descriptor.serviceId.value),
                Detail::IsCanonicalExtensionAuthorityId(descriptor.contractId.value),
                Detail::IsCanonicalExtensionAuthorityId(descriptor.capability.value),
                Detail::IsCanonicalExtensionAuthorityId(descriptor.providerId),
                descriptor.version != ApplicationCapabilityVersion{},
                descriptor.providerGeneration != 0U,
                static_cast<std::uint8_t>(descriptor.threadRule) <=
                    static_cast<std::uint8_t>(BackendServiceThreadRule::ProviderOwnerThread),
            };
            return std::ranges::find(valid, false) == valid.end();
        }

        [[nodiscard]] bool CanFinalizeHere(const BackendServiceProviderState &provider) noexcept {
            const std::array allowed{provider.descriptor.threadRule == BackendServiceThreadRule::AnyThread,
                                     provider.ownerThread == std::this_thread::get_id()};
            return std::ranges::find(allowed, true) != allowed.end();
        }

        [[nodiscard]] BackendServiceRetirementDisposition StopProvider(const std::shared_ptr<BackendServiceRegistryState> &registry,
                                                                       const std::shared_ptr<BackendServiceProviderState> &provider,
                                                                       const std::chrono::steady_clock::time_point deadline,
                                                                       const bool waitForDrain) noexcept {
            std::unique_lock lock{provider->mutex};

            struct EarlyDisposition final {
                bool applies;
                BackendServiceRetirementDisposition disposition;
            };

            const std::array earlyDispositions{
                EarlyDisposition{provider->lifecycle == BackendServiceProviderLifecycle::Shutdown,
                                 BackendServiceRetirementDisposition::ShutdownComplete},
                EarlyDisposition{provider->lifecycle == BackendServiceProviderLifecycle::RetainedRestartRequired,
                                 BackendServiceRetirementDisposition::RestartRequired},
                EarlyDisposition{IsExecuting(provider.get()), BackendServiceRetirementDisposition::DeferredUntilCallExit},
            };
            const auto early = std::ranges::find(earlyDispositions, true, &EarlyDisposition::applies);
            if (early != earlyDispositions.end())
                return early->disposition;
            if (provider->activeCalls != 0U) {
                const std::array deadlines{std::chrono::steady_clock::now(), deadline};
                if (!provider->drained.wait_until(lock, deadlines[static_cast<std::size_t>(waitForDrain)], [&provider] {
                    return provider->activeCalls == 0U;
                })) {
                    constexpr std::array lifecycles{BackendServiceProviderLifecycle::Retiring,
                                                    BackendServiceProviderLifecycle::RetainedRestartRequired};
                    constexpr std::array dispositions{BackendServiceRetirementDisposition::DeferredUntilCallExit,
                                                      BackendServiceRetirementDisposition::RestartRequired};
                    if (waitForDrain)
                        registry->restartRequired.store(true, std::memory_order_release);
                    provider->lifecycle = lifecycles[static_cast<std::size_t>(waitForDrain)];
                    return dispositions[static_cast<std::size_t>(waitForDrain)];
                }
            }
            if (!CanFinalizeHere(*provider)) {
                provider->lifecycle = BackendServiceProviderLifecycle::AwaitingOwnerFinalization;
                return BackendServiceRetirementDisposition::OwnerThreadFinalizationRequired;
            }

            provider->lifecycle = BackendServiceProviderLifecycle::Finalizing;
            std::shared_ptr<void> service = provider->service;
            const auto shutdown = provider->shutdown;
            lock.unlock();
            shutdown(service.get());
            lock.lock();
            if (registry->restartRequired.load(std::memory_order_acquire)) {
                provider->lifecycle = BackendServiceProviderLifecycle::RetainedRestartRequired;
                lock.unlock();
                provider->drained.notify_all();
                return BackendServiceRetirementDisposition::RestartRequired;
            }
            service.reset();
            RetiredProviderStorage retired{std::move(provider->codeLease), std::move(provider->service)};
            provider->lifecycle = BackendServiceProviderLifecycle::Shutdown;
            lock.unlock();
            provider->drained.notify_all();
            retired.service.reset();
            return BackendServiceRetirementDisposition::ShutdownComplete;
        }

        void RequestRetirement(const std::shared_ptr<BackendServiceProviderState> &provider) noexcept {
            std::scoped_lock lock{provider->mutex};
            provider->registered.store(false, std::memory_order_release);
            provider->cancellation.RequestCancellation();
            if (provider->lifecycle == BackendServiceProviderLifecycle::Active)
                provider->lifecycle = BackendServiceProviderLifecycle::Retiring;
        }

        void EnqueueRetirement(BackendServiceRegistryState &registry, const std::shared_ptr<BackendServiceProviderState> &provider) {
            if (provider->retirementQueued)
                return;
            const auto position = std::ranges::lower_bound(registry.retiredProviders, provider->registrationSequence, std::greater{},
                                                           &BackendServiceProviderState::registrationSequence);
            registry.retiredProviders.insert(position, provider);
            provider->retirementQueued = true;
        }

        [[nodiscard]] BackendServiceRetirementDisposition ProgressRetirements(const std::shared_ptr<BackendServiceRegistryState> &registry,
                                                                              const std::chrono::steady_clock::time_point deadline,
                                                                              const bool waitForDrain) noexcept {
            if (registry->stagingShutdown.load(std::memory_order_acquire))
                return BackendServiceRetirementDisposition::DeferredUntilCallExit;
            std::unique_lock coordinator{registry->retirementMutex, std::defer_lock};
            const std::array coordinatorDeadlines{std::chrono::steady_clock::now(), deadline};
            if (!coordinator.try_lock_until(coordinatorDeadlines[static_cast<std::size_t>(waitForDrain)])) {
                if (waitForDrain)
                    registry->restartRequired.store(true, std::memory_order_release);
                constexpr std::array dispositions{BackendServiceRetirementDisposition::DeferredUntilCallExit,
                                                  BackendServiceRetirementDisposition::RestartRequired};
                return dispositions[static_cast<std::size_t>(waitForDrain)];
            }
            constexpr std::array initialDispositions{BackendServiceRetirementDisposition::ShutdownComplete,
                                                     BackendServiceRetirementDisposition::RestartRequired};
            auto aggregate = initialDispositions[static_cast<std::size_t>(registry->restartRequired.load(std::memory_order_acquire))];
            for (;;) {
                std::shared_ptr<BackendServiceProviderState> provider;
                {
                    std::scoped_lock lock{registry->mutex};
                    if (registry->retiredProviders.empty())
                        break;
                    provider = registry->retiredProviders.front();
                }
                const auto disposition = StopProvider(registry, provider, deadline, waitForDrain);
                aggregate = std::max(aggregate, disposition);
                if (disposition != BackendServiceRetirementDisposition::ShutdownComplete)
                    break;
                std::scoped_lock lock{registry->mutex};
                std::erase(registry->retiredProviders, provider);
            }
            return aggregate;
        }

        [[nodiscard]] BackendServiceRetirementDisposition RetireProvider(
            const std::shared_ptr<BackendServiceRegistryState> &registry,
            const std::shared_ptr<BackendServiceProviderState> &provider) noexcept {
            {
                std::scoped_lock lock{registry->mutex};
                std::erase(registry->providers, provider);
            }
            RequestRetirement(provider);
            {
                std::scoped_lock lock{registry->mutex};
                EnqueueRetirement(*registry, provider);
            }
            return ProgressRetirements(registry, std::chrono::steady_clock::now() + registry->config.drainDeadline, true);
        }

        void QuarantineForRestart(const std::shared_ptr<BackendServiceRegistryState> &registry) noexcept {
            static std::mutex mutex;
            static BackendServiceRegistryState *head{};
            std::scoped_lock lock{mutex};
            if (registry->quarantined)
                return;
            registry->restartQuarantine = registry;
            registry->nextQuarantined = head;
            registry->quarantined = true;
            head = registry.get();
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
        const std::array cancelled{caller_.IsCancellationRequested(), providerCancellation_.IsCancellationRequested()};
        return std::ranges::find(cancelled, true) != cancelled.end();
    }

    BackendServiceCallAdmission::BackendServiceCallAdmission(std::shared_ptr<BackendServiceProviderState> provider,
                                                             BackendServiceCallContext context) noexcept
        : provider_(std::move(provider)), context_(std::move(context)) {
        ExecutingProviders.providers[ExecutingProviders.size++] = provider_.get();
    }

    BackendServiceCallAdmission::~BackendServiceCallAdmission() {
        Reset();
    }

    /** @copydoc BackendServiceCallAdmission::Context */
    const BackendServiceCallContext &BackendServiceCallAdmission::Context() const noexcept {
        return context_;
    }

    void BackendServiceCallAdmission::Reset() noexcept {
        if (provider_ == nullptr)
            return;
        const std::shared_ptr<BackendServiceRegistryState> registry = provider_->registry.lock();
        {
            std::scoped_lock lock{provider_->mutex};
            --provider_->activeCalls;
        }
        provider_->drained.notify_all();
        --ExecutingProviders.size;
        ExecutingProviders.providers[ExecutingProviders.size] = nullptr;
        provider_.reset();
        if (registry != nullptr)
            static_cast<void>(ProgressRetirements(registry, std::chrono::steady_clock::now(), false));
    }

    namespace Detail {
        Result<BackendServiceCallAdmission> BeginBackendServiceCall(const std::shared_ptr<BackendServiceProviderState> &provider,
                                                                    const CancellationToken &caller) {
            std::scoped_lock lock{provider->mutex};
            const std::array cancellationFacts{caller.IsCancellationRequested(), provider->cancellation.Token().IsCancellationRequested()};
            const std::array failures{
                !provider->registered.load(std::memory_order_acquire),
                !CanFinalizeHere(*provider),
                std::ranges::find(cancellationFacts, true) != cancellationFacts.end(),
                ExecutingProviders.size >= ExecutingProviders.providers.size(),
            };
            const std::array errors{
                &ExtensionErrors::BackendServiceUnavailable,
                &ExtensionErrors::BackendServiceThreadViolation,
                &ExtensionErrors::BackendServiceCancelled,
                &ExtensionErrors::BackendServiceCapacityExceeded,
            };
            const auto failure = std::ranges::find(failures, true);
            if (failure != failures.end()) {
                const std::size_t index = static_cast<std::size_t>(failure - failures.begin());
                if (errors[index] == &ExtensionErrors::BackendServiceCancelled)
                    return Result<BackendServiceCallAdmission>::Failure(BackendServiceCancellationError(provider->descriptor));
                return Result<BackendServiceCallAdmission>::Failure(MakeError(*errors[index]));
            }
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

    BackendServiceRegistration::BackendServiceRegistration(std::shared_ptr<BackendServiceRegistryState> registry,
                                                           std::shared_ptr<BackendServiceProviderState> provider) noexcept
        : registry_(std::move(registry)), provider_(std::move(provider)) {}

    BackendServiceRegistration::~BackendServiceRegistration() {
        static_cast<void>(Reset());
    }

    /** @copydoc BackendServiceRegistration::Reset */
    BackendServiceRetirementDisposition BackendServiceRegistration::Reset() noexcept {
        if (provider_ == nullptr)
            return BackendServiceRetirementDisposition::ShutdownComplete;
        const BackendServiceRetirementDisposition disposition = RetireProvider(registry_, provider_);
        provider_.reset();
        registry_.reset();
        return disposition;
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
        if (BeginShutdown() != BackendServiceRetirementDisposition::ShutdownComplete)
            QuarantineForRestart(state_);
    }

    /** @copydoc BackendServiceRegistry::Register */
    Result<BackendServiceRegistration> BackendServiceRegistry::RegisterErased(BackendServiceDescriptor descriptor,
                                                                              std::shared_ptr<void> service,
                                                                              BackendServiceCodeLease codeLease, const void *typeTag,
                                                                              const ShutdownFunction shutdown) {
        const std::array invalid{!ValidDescriptor(descriptor), service == nullptr, codeLease.owner_ == nullptr, typeTag == nullptr,
                                 shutdown == nullptr};
        if (std::ranges::find(invalid, true) != invalid.end())
            return Result<BackendServiceRegistration>::Failure(MakeError(ExtensionErrors::BackendServiceInvalid));
        std::scoped_lock lock{state_->mutex};
        const auto duplicate = std::ranges::find_if(state_->providers, [&descriptor](const auto &provider) {
            return std::tie(provider->descriptor.serviceId, provider->descriptor.version) ==
                   std::tie(descriptor.serviceId, descriptor.version);
        });
        const auto retiredDuplicate = std::ranges::find_if(state_->retiredProviders, [&descriptor](const auto &provider) {
            return std::tie(provider->descriptor.serviceId, provider->descriptor.version) ==
                   std::tie(descriptor.serviceId, descriptor.version);
        });
        const std::array duplicateFacts{duplicate != state_->providers.end(), retiredDuplicate != state_->retiredProviders.end()};
        const std::array failures{state_->shutdown, std::ranges::find(duplicateFacts, true) != duplicateFacts.end(),
                                  state_->providers.size() + state_->retiredProviders.size() >= MaximumServices};
        constexpr std::array errors{&ExtensionErrors::BackendServiceShutdown, &ExtensionErrors::BackendServiceDuplicate,
                                    &ExtensionErrors::BackendServiceCapacityExceeded};
        const auto failure = std::ranges::find(failures, true);
        if (failure != failures.end())
            return Result<BackendServiceRegistration>::Failure(MakeError(*errors[static_cast<std::size_t>(failure - failures.begin())]));

        auto provider = std::make_shared<BackendServiceProviderState>();
        provider->descriptor = std::move(descriptor);
        provider->service = std::move(service);
        provider->codeLease = std::move(codeLease);
        provider->typeTag = typeTag;
        provider->shutdown = shutdown;
        provider->ownerThread = std::this_thread::get_id();
        provider->registry = state_;
        provider->registrationSequence = ++state_->nextRegistrationSequence;
        state_->providers.push_back(provider);
        return Result<BackendServiceRegistration>::Success(BackendServiceRegistration{state_, std::move(provider)});
    }

    /** @copydoc BackendServiceRegistry::Resolve */
    Result<std::shared_ptr<BackendServiceProviderState>> BackendServiceRegistry::ResolveErased(
        const ApplicationCapabilityProviderDescriptor &authority, const BackendServiceId &serviceId,
        const BackendServiceContractId &contractId, const void *typeTag) const {
        const std::array invalid{!Detail::IsCanonicalExtensionAuthorityId(serviceId.value),
                                 !Detail::IsCanonicalExtensionAuthorityId(contractId.value), typeTag == nullptr};
        if (std::ranges::find(invalid, true) != invalid.end())
            return Result<std::shared_ptr<BackendServiceProviderState>>::Failure(MakeError(ExtensionErrors::BackendServiceInvalid));
        std::scoped_lock lock{state_->mutex};
        if (state_->shutdown)
            return Result<std::shared_ptr<BackendServiceProviderState>>::Failure(MakeError(ExtensionErrors::BackendServiceShutdown));
        const auto service = std::ranges::find(state_->providers, serviceId, [](const auto &provider) {
            return provider->descriptor.serviceId;
        });
        const auto authorityMatch = std::ranges::find_if(state_->providers, [&serviceId, &authority](const auto &provider) {
            return std::tie(provider->descriptor.serviceId, provider->descriptor.capability, provider->descriptor.version,
                            provider->descriptor.providerId, provider->descriptor.providerGeneration) ==
                   std::tie(serviceId, authority.capability, authority.version, authority.providerId, authority.providerGeneration);
        });
        if (service == state_->providers.end())
            return Result<std::shared_ptr<BackendServiceProviderState>>::Failure(MakeError(ExtensionErrors::BackendServiceUnavailable));
        if (authorityMatch == state_->providers.end() || (*authorityMatch)->descriptor.contractId != contractId)
            return Result<std::shared_ptr<BackendServiceProviderState>>::Failure(
                MakeError(ExtensionErrors::BackendServiceContractMismatch));
        if ((*authorityMatch)->typeTag != typeTag)
            return Result<std::shared_ptr<BackendServiceProviderState>>::Failure(MakeError(ExtensionErrors::BackendServiceTypeMismatch));
        return Result<std::shared_ptr<BackendServiceProviderState>>::Success(*authorityMatch);
    }

    /** @copydoc BackendServiceRegistry::BeginShutdown */
    BackendServiceRetirementDisposition BackendServiceRegistry::BeginShutdown() noexcept {
        std::vector<std::shared_ptr<BackendServiceProviderState>> providers;
        bool stagesShutdown{};
        {
            std::scoped_lock lock{state_->mutex};
            if (!state_->shutdown) {
                state_->stagingShutdown.store(true, std::memory_order_release);
                state_->shutdown = true;
                providers = std::move(state_->providers);
                stagesShutdown = true;
            }
        }
        const auto deadline = std::chrono::steady_clock::now() + state_->config.drainDeadline;
        for (auto iterator = providers.rbegin(); iterator != providers.rend(); ++iterator) {
            RequestRetirement(*iterator);
            std::scoped_lock lock{state_->mutex};
            EnqueueRetirement(*state_, *iterator);
        }
        if (stagesShutdown)
            state_->stagingShutdown.store(false, std::memory_order_release);
        return ProgressRetirements(state_, deadline, true);
    }

    /** @copydoc BackendServiceRegistry::FinalizeRetiredOnOwnerThread */
    BackendServiceRetirementDisposition BackendServiceRegistry::FinalizeRetiredOnOwnerThread() noexcept {
        return ProgressRetirements(state_, std::chrono::steady_clock::now(), false);
    }

    /** @copydoc BackendServiceRegistry::IsShutdown */
    bool BackendServiceRegistry::IsShutdown() const noexcept {
        std::scoped_lock lock{state_->mutex};
        return state_->shutdown;
    }
}  // namespace Horo::Extensions
