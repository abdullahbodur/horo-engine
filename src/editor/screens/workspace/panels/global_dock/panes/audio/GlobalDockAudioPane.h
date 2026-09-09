#pragma once

#include <array>

struct ImVec2;

namespace Horo::Editor {
    struct EditorGuiContext;

    /** @brief Audio status story hosted by the global dock. */
    class GlobalDockAudioPane {
    public:
        void Draw(const ImVec2 &contentOrigin, float contentWidth, const EditorGuiContext &context);

    private:
        std::array<char, 256> m_search{};
        std::array<bool, 4> m_muted{};
        std::array<bool, 4> m_solo{};
        int m_deviceSelection{};
        int m_viewSelection{};
        bool m_paused{};
        bool m_allMuted{};
    };
}  // namespace Horo::Editor
