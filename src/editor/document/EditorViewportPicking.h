#pragma once

/**
 * @file EditorViewportPicking.h
 * @brief Backend-neutral CPU picking against immutable editor viewport snapshots.
 */

#include "Horo/Foundation/Result.h"
#include "editor/document/EditorViewportSceneExtractor.h"

#include <optional>

namespace Horo::Editor
{
/** @brief Normalized viewport position and aspect ratio captured by the viewport panel. */
struct EditorViewportPickQuery
{
    float normalizedX{0.0F};
    float normalizedY{0.0F};
    float aspect{1.0F};
};

/** @brief Picking payload tagged with the runtime scene that produced it. */
struct EditorViewportPickResult
{
    Runtime::SceneRuntimeId runtimeScene;
    std::optional<SceneObjectId> object;
};

/** @brief Finds the nearest renderable scene object and tags the result with its source runtime. */
[[nodiscard]] Result<EditorViewportPickResult> PickEditorViewportScene(const EditorViewportSceneSnapshot &scene,
                                                                       const EditorViewportPickQuery &query);
} // namespace Horo::Editor
