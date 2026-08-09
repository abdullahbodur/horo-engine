#pragma once

/**
 * @file ErrorCode.h
 * @brief Stable error identities, descriptors, and typed error values.
 */

#include "Horo/Foundation/Diagnostics.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Horo
{
    /** @brief Stable machine-readable error code. Callers branch on this value, never message text. */
    class ErrorCode
    {
    public:
        ErrorCode() = default;
        explicit ErrorCode(std::string value) : m_value(std::move(value)) {}
        [[nodiscard]] const std::string &Value() const noexcept { return m_value; }
    private:
        std::string m_value;
    };

    /** @brief Stable module-owned error namespace. */
    class ErrorDomainId
    {
    public:
        ErrorDomainId() = default;
        explicit ErrorDomainId(std::string value) : m_value(std::move(value)) {}
        [[nodiscard]] const std::string &Value() const noexcept { return m_value; }
    private:
        std::string m_value;
    };

    enum class ErrorSeverity : std::uint8_t { Info, Warning, Error, Critical };

    /** @brief Typed expected failure with human-facing diagnostic detail. */
    struct Error
    {
        ErrorCode code;
        ErrorDomainId domain;
        ErrorSeverity severity = ErrorSeverity::Error;
        std::string message;
        std::vector<Diagnostic> diagnostics;
    };

    /** @brief Immutable declaration of one stable module-owned error identity and its presentation defaults. */
    struct ErrorCodeDescriptor
    {
        ErrorDomainId domain;                       /**< Stable namespace owned by the declaring module. */
        ErrorCode code;                             /**< Stable machine-readable failure classification. */
        ErrorSeverity defaultSeverity;              /**< Severity used unless an owning boundary overrides it. */
        std::string_view summary;                   /**< Developer-facing fallback message. */
        std::string_view remediationHint;           /**< Short safe remediation guidance for presentation adapters. */
        bool retryable = false;                     /**< Whether retry is valid without changing the request. */
        bool userActionable = false;                /**< Whether a user can normally resolve the failure. */
        std::optional<ErrorCode> deprecatedBy;      /**< Replacement code when this identity is deprecated. */
    };

    /**
     * @brief Creates an error from a predeclared module-owned descriptor.
     * @param descriptor Stable descriptor registered by the owning module.
     * @param message Optional operation-specific context; the descriptor summary is used when empty.
     * @return Error carrying the descriptor identity and default severity.
     */
    [[nodiscard]] Error MakeError(const ErrorCodeDescriptor &descriptor, std::string message = {});

    /** @brief Event published over DataBus whenever a new typed error is reported. */
    struct ErrorPublishedEvent
    {
        static constexpr std::string_view HoroEventTypeName = "horo.foundation.ErrorPublishedEvent";
        Error error;
    };
} // namespace Horo
