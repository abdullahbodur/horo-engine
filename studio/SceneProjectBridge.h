#pragma once

#include "SceneDocument.h"
#include "scene/SceneProjectModel.h"

namespace Monolith {
namespace Editor {

// Converts between the editor-facing SceneDocument and the engine-owned typed
// SceneProjectModel. Runtime-facing code should target SceneProjectModel.
SceneProjectModel BuildSceneProjectModel(const SceneDocument& doc);
SceneDocument BuildSceneDocument(const SceneProjectModel& model);

}  // namespace Editor
}  // namespace Monolith
