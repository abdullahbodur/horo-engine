#pragma once

#include "editor/SceneDocument.h"
#include "scene/RuntimeSceneDefinition.h"

namespace Monolith {
namespace Editor {

// Convenience bridge for the canonical authoring path:
// SceneDocument -> SceneProjectModel -> RuntimeSceneDefinition.
RuntimeSceneBuildResult BuildRuntimeSceneDefinition(const SceneDocument& doc);

}  // namespace Editor
}  // namespace Monolith
