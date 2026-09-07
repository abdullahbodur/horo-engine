#include "Horo/Foundation/ErrorCode.h"

#include <algorithm>
#include <utility>

namespace Horo {
    /** @copydoc MakeError */
    Error MakeError(const ErrorCodeDescriptor &descriptor, std::string message) {
        if (message.empty())
            message.assign(descriptor.summary);

        return Error{
            .code = descriptor.code,
            .domain = descriptor.domain,
            .severity = descriptor.defaultSeverity,
            .message = std::move(message),
        };
    }

    /** @copydoc DiagnosticCodeForDeclaredError */
    std::optional<DiagnosticCode> DiagnosticCodeForDeclaredError(const Error &error, const std::string_view expectedDomain,
                                                                 const std::span<const ErrorCodeDescriptor *const> descriptors) {
        if (error.domain.Value() != expectedDomain)
            return std::nullopt;
        const auto descriptor = std::ranges::find_if(descriptors, [&](const ErrorCodeDescriptor *candidate) {
            return candidate != nullptr && candidate->domain.Value() == expectedDomain && error.code.Value() == candidate->code.Value();
        });
        if (descriptor == descriptors.end())
            return std::nullopt;
        return DiagnosticCode{std::string{(*descriptor)->code.Value()}};
    }

    /** @copydoc DiagnosticSeverityForError */
    std::optional<DiagnosticSeverity> DiagnosticSeverityForError(const ErrorSeverity severity) noexcept {
        DiagnosticSeverity diagnosticSeverity{};
        switch (severity) {
            case ErrorSeverity::Info:
                diagnosticSeverity = DiagnosticSeverity::Note;
                break;
            case ErrorSeverity::Warning:
                diagnosticSeverity = DiagnosticSeverity::Warning;
                break;
            case ErrorSeverity::Error:
                diagnosticSeverity = DiagnosticSeverity::Error;
                break;
            case ErrorSeverity::Critical:
                diagnosticSeverity = DiagnosticSeverity::Fatal;
                break;
            default:
                return std::nullopt;
        }
        return diagnosticSeverity;
    }
}  // namespace Horo
