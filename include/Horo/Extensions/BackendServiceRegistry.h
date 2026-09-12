#pragma once

/**
 * @file BackendServiceRegistry.h
 * @brief Typed, host-owned invocation boundary for backend-only service contributions.
 */

#include "Horo/Extensions/ApplicationCapabilityRegistry.h"
#include "Horo/Foundation/CancellationToken.h"

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>

namespace Horo::Extensions {
    struct BackendServiceProviderState;
    struct BackendServiceRegistryState;
    class BackendServiceCallAdmission;

    namespace Detail {
        [[nodiscard]] Result<BackendServiceCallAdmission> BeginBackendServiceCall(
            const std::shared_ptr<BackendServiceProviderState> &provider, const CancellationToken &caller);
    }

    /** @brief Stable identity of one backend-neutral callable service export. */
    struct BackendServiceId final {
        std::string value;
        bool operator==(const BackendServiceId &) const noexcept = default;
    };

    /** @brief Stable identity of the typed C++ contract implemented by a service export. */
    struct BackendServiceContractId final {
        std::string value;
        bool operator==(const BackendServiceContractId &) const noexcept = default;
    };

    /** @brief Thread rule enforced before provider code is entered. */
    enum class BackendServiceThreadRule : std::uint8_t {
        AnyThread,
        ProviderOwnerThread,
    };

    /** @brief Immutable service identity, capability binding, and invocation policy. */
    struct BackendServiceDescriptor final {
        BackendServiceId serviceId;                                               /**< Stable exported service identity. */
        BackendServiceContractId contractId;                                      /**< Stable typed callable contract identity. */
        ExtensionCapabilityId capability;                                         /**< Application capability authorizing calls. */
        ApplicationCapabilityVersion version{};                                   /**< Exact service contract version. */
        std::string providerId;                                                   /**< Canonical stable provider identity. */
        std::uint64_t providerGeneration{};                                       /**< Non-zero activation generation. */
        BackendServiceThreadRule threadRule{BackendServiceThreadRule::AnyThread}; /**< Enforced call-thread policy. */
    };

    /** @brief Cancellation and attribution supplied to one admitted typed service operation. */
    class BackendServiceCallContext final {
    public:
        /** @brief Returns the exact provider generation executing the operation. */
        [[nodiscard]] const BackendServiceDescriptor &Provider() const noexcept;

        /** @brief Reports cancellation requested by either the caller or provider lifecycle. */
        [[nodiscard]] bool IsCancellationRequested() const noexcept;

    private:
        friend class BackendServiceCallAdmission;
        friend Result<BackendServiceCallAdmission> Detail::BeginBackendServiceCall(const std::shared_ptr<BackendServiceProviderState> &,
                                                                                   const CancellationToken &);
        BackendServiceCallContext(const BackendServiceDescriptor &provider, CancellationToken caller,
                                  CancellationToken providerCancellation) noexcept;

        const BackendServiceDescriptor *provider_{};
        CancellationToken caller_;
        CancellationToken providerCancellation_;
    };

    /** @brief Move-only admission guard retaining one active provider operation. */
    class BackendServiceCallAdmission final {
    public:
        ~BackendServiceCallAdmission();
        BackendServiceCallAdmission(const BackendServiceCallAdmission &) = delete;
        BackendServiceCallAdmission &operator=(const BackendServiceCallAdmission &) = delete;
        BackendServiceCallAdmission(BackendServiceCallAdmission &&other) noexcept;
        BackendServiceCallAdmission &operator=(BackendServiceCallAdmission &&other) noexcept;

        /** @brief Returns the operation context valid for this admitted call. */
        [[nodiscard]] const BackendServiceCallContext &Context() const noexcept;

    private:
        friend class BackendServiceRegistry;
        template <typename Service> friend class BackendServiceCall;
        friend Result<BackendServiceCallAdmission> Detail::BeginBackendServiceCall(const std::shared_ptr<BackendServiceProviderState> &,
                                                                                   const CancellationToken &);
        BackendServiceCallAdmission(std::shared_ptr<BackendServiceProviderState> provider, BackendServiceCallContext context) noexcept;
        void Reset() noexcept;

        std::shared_ptr<BackendServiceProviderState> provider_;
        BackendServiceCallContext context_;
    };

    namespace Detail {
        template <typename Service> inline constexpr std::byte BackendServiceTypeTag{};

        [[nodiscard]] void *BackendServiceObject(const std::shared_ptr<BackendServiceProviderState> &provider) noexcept;
        [[nodiscard]] Error AttributeBackendServiceError(const BackendServiceDescriptor &provider, Error cause);
        [[nodiscard]] Error BackendServiceCancellationError(const BackendServiceDescriptor &provider);
    }  // namespace Detail

    /** @brief One-shot typed invocation handle bound to an admitted application capability lease. */
    template <typename Service> class BackendServiceCall final {
    public:
        BackendServiceCall(const BackendServiceCall &) = delete;
        BackendServiceCall &operator=(const BackendServiceCall &) = delete;
        BackendServiceCall(BackendServiceCall &&) noexcept = default;
        BackendServiceCall &operator=(BackendServiceCall &&) noexcept = default;

        /**
         * @brief Invokes one mutable typed service operation exactly once.
         * @tparam Request Operation request type owned by the service contract.
         * @tparam Response Operation response type owned by the service contract.
         * @param operation Typed member operation to invoke.
         * @param request Immutable request passed to the provider.
         * @param cancellation Caller-owned cooperative cancellation.
         * @return Typed response or attributed provider, cancellation, lifecycle, or thread failure.
         */
        template <typename Request, typename Response>
        [[nodiscard]] Result<Response> Invoke(Result<Response> (Service::*operation)(const Request &, const BackendServiceCallContext &),
                                              const Request &request, CancellationToken cancellation = {}) && {
            return InvokeImpl(operation, request, std::move(cancellation));
        }

        /** @brief Invokes one const typed service operation exactly once. */
        template <typename Request, typename Response>
        [[nodiscard]] Result<Response> Invoke(Result<Response> (Service::*operation)(const Request &, const BackendServiceCallContext &)
                                                  const,
                                              const Request &request, CancellationToken cancellation = {}) && {
            return InvokeImpl(operation, request, std::move(cancellation));
        }

    private:
        friend class BackendServiceRegistry;

        BackendServiceCall(std::shared_ptr<BackendServiceProviderState> provider, ApplicationCapabilityProviderLease authority) noexcept
            : provider_(std::move(provider)), authority_(std::move(authority)) {}

        template <typename Operation, typename Request>
        [[nodiscard]] auto InvokeImpl(Operation operation, const Request &request, CancellationToken cancellation)
            && -> std::invoke_result_t<Operation, Service &, const Request &, const BackendServiceCallContext &> {
            using OperationResult = std::invoke_result_t<Operation, Service &, const Request &, const BackendServiceCallContext &>;
            const auto provider = std::exchange(provider_, {});
            if (provider == nullptr)
                return OperationResult::Failure(Detail::BackendServiceCancellationError(authority_.Descriptor()));
            auto admitted = Detail::BeginBackendServiceCall(provider, cancellation);
            if (admitted.HasError())
                return OperationResult::Failure(admitted.ErrorValue());
            BackendServiceCallAdmission call = std::move(admitted).Value();
            auto *service = static_cast<Service *>(Detail::BackendServiceObject(provider));
            try {
                OperationResult result = std::invoke(operation, *service, request, call.Context());
                if (call.Context().IsCancellationRequested())
                    return OperationResult::Failure(Detail::BackendServiceCancellationError(call.Context().Provider()));
                if (result.HasError())
                    return OperationResult::Failure(Detail::AttributeBackendServiceError(call.Context().Provider(), result.ErrorValue()));
                return result;
            } catch (...) {
                return OperationResult::Failure(Detail::AttributeBackendServiceError(call.Context().Provider(), {}));
            }
        }

        std::shared_ptr<BackendServiceProviderState> provider_;
        ApplicationCapabilityProviderLease authority_;
    };

    /** @brief Move-only registration owning discoverability, cancellation, drainage, and service shutdown. */
    class BackendServiceRegistration final {
    public:
        ~BackendServiceRegistration();
        BackendServiceRegistration(const BackendServiceRegistration &) = delete;
        BackendServiceRegistration &operator=(const BackendServiceRegistration &) = delete;
        BackendServiceRegistration(BackendServiceRegistration &&other) noexcept;
        BackendServiceRegistration &operator=(BackendServiceRegistration &&other) noexcept;

        /** @brief Revokes future calls, cancels and drains active work, then shuts down the service exactly once. */
        void Reset() noexcept;

        /** @brief Reports whether this registration still owns a discoverable service. */
        [[nodiscard]] bool IsRegistered() const noexcept;

    private:
        friend class BackendServiceRegistry;
        BackendServiceRegistration(std::weak_ptr<BackendServiceRegistryState> registry,
                                   std::shared_ptr<BackendServiceProviderState> provider) noexcept;

        std::weak_ptr<BackendServiceRegistryState> registry_;
        std::shared_ptr<BackendServiceProviderState> provider_;
    };

    /** @brief Explicit host-owned registry for typed backend-only service contributions. */
    class BackendServiceRegistry final {
    public:
        static constexpr std::size_t MaximumServices = 256; /**< Hard publication bound. */

        BackendServiceRegistry();
        ~BackendServiceRegistry();
        BackendServiceRegistry(const BackendServiceRegistry &) = delete;
        BackendServiceRegistry &operator=(const BackendServiceRegistry &) = delete;
        BackendServiceRegistry(BackendServiceRegistry &&) noexcept = default;
        BackendServiceRegistry &operator=(BackendServiceRegistry &&other) noexcept;

        /**
         * @brief Publishes a typed service implementation without invoking provider code.
         * @tparam Service Contract type with a noexcept `Shutdown()` lifecycle method.
         * @param descriptor Exact service, capability, provider, version, generation, and thread policy.
         * @param service Provider implementation whose sole ownership transfers to the registry.
         * @return Lifetime registration or a typed invalid, duplicate, capacity, or shutdown failure.
         */
        template <typename Service>
            requires requires(Service &service) {
                { service.Shutdown() } noexcept -> std::same_as<void>;
            }
        [[nodiscard]] Result<BackendServiceRegistration> Register(BackendServiceDescriptor descriptor, std::unique_ptr<Service> service) {
            std::shared_ptr<void> erased{std::move(service)};
            return RegisterErased(std::move(descriptor), std::move(erased), &Detail::BackendServiceTypeTag<Service>,
                                  [](void *object) noexcept {
                static_cast<Service *>(object)->Shutdown();
            });
        }

        /**
         * @brief Resolves one exact typed service for a single host-mediated call.
         * @param authority Compatible application capability/provider lease for the caller.
         * @param serviceId Exact service export identity requested by the caller.
         * @param contractId Exact typed contract expected by the caller.
         * @return One-shot typed call handle or a typed unavailable, mismatch, or shutdown failure.
         */
        template <typename Service>
        [[nodiscard]] Result<BackendServiceCall<Service>> Resolve(ApplicationCapabilityProviderLease authority,
                                                                  const BackendServiceId &serviceId,
                                                                  const BackendServiceContractId &contractId) const {
            auto resolved = ResolveErased(authority.Descriptor(), serviceId, contractId, &Detail::BackendServiceTypeTag<Service>);
            if (resolved.HasError())
                return Result<BackendServiceCall<Service>>::Failure(resolved.ErrorValue());
            return Result<BackendServiceCall<Service>>::Success(
                BackendServiceCall<Service>{std::move(resolved).Value(), std::move(authority)});
        }

        /** @brief Idempotently revokes, cancels, drains, and shuts down every service. */
        void BeginShutdown() noexcept;

        /** @brief Reports whether registration and resolution are terminally closed. */
        [[nodiscard]] bool IsShutdown() const noexcept;

    private:
        using ShutdownFunction = void (*)(void *) noexcept;
        [[nodiscard]] Result<BackendServiceRegistration> RegisterErased(BackendServiceDescriptor descriptor, std::shared_ptr<void> service,
                                                                        const void *typeTag, ShutdownFunction shutdown);
        [[nodiscard]] Result<std::shared_ptr<BackendServiceProviderState>> ResolveErased(
            const ApplicationCapabilityProviderDescriptor &authority, const BackendServiceId &serviceId,
            const BackendServiceContractId &contractId, const void *typeTag) const;

        std::shared_ptr<BackendServiceRegistryState> state_;
    };
}  // namespace Horo::Extensions
