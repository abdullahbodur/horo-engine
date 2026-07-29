#pragma once

namespace Horo::Editor {
    class EditorGuiContext;
    struct AssetBrowserInteractionState;
    struct ContentBrowserDirectory;
    struct EditorWorkspaceViewCommandData;

    /**
     * @brief Draws every modal workflow owned by the Asset Browser pane.
     * @param state Cross-frame popup state and edit buffers.
     * @param directory Current absolute Asset Browser directory.
     * @param command Command sink for confirmed operations.
     * @param context Editor GUI services and theme.
     */
    void DrawAssetBrowserDialogs(AssetBrowserInteractionState &state, const ContentBrowserDirectory &directory,
                                 EditorWorkspaceViewCommandData &command, const EditorGuiContext &context);
}  // namespace Horo::Editor
