#pragma once

#include "Horo/Editor/EditorGuiContext.h"
#include "editor/screens/workspace/EditorWorkspaceViewModel.h"
#include "Horo/Editor/WorkspacePanelRegistry.h"
#include "WorkspaceSplitterInteraction.h"

#include <imgui.h>

#include <cstdint>

namespace Horo::Editor
{
    struct GuiContentRegion;

    class EditorWorkspaceView final : public Input::IInputCaptureOwner
    {
    public:
        EditorWorkspaceView(const EditorGuiContext& context, const WorkspacePanelRegistry& panelRegistry,
                            std::uintptr_t logoTexture, Input::InputRouter& inputRouter,
                            Input::InputContextToken& workspaceInputContext);

        void Draw(const EditorWorkspaceViewModel& viewModel, EditorWorkspaceViewCommandData& outCommand,
                  const GuiContentRegion& contentRegion) const;
        void OnInputCaptureCancelled(Input::CaptureCancellationReason reason) noexcept override;

    private:
        const EditorGuiContext& m_context;
        const WorkspacePanelRegistry& m_panelRegistry;
        std::uintptr_t m_logoTexture;
        Input::InputRouter& m_inputRouter;
        Input::InputContextToken& m_workspaceInputContext;
        mutable WorkspaceSplitterInteraction m_splitterInteraction;
        mutable Input::InputContextToken m_panelDragContext;
        mutable Input::PointerCaptureToken m_panelDragCapture;

        [[nodiscard]] bool EnsurePanelDragCapture() const;
        [[nodiscard]] bool PanelDragEligible() const noexcept;

        void DrawMenuBar(const ImVec2& display, const EditorWorkspaceViewModel& viewModel,
                         EditorWorkspaceViewCommandData& outCommand) const;

        void DrawToolbar(const ImVec2& pos, const ImVec2& size, const EditorWorkspaceViewModel& viewModel,
                         EditorWorkspaceViewCommandData& outCommand) const;

        void DrawDockArea(WorkspaceDockArea area, const char* windowId, const ImVec2& pos, const ImVec2& size,
                          std::string_view activePanelId, const EditorWorkspaceViewModel& viewModel,
                          EditorWorkspaceViewCommandData& outCommand) const;

        void DrawActivityBar(const ImVec2& pos, const ImVec2& size, const WorkspacePanelRegistry& registry,
                             const EditorWorkspaceViewModel& viewModel, EditorWorkspaceViewCommandData& outCommand,
                             WorkspaceDockArea area, bool indicatorOnRight, bool allowDragSources) const;
    };
} // namespace Horo::Editor
