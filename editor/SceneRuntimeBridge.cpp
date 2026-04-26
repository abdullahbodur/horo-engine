#include "editor/SceneRuntimeBridge.h"

#include "editor/SceneProjectBridge.h"
#include "scene/SceneRuntimeConversion.h"

namespace Horo::Editor {
    RuntimeSceneBuildResult BuildRuntimeSceneDefinition(const SceneDocument &doc) {
        return Horo::BuildRuntimeSceneDefinition(BuildSceneProjectModel(doc));
    }
} // namespace Horo::Editor
