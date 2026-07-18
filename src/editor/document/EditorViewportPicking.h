#pragma once

/**
 * @file EditorViewportPicking.h
 * @brief Backend-neutral CPU picking against immutable editor viewport snapshots.
 */

#include "editor/document/EditorViewportSceneExtractor.h"
#include "Horo/Foundation/Result.h"

#include <optional>

namespace Horo::Editor {
    /** @brief Normalized viewport position and aspect ratio captured by the viewport panel. */
    struct EditorViewportPickQuery {
        float normalizedX{0.0F};
        float normalizedY{0.0F};
        float aspect{1.0F};
    };

    /**
     * @brief Finds the nearest renderable scene object intersected by one viewport ray.
     * @param scene Immutable owning viewport snapshot and stable object mapping.
     * @param query Normalized top-left-origin viewport coordinates and positive aspect ratio.
     * @return Nearest stable object identity, no value for a miss, or a typed validation error.
     */
    [[nodiscard]] Result<std::optional<SceneObjectId> > PickEditorViewportScene(
        const EditorViewportSceneSnapshot &scene, const EditorViewportPickQuery &query);
} // namespace Horo::Editor
