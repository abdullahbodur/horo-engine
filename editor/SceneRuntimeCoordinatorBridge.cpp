#include "editor/SceneRuntimeCoordinatorBridge.h"

#include <sstream>

#include "editor/SceneRuntimeBridge.h"

namespace Monolith::Editor {
namespace {
std::string SummarizeBuildIssues(const RuntimeSceneBuildResult &buildResult) {
  std::ostringstream stream;
  bool first = true;
  for (const auto &issue : buildResult.issues) {
    if (issue.severity != RuntimeSceneBuildIssue::Severity::Error)
      continue;
    if (!first)
      stream << " | ";
    first = false;
    stream << issue.path << ": " << issue.message;
  }
  return stream.str();
}

SceneRuntimeOperationResult
MakeBuildFailure(SceneRuntimeOperation operation, SceneLifecycleState state,
                 const RuntimeSceneBuildResult &buildResult) {
  SceneRuntimeOperationResult result;
  result.ok = false;
  result.operation = operation;
  result.state = state;
  result.error = SummarizeBuildIssues(buildResult);
  if (result.error.empty())
    result.error = "Runtime scene build failed.";
  return result;
}
} // namespace

SceneRuntimeOperationResult
LoadSceneDocument(SceneRuntimeCoordinator &coordinator,
                  const SceneDocument &document,
                  const RuntimeSceneApplyCallback &applyCallback) {
  const RuntimeSceneBuildResult buildResult =
      BuildRuntimeSceneDefinition(document);
  if (buildResult.HasErrors()) {
    return MakeBuildFailure(SceneRuntimeOperation::Load,
                            coordinator.GetLifecycle().GetState(), buildResult);
  }
  return coordinator.Load(buildResult.definition, applyCallback);
}

SceneRuntimeOperationResult
ReloadSceneDocument(SceneRuntimeCoordinator &coordinator,
                    const SceneDocument &document,
                    const RuntimeSceneApplyCallback &applyCallback) {
  const RuntimeSceneBuildResult buildResult =
      BuildRuntimeSceneDefinition(document);
  if (buildResult.HasErrors()) {
    return MakeBuildFailure(SceneRuntimeOperation::Reload,
                            coordinator.GetLifecycle().GetState(), buildResult);
  }
  return coordinator.Reload(buildResult.definition, applyCallback);
}
} // namespace Monolith::Editor
