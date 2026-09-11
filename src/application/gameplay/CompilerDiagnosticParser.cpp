#include "Horo/Application/CompilerDiagnosticParser.h"

#include <algorithm>
#include <charconv>
#include <system_error>

namespace Horo::Application {
    namespace {
        struct SeverityMatch {
            std::size_t position{};
            DiagnosticSeverity severity{DiagnosticSeverity::Note};
            std::string_view marker;
        };

        [[nodiscard]] std::optional<std::uint32_t> ParseCoordinate(const std::string_view digits) noexcept {
            if (digits.empty())
                return std::nullopt;
            std::uint32_t value{};
            const auto [end, error] = std::from_chars(digits.data(), digits.data() + digits.size(), value);
            if (error != std::errc{} || end != digits.data() + digits.size() || value == 0U)
                return std::nullopt;
            return value;
        }

        [[nodiscard]] std::optional<SeverityMatch> FindSeverity(const std::string_view line, const std::string_view errorMarker,
                                                                const std::string_view warningMarker) noexcept {
            const std::size_t error = line.find(errorMarker);
            const std::size_t warning = line.find(warningMarker);
            if (error == std::string_view::npos && warning == std::string_view::npos)
                return std::nullopt;
            if (error != std::string_view::npos && (warning == std::string_view::npos || error < warning))
                return SeverityMatch{error, DiagnosticSeverity::Error, errorMarker};
            return SeverityMatch{warning, DiagnosticSeverity::Warning, warningMarker};
        }

        [[nodiscard]] std::optional<DiagnosticSourceLocation> MakeSource(const std::string_view pathText, const std::uint32_t line,
                                                                         const std::uint32_t column,
                                                                         const std::filesystem::path &projectRoot) {
            if (pathText.empty())
                return std::nullopt;
            std::filesystem::path source{pathText};
            std::error_code error;
            if (source.is_relative())
                source = std::filesystem::absolute(projectRoot / source, error);
            if (error || !source.is_absolute())
                return std::nullopt;
            return DiagnosticSourceLocation{source.lexically_normal().string(), line, column};
        }

        void SetMessage(CompilerDiagnostic &diagnostic, const std::string_view message) {
            const std::size_t retained = std::min(message.size(), MaximumCompilerDiagnosticMessageBytes);
            diagnostic.message.assign(message.data(), retained);
            diagnostic.messageTruncated = message.size() > retained;
        }

        [[nodiscard]] std::optional<CompilerDiagnostic> ParseClangOrGcc(const std::string_view input,
                                                                        const std::filesystem::path &projectRoot,
                                                                        const bool inputTruncated) {
            const auto severity = FindSeverity(input, ": error: ", ": warning: ");
            if (!severity.has_value())
                return std::nullopt;
            const std::string_view location = input.substr(0, severity->position);
            const std::size_t columnSeparator = location.rfind(':');
            if (columnSeparator == std::string_view::npos)
                return std::nullopt;
            const std::size_t lineSeparator = location.rfind(':', columnSeparator - 1U);
            if (lineSeparator == std::string_view::npos)
                return std::nullopt;
            const auto line = ParseCoordinate(location.substr(lineSeparator + 1U, columnSeparator - lineSeparator - 1U));
            const auto column = ParseCoordinate(location.substr(columnSeparator + 1U));
            if (!line.has_value() || !column.has_value())
                return std::nullopt;
            const auto source = MakeSource(location.substr(0, lineSeparator), *line, *column, projectRoot);
            if (!source.has_value())
                return std::nullopt;

            CompilerDiagnostic result{.format = CompilerDiagnosticFormat::ClangOrGcc,
                                      .severity = severity->severity,
                                      .source = *source,
                                      .inputTruncated = inputTruncated};
            const std::string_view message = input.substr(severity->position + severity->marker.size());
            SetMessage(result, message);
            const std::size_t codeBegin = message.rfind(" [-");
            if (codeBegin != std::string_view::npos && message.ends_with(']'))
                result.compilerCode = std::string{message.substr(codeBegin + 2U, message.size() - codeBegin - 3U)};
            return result;
        }

        [[nodiscard]] std::optional<CompilerDiagnostic> ParseMsvc(const std::string_view input, const std::filesystem::path &projectRoot,
                                                                  const bool inputTruncated) {
            const auto severity = FindSeverity(input, "): error ", "): warning ");
            if (!severity.has_value())
                return std::nullopt;
            const std::size_t open = input.rfind('(', severity->position);
            if (open == std::string_view::npos || open == 0U)
                return std::nullopt;
            const std::string_view coordinates = input.substr(open + 1U, severity->position - open - 1U);
            const std::size_t separator = coordinates.find(',');
            if (separator == std::string_view::npos || coordinates.find(',', separator + 1U) != std::string_view::npos)
                return std::nullopt;
            const auto line = ParseCoordinate(coordinates.substr(0, separator));
            const auto column = ParseCoordinate(coordinates.substr(separator + 1U));
            if (!line.has_value() || !column.has_value())
                return std::nullopt;
            const auto source = MakeSource(input.substr(0, open), *line, *column, projectRoot);
            if (!source.has_value())
                return std::nullopt;

            CompilerDiagnostic result{.format = CompilerDiagnosticFormat::Msvc,
                                      .severity = severity->severity,
                                      .source = *source,
                                      .inputTruncated = inputTruncated};
            const std::string_view body = input.substr(severity->position + severity->marker.size());
            const std::size_t codeSeparator = body.find(": ");
            std::string_view message = body;
            if (codeSeparator != std::string_view::npos) {
                const std::string_view code = body.substr(0, codeSeparator);
                if (!code.empty() &&
                    code.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789") == std::string_view::npos) {
                    result.compilerCode = std::string{code};
                    message = body.substr(codeSeparator + 2U);
                }
            }
            SetMessage(result, message);
            return result;
        }
    }  // namespace

    /** @copydoc ParseCompilerDiagnostic */
    std::optional<CompilerDiagnostic> ParseCompilerDiagnostic(const std::string_view line, const std::filesystem::path &projectRoot,
                                                              const bool inputTruncated) {
        if (line.empty() || line.size() > MaximumCompilerDiagnosticLineBytes)
            return std::nullopt;
        if (auto diagnostic = ParseMsvc(line, projectRoot, inputTruncated); diagnostic.has_value())
            return diagnostic;
        return ParseClangOrGcc(line, projectRoot, inputTruncated);
    }
}  // namespace Horo::Application
