#pragma once

#include "Horo/Editor/WorkspaceDockArea.h"
#include "Horo/Editor/ActivityBarLayout.h"
#include "Horo/Editor/WorkspacePanelHost.h"
#include <string>

namespace Horo::Editor
{
    /** @brief Emitted when a workspace panel is opened/becomes active in a dock area. */
    struct WorkspacePanelOpenedEvent
    {
        static constexpr auto HoroEventTypeName = "WorkspacePanelOpenedEvent";
        std::string panelId;
        WorkspaceDockArea area;
    };

    /** @brief Emitted when a workspace panel is closed/hidden in a dock area. */
    struct WorkspacePanelClosedEvent
    {
        static constexpr auto HoroEventTypeName = "WorkspacePanelClosedEvent";
        std::string panelId;
        WorkspaceDockArea area;
    };

    struct WorkspaceLayoutSize
    {
        float leftWidth;
        float leftHeight;
        float rightWidth;
        float rightHeight;
        float bottomWidth;
        float bottomHeight;
        float documentWidth;
        float documentHeight;
    };

    /** @brief Emitted when the workspace layout changes (e.g. splitters resized). */
    struct WorkspaceLayoutChangedEvent
    {
        static constexpr auto HoroEventTypeName = "WorkspaceLayoutChangedEvent";
        WorkspaceDockArea triggerArea; // The area that was actively resized
        WorkspaceLayoutSize layout;
    };
    /** @brief Emitted after a workspace panel is docked into a tab stack or split. */
    struct WorkspacePanelDockedEvent
    {
        static constexpr auto HoroEventTypeName = "WorkspacePanelDockedEvent";
        std::string panelId;
        std::string targetNodeId;
        WorkspacePanelHost::DropKind kind;
    };

    /** @brief Emitted after an Activity Bar icon is moved to a new group slot. */
    struct ActivityBarItemReorderedEvent
    {
        static constexpr auto HoroEventTypeName = "ActivityBarItemReorderedEvent";
        std::string panelId;
        ActivityBarSlot previousSlot;
        ActivityBarSlot newSlot;
    };
} // namespace Horo::Editor
