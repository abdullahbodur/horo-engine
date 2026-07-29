#include "editor/screens/workspace/panels/global_dock/panes/localization/GlobalDockLocalizationPane.h"

#include "editor/screens/workspace/panels/global_dock/panes/shared/GlobalDockStatusPane.h"

#include <array>

namespace Horo::Editor {
    void GlobalDockLocalizationPane::Draw(const ImVec2 &contentOrigin, const float contentWidth, const EditorGuiContext &context) const {
        constexpr std::array rows{
            GlobalDockStatusRow{"LOCALE", "workspace.global_dock.localization.locale"},
            GlobalDockStatusRow{"STRINGS", "workspace.global_dock.localization.strings"},
            GlobalDockStatusRow{"FONTS", "workspace.global_dock.localization.fonts"},
        };
        DrawGlobalDockStatusPane(contentOrigin, contentWidth, context, "workspace.global_dock.preview.localization", rows);
    }
}  // namespace Horo::Editor
