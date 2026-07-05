/** @file BuildDiagnostics.h
 *  @brief Build failure classification, secret redaction, and diagnostic
 *         rendering for the release pipeline.
 *
 *  Provides a lightweight classification engine (DiagnoseBuildFailure) that
 *  inspects exit codes and log output to identify the failing build stage,
 *  extract a relevant log excerpt, and produce a human-readable suggestion.
 *  RedactSecrets strips credential-bearing strings before log output is
 *  surfaced in the CLI or editor UI.
 */
#pragma once
#include <string>

namespace Horo::Build {

/** @brief Identifies the build step where a failure occurred.
 *
 *  Used by DiagnoseBuildFailure to classify the failing stage from exit
 *  codes and log patterns.  Unknown is the fallback when no pattern matches. */
enum class BuildStage {
    Unknown,    /**< No stage could be determined. */
    Configure,  /**< CMake configure or dependency resolution step. */
    Compile,    /**< Compiler invocation (cl.exe, gcc, clang). */
    Link,       /**< Linker step. */
    Package,    /**< Artifact packaging or archive creation. */
    Sign,       /**< Code-signing step. */
    Upload,     /**< Artifact upload or distribution step. */
};

/** @brief Structured diagnostic produced when a build job fails.
 *
 *  Carries the classified stage, the original exit code, a human-readable
 *  suggestion, and a redacted excerpt of the failing log output.  Every
 *  string in this struct is safe to display directly — logExcerpt has
 *  already been passed through RedactSecrets. */
struct BuildFailureDiagnostic {
    BuildStage stage = BuildStage::Unknown; /**< Classified failing stage. */
    int exitCode = 0;                       /**< Original process exit code. */
    std::string suggestion;                 /**< Human-readable remediation hint. */
    std::string logExcerpt;                 /**< Redacted excerpt from the failing log (last ~40 lines, max 50). */
};

/** @brief Classifies a build failure from exit code and log output.
 *
 *  Pattern-matches the log and error strings against known compiler/linker/
 *  toolchain signatures to determine the failing BuildStage.  Falls back to
 *  exit-code heuristics when log matching is inconclusive.
 *
 *  The returned diagnostic's logExcerpt is already redacted via
 *  RedactSecrets — the caller does not need to redact again.
 *
 *  @param exitCode  Process exit code (0 = success in most toolchains).
 *  @param log       Captured stdout from the build process.
 *  @param error     Captured stderr or error message from the build process.
 *  @return          Populated diagnostic with stage, suggestion, and redacted excerpt. */
BuildFailureDiagnostic DiagnoseBuildFailure(int exitCode,
                                            const std::string& log,
                                            const std::string& error);

/** @brief Strips credential-bearing patterns from a string.
 *
 *  Redacts secrets that may appear in build logs: command-line arguments
 *  (--password, -p, /p), environment-variable assignments, and known
 *  credential patterns.  The redaction is conservative — it replaces
 *  matched secrets with a fixed placeholder.
 *
 *  @param input  Raw string that may contain secrets.
 *  @return       Copy of the input with detected secrets replaced by "***". */
std::string RedactSecrets(const std::string& input);

/** @brief Returns the display label for a build stage.
 *
 *  @param stage  The stage to label.
 *  @return       Null-terminated display string (e.g. "Compile", "Link"). */
const char* GetBuildStageLabel(BuildStage stage);

} // namespace Horo::Build
