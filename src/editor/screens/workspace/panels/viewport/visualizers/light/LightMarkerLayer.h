#pragma once

#include "editor/screens/workspace/EditorWorkspaceViewModel.h"

#include <imgui.h>
#include <optional>
#include <span>

namespace Horo::Editor {
    /**
     * @brief Draws constant-size viewport Light markers and resolves one clicked object.
     * @return Stable Light object identity when a visible marker was clicked.
     */
    [[nodiscard]] std::optional<SceneObjectId> DrawViewportLightMarkers(ImDrawList &drawList, ImVec2 origin, float width, float height,
                                                                        const EditorViewportCamera &camera,
                                                                        std::span<const ViewportLightPresentation> lights,
                                                                        std::optional<SceneObjectId> primarySelection, bool acceptInput,
                                                                        Math::ClipDepthRange depthRange);
}  // namespace Horo::Editor
