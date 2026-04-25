#include "editor/SceneRuntimeBridge.h"

#include "editor/SceneProjectBridge.h"
#include "scene/SceneRuntimeConversion.h"

namespace Monolith::Editor {
    RuntimeSceneBuildResult BuildRuntimeSceneDefinition(const SceneDocument &doc) {
        return Monolith::BuildRuntimeSceneDefinition(BuildSceneProjectModel(doc));
    }
} // namespace Monolith::Editor
