#pragma once

#include "editor/SceneDocument.h"
#include "scene/SceneRuntimeCoordinator.h"

namespace Monolith {
namespace Editor {

SceneRuntimeOperationResult LoadSceneDocument(SceneRuntimeCoordinator& coordinator,
                                              const SceneDocument& document,
                                              const RuntimeSceneApplyCallback& applyCallback);

SceneRuntimeOperationResult ReloadSceneDocument(SceneRuntimeCoordinator& coordinator,
                                                const SceneDocument& document,
                                                const RuntimeSceneApplyCallback& applyCallback);

}  // namespace Editor
}  // namespace Monolith
