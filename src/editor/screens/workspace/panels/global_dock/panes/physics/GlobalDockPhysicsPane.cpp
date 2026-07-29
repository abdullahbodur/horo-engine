#include "editor/screens/workspace/panels/global_dock/panes/physics/GlobalDockPhysicsPane.h"

#include "editor/screens/workspace/panels/global_dock/panes/shared/GlobalDockStatusPane.h"

#include <array>

namespace Horo::Editor {
    void GlobalDockPhysicsPane::Draw(const ImVec2 &contentOrigin, const float contentWidth, const EditorGuiContext &context) const {
        constexpr std::array rows{
            GlobalDockStatusRow{"SOLVER", "workspace.global_dock.physics.solver"},
            GlobalDockStatusRow{"LAYERS", "workspace.global_dock.physics.layers"},
            GlobalDockStatusRow{"MEM", "workspace.global_dock.physics.memory"},
        };
        DrawGlobalDockStatusPane(contentOrigin, contentWidth, context, "workspace.global_dock.preview.physics", rows);
    }
}  // namespace Horo::Editor
