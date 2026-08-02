#pragma once

#include "Horo/Editor/EditorGuiContext.h"
#include "Horo/Editor/EditorUiComponents.h"
#include "Horo/Foundation/OperationStore.h"

#include <array>
#include <cstdint>
#include <imgui.h>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Horo::Editor {
    /** @brief Stateful Operations tab hosted by the global bottom dock. */
    class GlobalDockOperationsPane {
    public:
        /** @brief Binds typed operation query and cancellation capabilities. */
        void Attach(const IOperationQuery *operationQuery, IOperationControl *operationControl) noexcept;

        /** @brief Releases the shared query source and transient state. */
        void Detach() noexcept;

        /** @brief Draws the Operations toolbar and operation status list. */
        void Draw(const ImVec2 &contentOrigin, float contentWidth, const EditorGuiContext &context);

        /** @brief Projects immutable operation indices through the active text filter. */
        [[nodiscard]] static std::vector<std::size_t> ProjectRecords(std::span<const OperationRecord> operations, std::string_view search);

    private:
        [[nodiscard]] bool RefreshSnapshot();
        void RebuildFilter();

        const IOperationQuery *m_operationQuery{nullptr};
        IOperationControl *m_operationControl{nullptr};
        OperationStoreSnapshot m_snapshot;
        std::uint64_t m_revision{};
        std::array<char, 160> m_search{};
        std::vector<std::size_t> m_filteredIndices;
        bool m_filterDirty{true};
        bool m_initialFollowTail{true};
        std::array<bool, 6> m_columnVisible{true, true, true, true, true, true};
    };
}  // namespace Horo::Editor
