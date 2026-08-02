#include "editor/screens/workspace/panels/global_dock/panes/mcp/GlobalDockMcpPane.h"

#include "editor/screens/workspace/panels/global_dock/panes/shared/GlobalDockStatusPane.h"

#include <array>

namespace Horo::Editor {
    void GlobalDockMcpPane::Draw(const ImVec2 &contentOrigin, const float contentWidth, const EditorGuiContext &context) const {
        constexpr std::array rows{
            GlobalDockStatusRow{"BRIDGE", "workspace.global_dock.mcp.bridge"},
            GlobalDockStatusRow{"TOOLS", "workspace.global_dock.mcp.tools"},
            GlobalDockStatusRow{"", "workspace.global_dock.mcp.awaiting", GlobalDockStatusTone::Dim},
        };
        DrawGlobalDockStatusPane(contentOrigin, contentWidth, context, "workspace.global_dock.preview.mcp", rows);
    }
}  // namespace Horo::Editor
