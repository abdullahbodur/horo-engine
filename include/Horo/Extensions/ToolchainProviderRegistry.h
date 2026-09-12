#pragma once

/**
 * @file ToolchainProviderRegistry.h
 * @brief Host-policy-mediated external tool invocation for approved extension providers.
 */

#include "Horo/Foundation/CancellationToken.h"
#include "Horo/Foundation/Result.h"
#include "Horo/Platform/ExternalProcess.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace Horo::Extensions {
    /** @brief Stable logical identity of one host-resolved external tool. */
    struct ToolchainToolId final {
        std::string value;
        [[nodiscard]] bool operator==(const ToolchainToolId &) const noexcept = default;
    };

    /** @brief Immutable identity and allowed-tool declaration for one provider publication. */
    struct ToolchainProviderDescriptor final {
        std::string contributionId;         /**< Canonical toolchain.provider contribution identity. */
        std::string providerId;             /**< Canonical module/provider identity. */
        std::uint64_t providerGeneration{}; /**< Non-zero activation generation. */
        std::vector<ToolchainToolId> tools; /**< Sorted unique logical tools the provider may request. */
    };

    /** @brief Copyable generation-bound authority used to request one provider invocation. */
    struct ToolchainInvocationAuthority final {
        std::string contributionId;         /**< Exact published contribution identity. */
        std::uint64_t providerGeneration{}; /**< Exact activation generation. */
    };

    /** @brief Provider intent that deliberately contains no executable, environment, timeout, or output callback. */
    struct ToolchainInvocationIntent final {
        ToolchainToolId tool;               /**< Declared logical tool requested from host policy. */
        std::vector<std::string> arguments; /**< Untrusted requested arguments interpreted by host policy. */
    };

    /** @brief Host policy that resolves provider intent into the complete platform process request. */
    class IToolchainInvocationPolicy {
    public:
        virtual ~IToolchainInvocationPolicy() = default;

        /**
         * @brief Resolves and approves the exact executable, arguments, environment, timeout, output, and working directory.
         * @param provider Exact admitted provider generation.
         * @param intent Bounded provider request containing only a logical tool and requested arguments.
         * @return Complete host-owned shell-free platform request, or a typed policy rejection.
         */
        [[nodiscard]] virtual Result<ExternalProcessRequest> Resolve(const ToolchainProviderDescriptor &provider,
                                                                     const ToolchainInvocationIntent &intent) const = 0;
    };

    struct ToolchainProviderRegistryState;
    struct ToolchainProviderState;

    /** @brief Move-only provider publication that revokes and cancels work when released. */
    class ToolchainProviderRegistration final {
    public:
        ~ToolchainProviderRegistration() noexcept;
        ToolchainProviderRegistration(const ToolchainProviderRegistration &) = delete;
        ToolchainProviderRegistration &operator=(const ToolchainProviderRegistration &) = delete;
        ToolchainProviderRegistration(ToolchainProviderRegistration &&other) noexcept;
        ToolchainProviderRegistration &operator=(ToolchainProviderRegistration &&other);

        /** @brief Idempotently revokes future calls and requests cancellation of active processes. */
        void Reset();
        /** @brief Reports whether the exact provider generation remains callable. */
        [[nodiscard]] bool IsRegistered() const noexcept;
        /** @brief Returns a copyable identity token that remains subject to registry revocation checks. */
        [[nodiscard]] ToolchainInvocationAuthority Authority() const;

    private:
        friend class ToolchainProviderRegistry;
        ToolchainProviderRegistration(std::weak_ptr<ToolchainProviderRegistryState> registry,
                                      std::shared_ptr<ToolchainProviderState> provider) noexcept;

        std::weak_ptr<ToolchainProviderRegistryState> registry_;
        std::shared_ptr<ToolchainProviderState> provider_;
    };

    /** @brief Terminal platform result attributed to the exact provider generation and logical tool. */
    struct ToolchainInvocationResult final {
        ToolchainInvocationAuthority authority; /**< Exact contribution and generation used for resolution. */
        std::string providerId;                 /**< Exact module/provider identity used for resolution. */
        ToolchainToolId tool;                   /**< Logical tool requested by the provider. */
        ExternalProcessResult process;          /**< Terminal result returned by the platform boundary. */
    };

    /** @brief Explicit host-owned registry and policy-enforcing platform process gateway. */
    class ToolchainProviderRegistry final {
    public:
        static constexpr std::size_t MaximumProviders = 256;                   /**< Hard publication bound. */
        static constexpr std::size_t MaximumToolsPerProvider = 256;            /**< Hard declared-tool bound. */
        static constexpr std::size_t MaximumActiveInvocationsPerProvider = 16; /**< Hard concurrent-call bound. */
        static constexpr std::size_t MaximumArguments = 4096;                  /**< Hard requested-argument bound. */
        static constexpr std::size_t MaximumArgumentBytes = 1U << 20U;         /**< Hard requested-byte bound. */

        /**
         * @brief Creates a registry borrowing host policy and platform process authorities.
         * @param policy Host policy that owns all concrete process request fields.
         * @param processes Platform boundary used for every approved invocation.
         */
        ToolchainProviderRegistry(const IToolchainInvocationPolicy &policy, IExternalProcessRunner &processes);
        ~ToolchainProviderRegistry() noexcept;
        ToolchainProviderRegistry(const ToolchainProviderRegistry &) = delete;
        ToolchainProviderRegistry &operator=(const ToolchainProviderRegistry &) = delete;
        ToolchainProviderRegistry(ToolchainProviderRegistry &&) noexcept = default;
        ToolchainProviderRegistry &operator=(ToolchainProviderRegistry &&other);

        /**
         * @brief Publishes inert provider identity and allowed logical tools.
         * @param descriptor Exact provider publication.
         * @return Lifetime registration or a typed invalid, duplicate, capacity, or shutdown failure.
         */
        [[nodiscard]] Result<ToolchainProviderRegistration> Register(ToolchainProviderDescriptor descriptor);

        /**
         * @brief Resolves provider intent through host policy and executes it through the platform boundary.
         * @param authority Exact generation-bound provider authority.
         * @param intent Bounded logical invocation intent.
         * @param cancellation Caller cancellation inherited by the process operation.
         * @return Attributed terminal process result, or a typed validation, policy, launch, cancellation, or revocation failure.
         */
        [[nodiscard]] Result<ToolchainInvocationResult> Invoke(const ToolchainInvocationAuthority &authority,
                                                               const ToolchainInvocationIntent &intent,
                                                               const CancellationToken &cancellation) const;

        /** @brief Revokes all publications, cancels active processes, and closes admission. */
        void BeginShutdown();
        /** @brief Reports whether shutdown admission has closed. */
        [[nodiscard]] bool IsShutdown() const;

    private:
        std::shared_ptr<ToolchainProviderRegistryState> state_;
    };
}  // namespace Horo::Extensions
