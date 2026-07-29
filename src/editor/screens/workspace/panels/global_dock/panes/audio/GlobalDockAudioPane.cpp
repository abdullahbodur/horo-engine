#include "editor/screens/workspace/panels/global_dock/panes/audio/GlobalDockAudioPane.h"

#include "editor/screens/workspace/panels/global_dock/panes/shared/GlobalDockStatusPane.h"

#include <array>

namespace Horo::Editor {
    void GlobalDockAudioPane::Draw(const ImVec2 &contentOrigin, const float contentWidth, const EditorGuiContext &context) const {
        constexpr std::array rows{
            GlobalDockStatusRow{"MASTER", "workspace.global_dock.audio.master"},
            GlobalDockStatusRow{"BUSSES", "workspace.global_dock.audio.busses"},
            GlobalDockStatusRow{"DEVICE", "workspace.global_dock.audio.device"},
        };
        DrawGlobalDockStatusPane(contentOrigin, contentWidth, context, "workspace.global_dock.preview.audio", rows);
    }
}  // namespace Horo::Editor
