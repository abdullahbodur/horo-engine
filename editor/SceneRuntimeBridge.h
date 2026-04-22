#pragma once

#include "editor/SceneDocument.h"
#include "scene/RuntimeSceneDefinition.h"

namespace Monolith::Editor {
    // Convenience bridge for the canonical authoring path:
    // SceneDocument -> SceneProjectModel -> RuntimeSceneDefinition.
    RuntimeSceneBuildResult BuildRuntimeSceneDefinition(const SceneDocument &doc);
} // namespace Monolith::Editor
