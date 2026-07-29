#pragma once

#include "Horo/Editor/EditorGuiContext.h"
#include "Horo/Editor/EditorUiComponents.h"
#include "Horo/Foundation/Logging/StructuredLogStore.h"

#include <array>
#include <cstdint>
#include <imgui.h>
#include <string>
#include <vector>

namespace Horo::Editor {
    /** @brief Stateful Console tab view hosted by the global bottom dock. */
    class GlobalDockConsolePane {
    public:
        /** @brief Binds the shared structured-log query source. */
        void Attach(const Log::IStructuredLogQuery *logQuery) noexcept;

        /** @brief Releases the shared query source and transient selection state. */
        void Detach() noexcept;

        /** @brief Draws the complete Console toolbar and selectable log stream. */
        void Draw(const ImVec2 &contentOrigin, float contentWidth, const EditorGuiContext &context);

    private:
        [[nodiscard]] bool RefreshSnapshot();
        void RebuildFilter();

        const Log::IStructuredLogQuery *m_logQuery{nullptr};
        Log::StructuredLogSnapshot m_snapshot;
        std::uint64_t m_revision{};
        std::array<bool, 5> m_levelEnabled{true, true, true, true, true};
        std::array<char, 160> m_search{};
        std::vector<std::size_t> m_filteredIndices;
        std::string m_selectableText;
        std::vector<Ui::SelectableTextLineLayout> m_lineLayouts;
        bool m_filterDirty{true};
        bool m_initialFollowTail{true};
        bool m_textSelectionActive{false};
    };
}  // namespace Horo::Editor
