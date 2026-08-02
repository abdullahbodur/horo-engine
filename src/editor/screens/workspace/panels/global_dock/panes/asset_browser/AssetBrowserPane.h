#pragma once

#include "editor/screens/workspace/panels/global_dock/panes/asset_browser/AssetBrowserCards.h"
#include "editor/screens/workspace/panels/global_dock/panes/asset_browser/AssetBrowserInteractionSession.h"

struct ImVec2;

namespace Horo::Editor {
    class EditorGuiContext;
    class IEditorGuiRenderer;

    /** @brief Owns and draws the Assets story hosted by the global dock. */
    class AssetBrowserPane {
    public:
        /** @brief Binds renderer services required by generated previews. */
        void Attach(IEditorGuiRenderer *guiRenderer) noexcept;

        /** @brief Releases every renderer-owned preview texture. */
        void Detach() noexcept;

        /** @brief Draws the complete Assets tab content. */
        void Draw(const ImVec2 &contentOrigin, float contentWidth, const EditorWorkspaceViewModel &viewModel,
                  EditorWorkspaceViewCommandData &command, const EditorGuiContext &context);

    private:
        AssetBrowserInteractionSession m_interactionSession;
        AssetBrowserCardRenderer m_cardRenderer;
    };
}  // namespace Horo::Editor
