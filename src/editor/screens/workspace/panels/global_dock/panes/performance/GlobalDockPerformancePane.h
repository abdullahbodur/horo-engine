#pragma once

#include <array>

struct ImVec2;

namespace Horo::Editor {
    struct EditorGuiContext;

    /** @brief Performance status story hosted by the global dock. */
    class GlobalDockPerformancePane {
    public:
        void Draw(const ImVec2 &contentOrigin, float contentWidth, const EditorGuiContext &context);

    private:
        std::array<char, 256> m_search{};
        int m_windowSelection{};
        int m_subsystemSelection{};
        bool m_live{true};
    };
}  // namespace Horo::Editor
