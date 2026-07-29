#include "editor/screens/workspace/panels/global_dock/panes/performance/GlobalDockPerformancePane.h"

#include "editor/screens/workspace/panels/global_dock/panes/shared/GlobalDockStatusPane.h"

#include <array>

namespace Horo::Editor {
    void GlobalDockPerformancePane::Draw(const ImVec2 &contentOrigin, const float contentWidth, const EditorGuiContext &context) const {
        constexpr std::array rows{
            GlobalDockStatusRow{"GPU", "workspace.global_dock.performance.gpu"},
            GlobalDockStatusRow{"CPU", "workspace.global_dock.performance.cpu"},
            GlobalDockStatusRow{"MEM", "workspace.global_dock.performance.memory"},
        };
        DrawGlobalDockStatusPane(contentOrigin, contentWidth, context, "workspace.global_dock.preview.performance", rows);
    }
}  // namespace Horo::Editor
