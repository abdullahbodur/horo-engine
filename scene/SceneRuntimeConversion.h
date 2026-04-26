#pragma once

#include "scene/RuntimeSceneDefinition.h"
#include "scene/SceneProjectModel.h"

namespace Horo {
    // Builds the engine-owned runtime scene definition from the typed authoring
    // model.
    RuntimeSceneBuildResult
    BuildRuntimeSceneDefinition(const SceneProjectModel &model);
} // namespace Horo
