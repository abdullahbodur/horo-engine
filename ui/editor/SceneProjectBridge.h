/**
 * @file SceneProjectBridge.h
 * @brief Conversion between the editor SceneDocument and the engine's typed SceneProjectModel.
 */
#pragma once

#include "ui/editor/SceneDocument.h"
#include "scene/SceneProjectModel.h"

namespace Horo::Editor {
    /**
     * @brief Converts an editor SceneDocument to the engine-owned typed SceneProjectModel.
     * @param doc Source editor scene document.
     * @return Fully constructed SceneProjectModel ready for runtime use.
     * @note Runtime-facing code should target SceneProjectModel, not SceneDocument.
     */
    SceneProjectModel BuildSceneProjectModel(const SceneDocument &doc);

    /**
     * @brief Converts a SceneProjectModel back into an editor SceneDocument.
     * @param model Source engine scene model.
     * @return Editor-facing SceneDocument reflecting the model's state.
     */
    SceneDocument BuildSceneDocument(const SceneProjectModel &model);
} // namespace Horo::Editor
