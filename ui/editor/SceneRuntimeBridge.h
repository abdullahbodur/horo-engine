/**
 * @file SceneRuntimeBridge.h
 * @brief Convenience bridge from SceneDocument to RuntimeSceneDefinition.
 */
#pragma once

#include "ui/editor/SceneDocument.h"
#include "scene/RuntimeSceneDefinition.h"

namespace Horo::Editor {
    /**
     * @brief Converts a SceneDocument to a RuntimeSceneDefinition via the canonical
     *        authoring pipeline: SceneDocument → SceneProjectModel → RuntimeSceneDefinition.
     * @param doc The editor scene document to convert.
     * @return The build result containing the RuntimeSceneDefinition and any diagnostics.
     */
    RuntimeSceneBuildResult BuildRuntimeSceneDefinition(const SceneDocument &doc);
} // namespace Horo::Editor
