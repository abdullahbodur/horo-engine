#pragma once

struct ImVec2;

namespace Horo::Editor {
    struct EditorGuiContext;

    /** @brief Localization status story hosted by the global dock. */
    class GlobalDockLocalizationPane {
    public:
        void Draw(const ImVec2 &contentOrigin, float contentWidth, const EditorGuiContext &context) const;
    };
}  // namespace Horo::Editor
