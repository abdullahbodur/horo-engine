#pragma once

#include <span>
#include <string_view>

struct ImVec2;

namespace Horo::Editor {
    struct EditorGuiContext;

    /** @brief Semantic tone for one global-dock status value. */
    enum class GlobalDockStatusTone {
        Normal,
        Dim,
        Warning,
        Error,
    };

    /** @brief One localized row rendered by a lightweight status pane. */
    struct GlobalDockStatusRow {
        std::string_view label;
        std::string_view valueKey;
        GlobalDockStatusTone tone{GlobalDockStatusTone::Normal};
    };

    /** @brief Draws the shared presentation used by lightweight dock stories. */
    void DrawGlobalDockStatusPane(const ImVec2 &contentOrigin, float contentWidth, const EditorGuiContext &context,
                                  std::string_view descriptionKey, std::span<const GlobalDockStatusRow> rows);
}  // namespace Horo::Editor
