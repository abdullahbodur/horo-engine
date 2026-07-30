#pragma once

#include "Horo/Editor/HierarchyModel.h"
#include "editor/screens/workspace/EditorWorkspaceViewModel.h"

#include <optional>
#include <string_view>
#include <vector>

namespace Horo::Editor {
    /** @brief User gesture used to derive one complete hierarchy selection request. */
    enum class HierarchySelectionGesture : std::uint8_t {
        Replace,
        Toggle,
        Range,
    };

    /**
     * @brief Owns Hierarchy projection state and reduces semantic actions.
     *
     * The panel remains responsible for ImGui interaction. This session owns the
     * derived tree and translates semantic actions into typed workspace commands.
     */
    class HierarchyEditSession {
    public:
        /** @brief Reconciles the derived tree, reveal request, and selection. */
        void Synchronize(const EditorWorkspaceViewModel &viewModel);

        /** @brief Builds and returns visible rows for the supplied search query. */
        [[nodiscard]] const std::vector<HierarchyVisibleRow> &VisibleRows(std::string_view query);

        /** @brief Finds a projected hierarchy node by stable ID. */
        [[nodiscard]] const HierarchyNode *Find(HierarchyNodeId id) const noexcept;

        /** @brief Returns the selected projected hierarchy node ID. */
        [[nodiscard]] std::optional<HierarchyNodeId> SelectedId() const noexcept;

        /** @brief Reports whether one projected node belongs to the complete selection. */
        [[nodiscard]] bool IsSelected(HierarchyNodeId id) const noexcept;

        /** @brief Selects an existing projected node. */
        void Select(HierarchyNodeId id) noexcept;

        /** @brief Toggles expansion for an existing projected node. */
        void ToggleExpanded(HierarchyNodeId id) noexcept;

        /**
         * @brief Creates a typed complete object-selection command from one hierarchy gesture.
         * @param id Clicked projected object.
         * @param gesture Replace, command-toggle, or visible-row range gesture.
         * @return Typed selection command carrying the complete replacement set.
         */
        [[nodiscard]] EditorWorkspaceViewCommandData SelectCommand(HierarchyNodeId id, HierarchySelectionGesture gesture);

        /** @brief Creates a typed primitive-creation command. */
        [[nodiscard]] static EditorWorkspaceViewCommandData CreateCommand(Runtime::PrimitiveId primitive,
                                                                          std::optional<SceneObjectId> parent);

        /** @brief Creates a typed object-duplication command. */
        [[nodiscard]] static EditorWorkspaceViewCommandData DuplicateCommand(HierarchyNodeId id);

        /** @brief Creates a validated typed object-rename command. */
        [[nodiscard]] static EditorWorkspaceViewCommandData RenameCommand(HierarchyNodeId id, std::string_view name);

        /** @brief Creates a typed object-deletion command. */
        [[nodiscard]] static EditorWorkspaceViewCommandData DeleteCommand(HierarchyNodeId id);

    private:
        HierarchyModel m_model;
        std::vector<HierarchyNodeInput> m_inputs;
        std::vector<HierarchyVisibleRow> m_visibleRows;
        DocumentRevision m_projectedRevision{};
        DocumentRevision m_handledRevealRevision{};
        bool m_projectionInitialized{false};
        std::vector<SceneObjectId> m_selectedObjects;
        std::optional<HierarchyNodeId> m_selectionAnchor;
    };
}  // namespace Horo::Editor
