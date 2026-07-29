#include "editor/screens/workspace/panels/global_dock/panes/network/GlobalDockNetworkPane.h"

#include "editor/screens/workspace/panels/global_dock/panes/shared/GlobalDockStatusPane.h"

#include <array>

namespace Horo::Editor {
    void GlobalDockNetworkPane::Draw(const ImVec2 &contentOrigin, const float contentWidth, const EditorGuiContext &context) const {
        constexpr std::array rows{
            GlobalDockStatusRow{"PING", "workspace.global_dock.network.ping"},
            GlobalDockStatusRow{"REP", "workspace.global_dock.network.replication"},
            GlobalDockStatusRow{"CONN", "workspace.global_dock.network.connection"},
        };
        DrawGlobalDockStatusPane(contentOrigin, contentWidth, context, "workspace.global_dock.preview.network", rows);
    }
}  // namespace Horo::Editor
