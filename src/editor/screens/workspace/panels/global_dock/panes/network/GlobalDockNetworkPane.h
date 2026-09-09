#pragma once

#include <array>

struct ImVec2;

namespace Horo::Editor {
    struct EditorGuiContext;

    /** @brief Network status story hosted by the global dock. */
    class GlobalDockNetworkPane {
    public:
        void Draw(const ImVec2 &contentOrigin, float contentWidth, const EditorGuiContext &context);

    private:
        std::array<char, 256> m_search{};
        int m_sessionSelection{};
        int m_viewSelection{};
        bool m_paused{};
        bool m_cleared{};
    };
}  // namespace Horo::Editor
