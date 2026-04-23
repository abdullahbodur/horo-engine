#include "SceneRuntimeBridge.h"

#include "SceneProjectBridge.h"
#include "scene/SceneRuntimeConversion.h"

namespace Monolith {
namespace Editor {

RuntimeSceneBuildResult BuildRuntimeSceneDefinition(const SceneDocument& doc) {
  return Monolith::BuildRuntimeSceneDefinition(BuildSceneProjectModel(doc));
}

}  // namespace Editor
}  // namespace Monolith
