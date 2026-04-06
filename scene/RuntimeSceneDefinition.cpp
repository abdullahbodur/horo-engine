#include "scene/RuntimeSceneDefinition.h"

namespace Monolith {

bool RuntimeSceneBuildResult::HasErrors() const {
  for (const auto& issue : issues) {
    if (issue.severity == RuntimeSceneBuildIssue::Severity::Error)
      return true;
  }
  return false;
}

std::size_t RuntimeSceneBuildResult::ErrorCount() const {
  std::size_t count = 0;
  for (const auto& issue : issues) {
    if (issue.severity == RuntimeSceneBuildIssue::Severity::Error)
      ++count;
  }
  return count;
}

std::size_t RuntimeSceneBuildResult::WarningCount() const {
  std::size_t count = 0;
  for (const auto& issue : issues) {
    if (issue.severity == RuntimeSceneBuildIssue::Severity::Warning)
      ++count;
  }
  return count;
}

}  // namespace Monolith
