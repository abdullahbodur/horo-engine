/**
 * @file SceneRuntimeCoordinatorBridge.h
 * @brief Bridge functions that drive SceneRuntimeCoordinator from SceneDocument values.
 */
#pragma once

#include "ui/editor/SceneDocument.h"
#include "scene/SceneRuntimeCoordinator.h"

namespace Horo::Editor {
    /**
     * @brief Performs an initial scene load into the runtime coordinator.
     * @param coordinator   The runtime coordinator that owns the active scene.
     * @param document      The editor document describing the scene to load.
     * @param applyCallback Callback invoked when the runtime scene is ready to apply.
     * @return Operation result indicating success or the failure reason.
     */
    SceneRuntimeOperationResult
    LoadSceneDocument(SceneRuntimeCoordinator &coordinator,
                      const SceneDocument &document,
                      const RuntimeSceneApplyCallback &applyCallback);

    /**
     * @brief Reloads a previously loaded scene into the runtime coordinator.
     * @param coordinator   The runtime coordinator that owns the active scene.
     * @param document      The updated editor document to reload from.
     * @param applyCallback Callback invoked when the runtime scene is ready to apply.
     * @return Operation result indicating success or the failure reason.
     */
    SceneRuntimeOperationResult
    ReloadSceneDocument(SceneRuntimeCoordinator &coordinator,
                        const SceneDocument &document,
                        const RuntimeSceneApplyCallback &applyCallback);
} // namespace Horo::Editor
