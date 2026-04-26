#pragma once

#include "editor/SceneDocument.h"
#include "scene/RuntimeSceneDefinition.h"

namespace Horo::Editor {
    // Convenience bridge for the canonical authoring path:
    // SceneDocument -> SceneProjectModel -> RuntimeSceneDefinition.
    RuntimeSceneBuildResult BuildRuntimeSceneDefinition(const SceneDocument &doc);
} // namespace Horo::Editor
