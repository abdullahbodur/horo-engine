#pragma once

#include "Horo/Editor/EditorGuiContext.h"
#include "Horo/Editor/EditorUiComponents.h"
#include "Horo/Foundation/Logging/StructuredLogStore.h"

#include <array>
#include <cstdint>
#include <imgui.h>
#include <string_view>
#include <vector>

namespace Horo::Editor {
    /** @brief Stateful Build Output tab hosted by the global bottom dock. */
    class GlobalDockBuildOutputPane {
    public:
        /** @brief Coarse status filter applied on top of the "Build." category filter. */
        enum class StatusFilter : std::uint8_t {
            All,
            Ok,
            Failed,
            Cached
        };

        /** @brief Binds the shared structured-log query source. */
        void Attach(const Log::IStructuredLogQuery *logQuery) noexcept;

        /** @brief Releases the shared query source and transient state. */
        void Detach() noexcept;

        /** @brief Draws the Build Output toolbar and per-asset cook result stream. */
        void Draw(const ImVec2 &contentOrigin, float contentWidth, const EditorGuiContext &context);

    private:
        [[nodiscard]] bool RefreshSnapshot();
        void RebuildFilter();
        [[nodiscard]] bool PassesStatusFilter(std::string_view status) const noexcept;

        const Log::IStructuredLogQuery *m_logQuery{nullptr};
        Log::StructuredLogSnapshot m_snapshot;
        std::uint64_t m_revision{};
        std::array<char, 160> m_search{};
        std::array<bool, 4> m_columnVisible{true, true, true, true};
        std::vector<std::size_t> m_filteredIndices;
        bool m_filterDirty{true};
        bool m_initialFollowTail{true};

        StatusFilter m_statusFilter{StatusFilter::All};
    };
}  // namespace Horo::Editor
