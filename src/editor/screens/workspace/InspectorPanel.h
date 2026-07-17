#pragma once

#include "Horo/Editor/EditorGuiContext.h"
#include "editor/screens/workspace/EditorWorkspaceViewModel.h"

#include <imgui.h>

#include "Horo/Editor/IWorkspacePanel.h"

#include <array>
#include <optional>

namespace Horo::Editor
{
    class InspectorPanel final : public IWorkspacePanel
    {
    public:
        [[nodiscard]] std::string GetId() const override
        {
            return "horo.inspector";
        }

        [[nodiscard]] std::string GetDisplayName() const override
        {
            return "horo.panel.inspector.title";
        }

        [[nodiscard]] WorkspaceDockArea GetDefaultDockArea() const override
        {
            return WorkspaceDockArea::Right;
        }

        [[nodiscard]] std::vector<std::string> GetObservedEventTypes() const override
        {
            return {"SceneDocumentChangedEvent", "SelectionChangedEvent"};
        }

        void OnAttach(PanelContext& ctx) override
        {
        }

        void OnDetach() override
        {
        }

        void DrawIcon(ImDrawList* dl, const ImVec2& pos, const ImVec2& size, ImU32 color) override;

        void DrawPanel(const ImVec2& pos, const ImVec2& size, const EditorWorkspaceViewModel& vm,
                       EditorWorkspaceViewCommandData& cmd, const EditorGuiContext& ctx) override;

    private:
        std::optional<SceneObjectId> m_draftObject;
        DocumentRevision m_draftRevision;
        std::array<float, 3> m_positionDraft{};
        std::array<float, 3> m_rotationDegreesDraft{};
        std::array<float, 3> m_scaleDraft{1.0F, 1.0F, 1.0F};
    };
} // namespace Horo::Editor
