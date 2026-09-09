#pragma once

#include <array>

struct ImVec2;

namespace Horo::Editor {
    struct EditorGuiContext;

    /** @brief MCP status story hosted by the global dock. */
    class GlobalDockMcpPane {
    public:
        void Draw(const ImVec2 &contentOrigin, float contentWidth, const EditorGuiContext &context);

    private:
        enum class Filter : unsigned char {
            All,
            Mutations,
            Errors,
        };

        std::array<char, 256> m_search{};
        Filter m_filter{Filter::All};
        int m_sessionSelection{};
        bool m_paused{};
    };
}  // namespace Horo::Editor
