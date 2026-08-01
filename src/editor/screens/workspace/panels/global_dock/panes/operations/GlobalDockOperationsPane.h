#pragma once

#include "Horo/Editor/EditorGuiContext.h"
#include "Horo/Editor/EditorUiComponents.h"
#include "Horo/Foundation/Logging/StructuredLogStore.h"

#include <array>
#include <cstdint>
#include <imgui.h>
#include <string>
#include <string_view>
#include <vector>

namespace Horo::Editor {
    /** @brief Stateful Operations tab hosted by the global bottom dock. */
    class GlobalDockOperationsPane {
    public:
        /** @brief Binds the shared structured-log query source. */
        void Attach(const Log::IStructuredLogQuery *logQuery) noexcept;

        /** @brief Releases the shared query source and transient state. */
        void Detach() noexcept;

        /** @brief Draws the Operations toolbar and operation status list. */
        void Draw(const ImVec2 &contentOrigin, float contentWidth, const EditorGuiContext &context);

    private:
        [[nodiscard]] bool RefreshSnapshot();
        void RebuildFilter();

        const Log::IStructuredLogQuery *m_logQuery{nullptr};
        Log::StructuredLogSnapshot m_snapshot;
        std::uint64_t m_revision{};
        std::array<char, 160> m_search{};
        std::vector<std::size_t> m_filteredIndices;
        std::string m_selectableText;
        std::vector<Ui::SelectableTextLineLayout> m_lineLayouts;
        bool m_filterDirty{true};
        bool m_initialFollowTail{true};
        bool m_textSelectionActive{false};
        std::array<bool, 3> m_columnVisible{true, true, true};
    };
}  // namespace Horo::Editor
