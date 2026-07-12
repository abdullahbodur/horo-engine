#pragma once

#include "Horo/Editor/WorkspaceDockArea.h"
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
} // namespace Horo::Editor
