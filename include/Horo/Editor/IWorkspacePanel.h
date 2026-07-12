#pragma once

#include "Horo/Editor/WorkspaceDockArea.h"
#include "Horo/Editor/EditorGuiContext.h"
#include "Horo/Editor/EditorWorkspaceViewModel.h"

#include <imgui.h>
#include <string>
#include <vector>

namespace Horo::Editor
{
    class EditorDataBus;

    /** @brief Context injected when a panel is attached to the workspace. */
    struct PanelContext
    {
        EditorDataBus& dataBus;
        // Extend later with other services (SceneDocument, etc.)
    };

    /** @brief Base interface for a modular Workspace Panel (e.g. Hierarchy, Inspector) */
    class IWorkspacePanel
    {
    public:
        virtual ~IWorkspacePanel() = default;

        /** @brief Unique stable identifier for this panel (e.g. "horo.hierarchy") */
        [[nodiscard]] virtual std::string GetId() const = 0;

        /** @brief i18n localization key for the panel's display name (e.g. "horo.panel.hierarchy.title") */
        [[nodiscard]] virtual std::string GetDisplayName() const = 0;

        /** @brief The dock area where this panel should open by default. */
        [[nodiscard]] virtual WorkspaceDockArea GetDefaultDockArea() const = 0;

        /** @brief Returns a list of event types this panel wants to observe via the DataBus. */
        [[nodiscard]] virtual std::vector<std::string> GetObservedEventTypes() const = 0;

        /** @brief Called exactly once when the panel is registered and mounted. */
        virtual void OnAttach(PanelContext& ctx) = 0;

        /** @brief Called exactly once before the panel is unregistered or destroyed. */
        virtual void OnDetach() = 0;

        /** @brief Draws the 24x24 icon for the Activity Bar. */
        virtual void DrawIcon(ImDrawList* dl, const ImVec2& pos, const ImVec2& size, ImU32 color) = 0;

        /** @brief Draws the actual panel content, including its own internal tabs if needed. */
        virtual void DrawPanel(const ImVec2& pos, const ImVec2& size, const EditorWorkspaceViewModel& vm, EditorWorkspaceViewCommandData& cmd, const EditorGuiContext& ctx) = 0;
    };
} // namespace Horo::Editor
