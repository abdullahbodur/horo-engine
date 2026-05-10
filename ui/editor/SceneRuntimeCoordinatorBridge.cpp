/**
 * @file SceneRuntimeCoordinatorBridge.cpp
 * @brief Bridges @ref SceneDocument load/reload into @ref SceneRuntimeCoordinator with build validation.
 *
 * Builds a @ref RuntimeSceneDefinition via @ref BuildRuntimeSceneDefinition and forwards to the coordinator,
 * returning structured failures when the build reports errors.
 */
#include "ui/editor/SceneRuntimeCoordinatorBridge.h"

#include <sstream>

#include "ui/editor/SceneRuntimeBridge.h"

namespace Horo::Editor {
    namespace {
        /**
         * @brief Builds a compact error summary from runtime scene build issues.
         * @param buildResult Build output that may contain validation issues.
         * @return Pipe-delimited error list, or empty when no build errors exist.
         */
        std::string SummarizeBuildIssues(const RuntimeSceneBuildResult &buildResult) {
            std::ostringstream stream;
            bool first = true;
            for (const auto &issue: buildResult.issues) {
                if (issue.severity != RuntimeSceneBuildIssue::Severity::Error)
                    continue;
                if (!first)
                    stream << " | ";
                first = false;
                stream << issue.path << ": " << issue.message;
            }
            return stream.str();
        }

        /**
         * @brief Creates a failed runtime operation result from build diagnostics.
         * @param operation Runtime operation that failed to start.
         * @param state Current lifecycle state when the failure happened.
         * @param buildResult Build output used to derive the failure message.
         * @return Failed operation result populated with an error summary.
         */
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

    /**
     * @copydoc LoadSceneDocument
     */
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

    /**
     * @copydoc ReloadSceneDocument
     */
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
} // namespace Horo::Editor
