#pragma once

/**
 * @file CompilerDiagnosticParser.h
 * @brief Bounded parsing of supported native compiler diagnostic formats.
 */

#include "Horo/Foundation/BuildOutputStore.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace Horo::Application {
    inline constexpr std::size_t MaximumCompilerDiagnosticLineBytes = 16U * 1024U;
    inline constexpr std::size_t MaximumCompilerDiagnosticMessageBytes = 8U * 1024U;

    /** @brief Supported compiler-output syntax recognized by the parser. */
    enum class CompilerDiagnosticFormat : std::uint8_t {
        ClangOrGcc,
        Msvc,
    };

    /** @brief Typed compiler diagnostic extracted from one bounded process-output line. */
    struct CompilerDiagnostic {
        CompilerDiagnosticFormat format{CompilerDiagnosticFormat::ClangOrGcc};
        DiagnosticSeverity severity{DiagnosticSeverity::Note};
        DiagnosticSourceLocation source;
        std::string message;
        std::optional<std::string> compilerCode;
        bool inputTruncated{false};
        bool messageTruncated{false};
    };

    /**
     * @brief Parses one bounded GCC, Clang, or MSVC compiler-output line.
     * @param line Raw compiler-output line without a presentation suffix.
     * @param projectRoot Project root used to make relative source paths absolute.
     * @param inputTruncated Whether the process runner truncated the original line.
     * @return A typed diagnostic, or no value for malformed, unsupported, or oversized input.
     * @details Source paths are normalized but intentionally not checked for project-root
     *          containment; the navigation layer owns that validation.
     */
    [[nodiscard]] std::optional<CompilerDiagnostic> ParseCompilerDiagnostic(std::string_view line, const std::filesystem::path &projectRoot,
                                                                            bool inputTruncated = false);
}  // namespace Horo::Application
