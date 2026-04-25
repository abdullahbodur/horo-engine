#pragma once

#include "editor/SceneDocument.h"
#include "scene/SceneProjectModel.h"

namespace Monolith::Editor {
    // Converts between the editor-facing SceneDocument and the engine-owned typed
    // SceneProjectModel. Runtime-facing code should target SceneProjectModel.
    SceneProjectModel BuildSceneProjectModel(const SceneDocument &doc);

    SceneDocument BuildSceneDocument(const SceneProjectModel &model);
} // namespace Monolith::Editor
