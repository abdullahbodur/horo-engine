#pragma once

#include "ui/editor/SceneDocument.h"
#include "scene/SceneRuntimeCoordinator.h"

namespace Horo::Editor {
    SceneRuntimeOperationResult
    LoadSceneDocument(SceneRuntimeCoordinator &coordinator,
                      const SceneDocument &document,
                      const RuntimeSceneApplyCallback &applyCallback);

    SceneRuntimeOperationResult
    ReloadSceneDocument(SceneRuntimeCoordinator &coordinator,
                        const SceneDocument &document,
                        const RuntimeSceneApplyCallback &applyCallback);
} // namespace Horo::Editor
