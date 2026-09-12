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
                if (callback != nullptr)
                    callback(service.get());
            }
        };

        struct RetirementRule final {
            bool applies;
            BackendServiceProviderLifecycle lifecycle;
            BackendServiceRetirementDisposition disposition;
            bool shutsDown;
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
            constexpr std::array Rules{BackendServiceThreadRule::AnyThread, BackendServiceThreadRule::ProviderOwnerThread};
            return std::ranges::find(Rules, rule) != Rules.end();
        }

        [[nodiscard]] bool ValidDescriptor(const BackendServiceDescriptor &descriptor) noexcept {
            const std::array valid{
                Detail::IsCanonicalExtensionAuthorityId(descriptor.serviceId.value),
                Detail::IsCanonicalExtensionAuthorityId(descriptor.contractId.value),
                Detail::IsCanonicalExtensionAuthorityId(descriptor.capability.value),
                Detail::IsCanonicalExtensionAuthorityId(descriptor.providerId),
                descriptor.version != ApplicationCapabilityVersion{},
                descriptor.providerGeneration != 0U,
                ValidThreadRule(descriptor.threadRule),
            };
            return std::ranges::find(valid, false) == valid.end();
        }

        [[nodiscard]] bool CanFinalizeHere(const BackendServiceProviderState &provider) noexcept {
            const std::array allowed{provider.descriptor.threadRule == BackendServiceThreadRule::AnyThread,
                                     provider.ownerThread == std::this_thread::get_id()};
            return std::ranges::find(allowed, true) != allowed.end();
        }

        [[nodiscard]] ServiceShutdown PrepareShutdown(BackendServiceProviderState &provider) noexcept {
            provider.lifecycle = BackendServiceProviderLifecycle::Shutdown;
            return {std::move(provider.service), provider.shutdown};
        }

        [[nodiscard]] RetirementRule SelectRetirement(const BackendServiceProviderState &provider, const bool executing, const bool drained,
                                                      const bool deadlineExpired) noexcept {
            const std::array rules{
                RetirementRule{provider.service == nullptr, BackendServiceProviderLifecycle::Shutdown,
                               BackendServiceRetirementDisposition::ShutdownComplete, false},
                RetirementRule{provider.lifecycle == BackendServiceProviderLifecycle::RetainedRestartRequired,
                               BackendServiceProviderLifecycle::RetainedRestartRequired,
                               BackendServiceRetirementDisposition::RestartRequired, false},
                RetirementRule{executing, BackendServiceProviderLifecycle::Retiring,
                               BackendServiceRetirementDisposition::DeferredUntilCallExit, false},
                RetirementRule{deadlineExpired, BackendServiceProviderLifecycle::RetainedRestartRequired,
                               BackendServiceRetirementDisposition::RestartRequired, false},
                RetirementRule{!drained, BackendServiceProviderLifecycle::Retiring,
                               BackendServiceRetirementDisposition::DeferredUntilCallExit, false},
                RetirementRule{!CanFinalizeHere(provider), BackendServiceProviderLifecycle::AwaitingOwnerFinalization,
                               BackendServiceRetirementDisposition::OwnerThreadFinalizationRequired, false},
                RetirementRule{true, BackendServiceProviderLifecycle::Shutdown, BackendServiceRetirementDisposition::ShutdownComplete,
                               true},
            };
            return *std::ranges::find(rules, true, &RetirementRule::applies);
        }

        [[nodiscard]] BackendServiceRetirementDisposition ApplyRetirement(BackendServiceProviderState &provider, const RetirementRule &rule,
                                                                          std::unique_lock<std::mutex> lock) noexcept {
            provider.lifecycle = rule.lifecycle;
            if (!rule.shutsDown)
                return rule.disposition;
            ServiceShutdown shutdown = PrepareShutdown(provider);
            lock.unlock();
            shutdown.Invoke();
            return rule.disposition;
        }

        [[nodiscard]] BackendServiceRetirementDisposition StopProvider(const std::shared_ptr<BackendServiceProviderState> &provider,
                                                                       const std::chrono::steady_clock::time_point deadline) noexcept {
            std::unique_lock lock{provider->mutex};
            provider->registered.store(false, std::memory_order_release);
            provider->cancellation.RequestCancellation();
            const bool executing = IsExecuting(provider.get());
            const std::array skipWaitFacts{executing, provider->lifecycle == BackendServiceProviderLifecycle::RetainedRestartRequired,
                                           provider->service == nullptr};
            const bool skipWait = std::ranges::find(skipWaitFacts, true) != skipWaitFacts.end();
            const std::array deadlines{deadline, std::chrono::steady_clock::now()};
            const bool drained = provider->drained.wait_until(lock, deadlines[static_cast<std::size_t>(skipWait)], [&provider] {
                return provider->activeCalls == 0U;
            });
            constexpr std::array DeadlineExpired{true, false, false, false};
            const std::size_t deadlineState = static_cast<std::size_t>(skipWait) * 2U + static_cast<std::size_t>(drained);
            return ApplyRetirement(*provider, SelectRetirement(*provider, executing, drained, DeadlineExpired[deadlineState]),
                                   std::move(lock));
        }

        [[nodiscard]] BackendServiceRetirementDisposition FinalizeRetiredProvider(
            const std::shared_ptr<BackendServiceProviderState> &provider) noexcept {
            std::unique_lock lock{provider->mutex};
            const bool executing = IsExecuting(provider.get());
            const bool drained = provider->activeCalls == 0U;
            return ApplyRetirement(*provider, SelectRetirement(*provider, executing, drained, false), std::move(lock));
        }

        void PruneShutdownProviders(BackendServiceRegistryState &registry) {
            std::erase_if(registry.retiredProviders, [](const auto &provider) {
                std::scoped_lock lock{provider->mutex};
                return provider->lifecycle == BackendServiceProviderLifecycle::Shutdown;
            });
        }

        [[nodiscard]] BackendServiceRetirementDisposition RetireProvider(
            const std::shared_ptr<BackendServiceRegistryState> &registry,
            const std::shared_ptr<BackendServiceProviderState> &provider) noexcept {
            {
                std::scoped_lock lock{registry->mutex};
                std::erase(registry->providers, provider);
            }
            const auto disposition = StopProvider(provider, std::chrono::steady_clock::now() + registry->config.drainDeadline);
            {
                std::scoped_lock lock{registry->mutex};
                registry->retiredProviders.push_back(provider);
                PruneShutdownProviders(*registry);
            }
            return disposition;
        }

        [[nodiscard]] bool MatchesAuthority(const BackendServiceDescriptor &service,
                                            const ApplicationCapabilityProviderDescriptor &authority) noexcept {
            return std::tie(service.capability, service.version, service.providerId, service.providerGeneration) ==
                   std::tie(authority.capability, authority.version, authority.providerId, authority.providerGeneration);
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
            const std::array finalizationFacts{provider_->activeCalls == 0U,
                                               provider_->lifecycle == BackendServiceProviderLifecycle::Retiring,
                                               CanFinalizeHere(*provider_)};
            if (std::ranges::find(finalizationFacts, false) == finalizationFacts.end())
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
        const BackendServiceRetirementDisposition disposition = RetireProvider(registry_, provider_);
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
        const std::array invalid{!ValidDescriptor(descriptor), service == nullptr, typeTag == nullptr, shutdown == nullptr};
        if (std::ranges::find(invalid, true) != invalid.end())
            return Result<BackendServiceRegistration>::Failure(MakeError(ExtensionErrors::BackendServiceInvalid));
        if (state_ == nullptr)
            return Result<BackendServiceRegistration>::Failure(MakeError(ExtensionErrors::BackendServiceShutdown));
        std::scoped_lock lock{state_->mutex};
        if (state_->shutdown)
            return Result<BackendServiceRegistration>::Failure(MakeError(ExtensionErrors::BackendServiceShutdown));
        const auto duplicate = std::ranges::find_if(state_->providers, [&descriptor](const auto &provider) {
            return std::tie(provider->descriptor.serviceId, provider->descriptor.version) ==
                   std::tie(descriptor.serviceId, descriptor.version);
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
        const std::array invalid{!Detail::IsCanonicalExtensionAuthorityId(serviceId.value),
                                 !Detail::IsCanonicalExtensionAuthorityId(contractId.value), typeTag == nullptr};
        if (std::ranges::find(invalid, true) != invalid.end())
            return Result<std::shared_ptr<BackendServiceProviderState>>::Failure(MakeError(ExtensionErrors::BackendServiceInvalid));
        if (state_ == nullptr)
            return Result<std::shared_ptr<BackendServiceProviderState>>::Failure(MakeError(ExtensionErrors::BackendServiceShutdown));
        std::scoped_lock lock{state_->mutex};
        if (state_->shutdown)
            return Result<std::shared_ptr<BackendServiceProviderState>>::Failure(MakeError(ExtensionErrors::BackendServiceShutdown));
        const auto service = std::ranges::find(state_->providers, serviceId, [](const auto &provider) {
            return provider->descriptor.serviceId;
        });
        const auto authorityMatch = std::ranges::find_if(state_->providers, [&serviceId, &authority](const auto &provider) {
            return provider->descriptor.serviceId == serviceId ? MatchesAuthority(provider->descriptor, authority) : false;
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
    Result<BackendServiceRetirementDisposition> BackendServiceRegistry::BeginShutdown() noexcept {
        if (state_ == nullptr)
            return Result<BackendServiceRetirementDisposition>::Success(BackendServiceRetirementDisposition::ShutdownComplete);
        std::vector<std::shared_ptr<BackendServiceProviderState>> providers;
        {
            std::scoped_lock lock{state_->mutex};
            state_->shutdown = true;
            providers = std::move(state_->providers);
        }
        auto aggregate = BackendServiceRetirementDisposition::ShutdownComplete;
        const auto deadline = std::chrono::steady_clock::now() + state_->config.drainDeadline;
        for (auto iterator = providers.rbegin(); iterator != providers.rend(); ++iterator) {
            const auto disposition = StopProvider(*iterator, deadline);
            MergeDisposition(aggregate, disposition);
            std::scoped_lock lock{state_->mutex};
            state_->retiredProviders.push_back(*iterator);
        }
        const auto finalized = FinalizeRetiredOnOwnerThread();
        MergeDisposition(aggregate, finalized.Value());
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
        }
        {
            std::scoped_lock lock{state_->mutex};
            PruneShutdownProviders(*state_);
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
