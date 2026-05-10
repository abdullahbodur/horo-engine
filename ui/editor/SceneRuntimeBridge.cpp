/**
 * @file SceneRuntimeBridge.cpp
 * @brief Thin adapter from @ref SceneDocument to @ref RuntimeSceneDefinition via @ref BuildSceneProjectModel.
 */
#include "ui/editor/SceneRuntimeBridge.h"

#include "ui/editor/SceneProjectBridge.h"
#include "scene/SceneRuntimeConversion.h"

namespace Horo::Editor {
    /** @copydoc BuildRuntimeSceneDefinition */
    RuntimeSceneBuildResult BuildRuntimeSceneDefinition(const SceneDocument &doc) {
        return Horo::BuildRuntimeSceneDefinition(BuildSceneProjectModel(doc));
    }
} // namespace Horo::Editor
