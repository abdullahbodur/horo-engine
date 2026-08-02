#pragma once

#include <string>

namespace Horo
{
    /** @brief Stable diagnostic identity used for validation and host presentation. */
    class DiagnosticCode
    {
    public:
        DiagnosticCode() = default;
        explicit DiagnosticCode(std::string value) : m_value(std::move(value)) {}
        [[nodiscard]] const std::string &Value() const noexcept { return m_value; }
    private:
        std::string m_value;
    };

    enum class DiagnosticSeverity : std::uint8_t { Note, Warning, Error };

    /** @brief A source location within a user-visible input. */
    struct SourceLocation
    {
        std::string source;
        std::uint32_t line = 0;
        std::uint32_t column = 0;
    };

    /** @brief Structured, non-fatal validation finding. */
    struct Diagnostic
    {
        DiagnosticCode code;
        DiagnosticSeverity severity = DiagnosticSeverity::Error;
        std::string message;
        SourceLocation location;
    };

    /** @brief Event published over DataBus whenever a new diagnostic finding is reported. */
    struct DiagnosticPublishedEvent
    {
        static constexpr std::string_view HoroEventTypeName = "horo.foundation.DiagnosticPublishedEvent";
        Diagnostic diagnostic;
    };
} // namespace Horo
