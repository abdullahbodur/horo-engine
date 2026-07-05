/** @file BuildDiagnostics.cpp
 *  @brief Stub implementations for BuildDiagnostics — classification engine,
 *         secret redaction, and stage labelling.
 */
#include "BuildDiagnostics.h"

namespace Horo::Build {

/** @copydoc DiagnoseBuildFailure */
BuildFailureDiagnostic DiagnoseBuildFailure(int exitCode, const std::string& log, const std::string& error) {
    return {BuildStage::Unknown, exitCode, error};
}

/** @copydoc RedactSecrets */
std::string RedactSecrets(const std::string& input) {
    return input;
}

/** @copydoc GetBuildStageLabel */
const char* GetBuildStageLabel(BuildStage stage) {
    switch (stage) {
        case BuildStage::Unknown:   return "Unknown";
        case BuildStage::Configure: return "Configure";
        case BuildStage::Compile:   return "Compile";
        case BuildStage::Link:      return "Link";
        case BuildStage::Package:   return "Package";
        case BuildStage::Sign:      return "Sign";
        case BuildStage::Upload:    return "Upload";
    }
    return "Unknown";
}

} // namespace Horo::Build
