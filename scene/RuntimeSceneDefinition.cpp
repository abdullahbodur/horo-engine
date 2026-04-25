#include "scene/RuntimeSceneDefinition.h"

#include <algorithm>

namespace Monolith {
bool RuntimeSceneBuildResult::HasErrors() const {
  return std::ranges::any_of(issues, [](const auto &issue) {
    return issue.severity == RuntimeSceneBuildIssue::Severity::Error;
  });
}

std::size_t RuntimeSceneBuildResult::ErrorCount() const {
  std::size_t count = 0;
  for (const auto &issue : issues) {
    if (issue.severity == RuntimeSceneBuildIssue::Severity::Error)
      ++count;
  }
  return count;
}

std::size_t RuntimeSceneBuildResult::WarningCount() const {
  std::size_t count = 0;
  for (const auto &issue : issues) {
    if (issue.severity == RuntimeSceneBuildIssue::Severity::Warning)
      ++count;
  }
  return count;
}
} // namespace Monolith
