#pragma once

/**
 * @file ProjectValidatorRegistry.h
 * @brief Host-owned project-validator extension point and deterministic result attribution.
 */

#include "Horo/Foundation/CancellationToken.h"
#include "Horo/Foundation/ErrorCodeRegistry.h"
#include "Horo/Foundation/Paths.h"
#include "Horo/Foundation/ValidationResult.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Horo::Extensions {
    /** @brief Project operation whose immutable inputs are being validated. */
    enum class ProjectValidationMode : std::uint8_t {
        ReadOnly,
        Edit,
        Build,
    };

    /** @brief One immutable project-relative resource borrowed for a synchronous validation call. */
    struct ProjectValidationResourceView final {
        ProjectPath path;                 /**< Normalized project-relative identity; never an absolute filesystem path. */
        std::span<const std::byte> bytes; /**< Immutable content valid for the complete synchronous registry call. */
    };

    /**
     * @brief Immutable project input exposed to validators without project mutation authority.
     * @note All string and span storage is borrowed and must remain valid until ValidateAll returns.
     */
    struct ProjectValidationSnapshot final {
        std::string_view projectId;                                  /**< Stable project identity, never a project root path. */
        ProjectValidationMode mode{ProjectValidationMode::ReadOnly}; /**< Exact operation-specific validation mode. */
        std::span<const ProjectValidationResourceView> resources;    /**< Strictly path-sorted unique resource views. */
    };

    /** @brief Stable typed identity of one project-validator contribution. */
    struct ProjectValidatorId final {
        std::string value;
        bool operator==(const ProjectValidatorId &) const noexcept = default;
    };

    /** @brief Stable typed identity of the module/provider owning a validator. */
    struct ProjectValidatorProviderId final {
        std::string value;
        bool operator==(const ProjectValidatorProviderId &) const noexcept = default;
    };

    /** @brief Stable identity and activation generation attributed to one validator contribution. */
    struct ProjectValidatorProviderDescriptor final {
        ProjectValidatorId validatorId;        /**< Canonical extension-point contribution identity. */
        ProjectValidatorProviderId providerId; /**< Canonical module/provider identity. */
        std::uint64_t providerGeneration{};    /**< Non-zero activation generation. */
    };

    class ProjectValidatorRegistry;

    /** @brief Host-owned bounded finding sink borrowed only during one provider callback. */
    class ProjectValidationFindingSink final {
    public:
        /**
         * @brief Adds one declared structured finding to the current provider result.
         * @param descriptor Exact error descriptor present in the host registry snapshot.
         * @param message Finding-specific detail, or empty to use the registered summary.
         * @param location Required project-relative source identity and optional line/column.
         * @return Success or a typed unknown-identity, invalid-source, capacity, or closed failure.
         */
        [[nodiscard]] Result<void> Add(const ErrorCodeDescriptor &descriptor, std::string message, SourceLocation location);

    private:
        friend class ProjectValidatorRegistry;
        explicit ProjectValidationFindingSink(ValidationResultBuilder builder) noexcept;
        [[nodiscard]] Result<ValidationResult> Complete();

        ValidationResultBuilder builder_;
    };

    /** @brief Trusted backend contribution invoked with immutable project input and cooperative cancellation. */
    class IProjectValidator {
    public:
        virtual ~IProjectValidator() = default;

        /**
         * @brief Validates one immutable snapshot without mutating project state.
         * @param snapshot Borrowed project-relative inputs valid for this synchronous call only.
         * @param findings Host-owned bounded structured finding sink.
         * @param cancellation Cooperative cancellation observed by provider work.
         * @return Success after a complete deterministic pass, or a typed provider failure.
         */
        [[nodiscard]] virtual Result<void> Validate(const ProjectValidationSnapshot &snapshot, ProjectValidationFindingSink &findings,
                                                    const CancellationToken &cancellation) const = 0;
    };

    struct ProjectValidatorRegistryState;
    struct ProjectValidatorProviderState;

    /** @brief Move-only registration whose lifetime controls future validator admission. */
    class ProjectValidatorRegistration final {
    public:
        ~ProjectValidatorRegistration() noexcept;
        ProjectValidatorRegistration(const ProjectValidatorRegistration &) = delete;
        ProjectValidatorRegistration &operator=(const ProjectValidatorRegistration &) = delete;
        ProjectValidatorRegistration(ProjectValidatorRegistration &&other) noexcept;
        ProjectValidatorRegistration &operator=(ProjectValidatorRegistration &&other);

        /** @brief Idempotently removes this exact provider generation from future validation calls. */
        void Reset();
        /**
         * @brief Reports whether the provider remains discoverable for new validation calls.
         * @return True only while this registration owns a live publication.
         */
        [[nodiscard]] bool IsRegistered() const noexcept;

    private:
        friend class ProjectValidatorRegistry;
        ProjectValidatorRegistration(std::weak_ptr<ProjectValidatorRegistryState> registry,
                                     std::shared_ptr<ProjectValidatorProviderState> provider) noexcept;

        std::weak_ptr<ProjectValidatorRegistryState> registry_;
        std::shared_ptr<ProjectValidatorProviderState> provider_;
    };

    /** @brief One completed immutable validation result attributed to an exact provider generation. */
    struct AttributedProjectValidationResult final {
        ProjectValidatorProviderDescriptor provider;
        ValidationResult result;
    };

    /** @brief Explicit composition-owned registry and synchronous dispatcher for project validators. */
    class ProjectValidatorRegistry final {
    public:
        static constexpr std::size_t MaximumProviders = 256;            /**< Hard provider publication bound. */
        static constexpr std::size_t MaximumResources = 65536;          /**< Hard resource-view bound per call. */
        static constexpr std::size_t MaximumProjectIdBytes = 256;       /**< Hard project identity byte bound. */
        static constexpr std::uint64_t MaximumInputBytes = 1ULL << 34U; /**< Hard aggregate borrowed-input bound. */

        /**
         * @brief Creates a host registry using one immutable declared-error snapshot.
         * @param errors Host-validated declared-error snapshot used for every finding.
         * @param findingLimits Host-owned per-provider finding bound.
         * @return Registry or a typed invalid-limit failure before publication is possible.
         */
        [[nodiscard]] static Result<ProjectValidatorRegistry> Create(ErrorCodeRegistry errors, ValidationResultLimits findingLimits = {});
        ~ProjectValidatorRegistry() noexcept;
        ProjectValidatorRegistry(const ProjectValidatorRegistry &) = delete;
        ProjectValidatorRegistry &operator=(const ProjectValidatorRegistry &) = delete;
        ProjectValidatorRegistry(ProjectValidatorRegistry &&) noexcept = default;
        ProjectValidatorRegistry &operator=(ProjectValidatorRegistry &&other);

        /**
         * @brief Publishes inert provider metadata and implementation from the composition root.
         * @param descriptor Exact contribution/provider identity and activation generation.
         * @param provider Shared provider implementation retained by admitted synchronous calls.
         * @return Lifetime registration or an invalid, duplicate, capacity, or shutdown failure.
         * @note Registration never invokes provider code.
         */
        [[nodiscard]] Result<ProjectValidatorRegistration> Register(ProjectValidatorProviderDescriptor descriptor,
                                                                    std::shared_ptr<const IProjectValidator> provider) const;

        /**
         * @brief Runs one immutable snapshot through the deterministically ordered provider set.
         * @param snapshot Borrowed immutable project-relative inputs valid until this call returns.
         * @param cancellation Cooperative cancellation checked before and after every provider callback.
         * @return Fully attributed results, or one stable provider/cancellation failure with no partial results.
         */
        [[nodiscard]] Result<std::vector<AttributedProjectValidationResult>> ValidateAll(const ProjectValidationSnapshot &snapshot,
                                                                                         const CancellationToken &cancellation) const;

        /** @brief Idempotently closes registration and new validation admission. */
        void BeginShutdown() const;
        /**
         * @brief Reports whether this registry has entered terminal shutdown.
         * @return True after shutdown admission closes.
         */
        [[nodiscard]] bool IsShutdown() const;

    private:
        explicit ProjectValidatorRegistry(ErrorCodeRegistry errors, ValidationResultLimits findingLimits);
        std::shared_ptr<ProjectValidatorRegistryState> state_;
    };
}  // namespace Horo::Extensions
