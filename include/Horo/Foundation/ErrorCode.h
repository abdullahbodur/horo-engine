#pragma once

#include "Horo/Foundation/Diagnostics.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Horo
{
    /** @brief Stable machine-readable error code. Callers branch on this value, never message text. */
    class ErrorCode
    {
    public:
        explicit ErrorCode(std::string value) : m_value(std::move(value)) {}
        [[nodiscard]] const std::string &Value() const noexcept { return m_value; }
    private:
        std::string m_value;
    };

    /** @brief Stable module-owned error namespace. */
    class ErrorDomainId
    {
    public:
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
} // namespace Horo
