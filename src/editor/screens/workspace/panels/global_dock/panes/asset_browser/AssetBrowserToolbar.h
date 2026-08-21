#pragma once

struct ImVec2;

namespace Horo::Editor {
    struct EditorGuiContext;
    struct AssetBrowserInteractionState;
    struct EditorWorkspaceViewCommandData;
    struct EditorWorkspaceViewModel;

    /**
     * @brief Draws Asset Browser navigation, breadcrumbs, search, filtering, and sorting.
     * @param position Absolute screen position of the toolbar.
     * @param availableWidth Width available to the complete toolbar.
     * @param viewModel Read-only workspace projection.
     * @param command Command sink for navigation actions.
     * @param state Cross-frame Asset Browser presentation state.
     * @param context Editor GUI services and theme.
     */
    void DrawAssetBrowserToolbar(const ImVec2 &position, float availableWidth, const EditorWorkspaceViewModel &viewModel,
                                 EditorWorkspaceViewCommandData &command, AssetBrowserInteractionState &state,
                                 const EditorGuiContext &context);
}  // namespace Horo::Editor
