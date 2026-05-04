#include "ui/editor/SceneRuntimeBridge.h"

#include "ui/editor/SceneProjectBridge.h"
#include "scene/SceneRuntimeConversion.h"

namespace Horo::Editor {
    RuntimeSceneBuildResult BuildRuntimeSceneDefinition(const SceneDocument &doc) {
        return Horo::BuildRuntimeSceneDefinition(BuildSceneProjectModel(doc));
    }
} // namespace Horo::Editor
