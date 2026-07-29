#pragma once

struct ImVec2;

namespace Horo::Editor {
    class EditorGuiContext;

    /** @brief Network status story hosted by the global dock. */
    class GlobalDockNetworkPane {
    public:
        void Draw(const ImVec2 &contentOrigin, float contentWidth, const EditorGuiContext &context) const;
    };
}  // namespace Horo::Editor
