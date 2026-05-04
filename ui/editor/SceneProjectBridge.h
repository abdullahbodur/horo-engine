#pragma once

#include "ui/editor/SceneDocument.h"
#include "scene/SceneProjectModel.h"

namespace Horo::Editor {
    // Converts between the editor-facing SceneDocument and the engine-owned typed
    // SceneProjectModel. Runtime-facing code should target SceneProjectModel.
    SceneProjectModel BuildSceneProjectModel(const SceneDocument &doc);

    SceneDocument BuildSceneDocument(const SceneProjectModel &model);
} // namespace Horo::Editor
