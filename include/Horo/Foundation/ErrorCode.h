#pragma once

/**
 * @file ErrorCode.h
 * @brief Stable error identities, descriptors, and typed error values.
 */

#include "Horo/Foundation/Diagnostics.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Horo {
    struct Error;

    /** @brief Stable machine-readable error code. Callers branch on this value, never message text. */
    class ErrorCode {
    public:
        ErrorCode() = default;

        explicit ErrorCode(std::string value) : m_value(std::move(value)) {}

        [[nodiscard]] const std::string &Value() const noexcept {
            return m_value;
        }

    private:
        std::string m_value;
    };

    /** @brief Stable module-owned error namespace. */
    class ErrorDomainId {
    public:
        ErrorDomainId() = default;

        explicit ErrorDomainId(std::string value) : m_value(std::move(value)) {}

        [[nodiscard]] const std::string &Value() const noexcept {
            return m_value;
        }

    private:
        std::string m_value;
    };

    enum class ErrorSeverity : std::uint8_t {
        Info,
        Warning,
        Error,
        Critical
    };

    /** @brief Immutable optional reference to an owned typed error cause. */
    class ErrorCause {
    public:
        /** @brief Creates an empty cause reference without allocating storage. */
        ErrorCause() noexcept = default;

        /**
         * @brief Reports whether this reference owns a cause.
         * @return True when Get() returns a typed cause.
         */
        [[nodiscard]] explicit operator bool() const noexcept;

        /**
         * @brief Returns the immutable typed cause.
         * @return Borrowed cause pointer, or nullptr when this reference is empty.
         */
        [[nodiscard]] const Error *Get() const noexcept;

    private:
        explicit ErrorCause(std::shared_ptr<const Error> cause) noexcept;

        std::shared_ptr<const Error> cause_;

        friend Error WithCause(Error outer, Error cause);
    };

    /** @brief Typed expected failure with human-facing diagnostic detail and an immutable cause chain. */
    struct Error {
        ErrorCode code;
        ErrorDomainId domain;
        ErrorSeverity severity = ErrorSeverity::Error;
        std::string message;
        std::vector<Diagnostic> diagnostics;
        ErrorCause cause;
    };

    /** @brief Immutable declaration of one stable module-owned error identity and its presentation defaults. */
    struct ErrorCodeDescriptor {
        ErrorDomainId domain;                  /**< Stable namespace owned by the declaring module. */
        ErrorCode code;                        /**< Stable machine-readable failure classification. */
        ErrorSeverity defaultSeverity;         /**< Severity used unless an owning boundary overrides it. */
        std::string_view summary;              /**< Developer-facing fallback message. */
        std::string_view remediationHint;      /**< Short safe remediation guidance for presentation adapters. */
        bool retryable = false;                /**< Whether retry is valid without changing the request. */
        bool userActionable = false;           /**< Whether a user can normally resolve the failure. */
        std::optional<ErrorCode> deprecatedBy; /**< Replacement code when this identity is deprecated. */
    };

    /**
     * @brief Creates an error from a predeclared module-owned descriptor.
     * @param descriptor Stable descriptor registered by the owning module.
     * @param message Optional operation-specific context; the descriptor summary is used when empty.
     * @return Error carrying the descriptor identity and default severity.
     */
    [[nodiscard]] Error MakeError(const ErrorCodeDescriptor &descriptor, std::string message = {});

    /**
     * @brief Attaches one immutable typed cause to an outer error.
     * @param outer Context error that remains the operation's returned identity.
     * @param cause Original typed failure preserved as the next chain node.
     * @return Outer error owning the immutable cause chain.
     * @note Allocates exactly one cause node. Copying the returned Error shares immutable cause nodes without cloning them.
     * @throws std::bad_alloc When storage for the cause node cannot be allocated.
     */
    [[nodiscard]] Error WithCause(Error outer, Error cause);

    /**
     * @brief Creates a declared context error around an existing typed failure.
     * @param descriptor Stable descriptor for the outer operation boundary.
     * @param cause Original typed failure preserved without flattening.
     * @param message Optional boundary context; the descriptor summary is used when empty.
     * @return Declared outer error owning the immutable cause chain.
     * @throws std::bad_alloc When storage for the error text or cause node cannot be allocated.
     */
    [[nodiscard]] Error WrapError(const ErrorCodeDescriptor &descriptor, Error cause, std::string message = {});

    /**
     * @brief Finds an exact typed identity anywhere in an error cause chain.
     * @param error Outermost error to inspect.
     * @param domain Exact stable domain identity.
     * @param code Exact stable error code identity.
     * @return True when the outer error or one of its causes matches both values.
     */
    [[nodiscard]] bool ErrorChainContains(const Error &error, const ErrorDomainId &domain, const ErrorCode &code) noexcept;

    /**
     * @brief Maps one error to diagnostic identity only when it matches an explicitly declared descriptor.
     * @param error Candidate operation error.
     * @param expectedDomain Exact owning domain required for both the error and matched descriptor.
     * @param descriptors Canonical descriptor set admitted by the diagnostic contract.
     * @return Owned diagnostic code for an exact declared match, or no value for foreign, unknown or malformed input.
     */
    [[nodiscard]] std::optional<DiagnosticCode> DiagnosticCodeForDeclaredError(const Error &error, std::string_view expectedDomain,
                                                                               std::span<const ErrorCodeDescriptor *const> descriptors);

    /**
     * @brief Converts the complete Foundation error severity vocabulary to diagnostic severity.
     * @param severity Error severity to convert.
     * @return Exact diagnostic severity, or no value for an unknown enum representation.
     */
    [[nodiscard]] std::optional<DiagnosticSeverity> DiagnosticSeverityForError(ErrorSeverity severity) noexcept;

    /** @brief Event published over DataBus whenever a new typed error is reported. */
    struct ErrorPublishedEvent {
        static constexpr std::string_view HoroEventTypeName = "horo.foundation.ErrorPublishedEvent";
        Error error;
    };
}  // namespace Horo
