#pragma once

#include "Horo/Editor/EditorGuiContext.h"
#include "Horo/Editor/EditorUiComponents.h"
#include "Horo/Foundation/BuildOutputStore.h"

#include <array>
#include <cstdint>
#include <imgui.h>
#include <span>
#include <string_view>
#include <vector>

namespace Horo::Editor {
    struct EditorWorkspaceViewCommandData;

    /** @brief Stateful Build Output tab hosted by the global bottom dock. */
    class GlobalDockBuildOutputPane {
    public:
        /** @brief Coarse status filter applied to typed build-output records. */
        enum class StatusFilter : std::uint8_t {
            All,
            Ok,
            Failed,
            Cached
        };

        /** @brief Binds the shared typed build-output query source. */
        void Attach(const IBuildOutputQuery *buildOutputQuery) noexcept;

        /** @brief Releases the shared query source and transient state. */
        void Detach() noexcept;

        /** @brief Draws the Build Output toolbar and per-asset cook result stream. */
        void Draw(const ImVec2 &contentOrigin, float contentWidth, EditorWorkspaceViewCommandData &command,
                  const EditorGuiContext &context);

        /** @brief Projects immutable record indices through the active status and text filters. */
        [[nodiscard]] static std::vector<std::size_t> ProjectRecords(std::span<const BuildOutputRecord> records, StatusFilter statusFilter,
                                                                     std::string_view search);

    private:
        [[nodiscard]] bool RefreshSnapshot();
        void RebuildFilter();
        [[nodiscard]] static bool PassesStatusFilter(BuildOutputStatus status, StatusFilter filter) noexcept;

        const IBuildOutputQuery *m_buildOutputQuery{nullptr};
        BuildOutputSnapshot m_snapshot;
        std::uint64_t m_revision{};
        std::array<char, 160> m_search{};
        std::array<bool, 4> m_columnVisible{true, true, true, true};
        std::vector<std::size_t> m_filteredIndices;
        bool m_filterDirty{true};
        bool m_initialFollowTail{true};

        StatusFilter m_statusFilter{StatusFilter::All};
    };
}  // namespace Horo::Editor
