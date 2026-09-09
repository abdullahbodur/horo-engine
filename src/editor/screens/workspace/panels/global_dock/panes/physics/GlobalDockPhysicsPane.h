#pragma once

#include <array>

struct ImVec2;

namespace Horo::Editor {
    struct EditorGuiContext;

    /** @brief Physics status story hosted by the global dock. */
    class GlobalDockPhysicsPane {
    public:
        void Draw(const ImVec2 &contentOrigin, float contentWidth, const EditorGuiContext &context);

    private:
        std::array<char, 256> m_search{};
        int m_worldSelection{};
        bool m_colliders{true};
        bool m_contacts{true};
        bool m_constraints{};
        bool m_paused{};
    };
}  // namespace Horo::Editor
