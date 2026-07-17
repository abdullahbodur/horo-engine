#pragma once

#include "Horo/Editor/EditorGuiContext.h"
#include "editor/screens/workspace/EditorWorkspaceViewModel.h"
#include "Horo/Editor/HierarchyModel.h"

#include <imgui.h>

#include "Horo/Editor/IWorkspacePanel.h"

#include <array>
#include <optional>

namespace Horo::Editor
{
    class HierarchyPanel final : public IWorkspacePanel
    {
    public:
        HierarchyPanel() = default;

        [[nodiscard]] std::string GetId() const override
        {
            return "horo.hierarchy";
        }

        [[nodiscard]] std::string GetDisplayName() const override
        {
            return "horo.panel.hierarchy.title";
        }

        [[nodiscard]] WorkspaceDockArea GetDefaultDockArea() const override
        {
            return WorkspaceDockArea::Left;
        }

        [[nodiscard]] std::vector<std::string> GetObservedEventTypes() const override
        {
            return {"SceneDocumentChangedEvent", "SelectionChangedEvent"};
        }

        void OnAttach(PanelContext& ctx) override;
        void OnDetach() override;

        void DrawIcon(ImDrawList* dl, const ImVec2& pos, const ImVec2& size, ImU32 color) override;

        void DrawPanel(const ImVec2& pos, const ImVec2& size, const EditorWorkspaceViewModel& vm,
                       EditorWorkspaceViewCommandData& cmd, const EditorGuiContext& ctx) override;

    private:
        void BeginRename(HierarchyNodeId id);

        HierarchyModel model_;
        std::vector<HierarchyNodeInput> hierarchyInputs_;
        std::vector<HierarchyVisibleRow> visibleRows_;
        std::array<char, 128> searchBuffer_{};
        std::array<char, 129> renameBuffer_{};
        std::optional<HierarchyNodeId> renamingId_;
        DocumentRevision projectedRevision_{};
        DocumentRevision handledRevealRevision_{};
        bool projectionInitialized_{false};
        bool requestRenameFocus_{false};
        Input::InputRouter* inputRouter_{nullptr};
        Input::InputContextToken* workspaceInputContext_{nullptr};
        Input::InputContextToken focusedWidgetContext_;
    };
} // namespace Horo::Editor
