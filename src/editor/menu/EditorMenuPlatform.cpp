#include "editor/menu/EditorMenuPlatform.h"

#if !defined(__APPLE__)
namespace Horo::Editor {
    /** @copydoc UsesNativeEditorMenuBar */
    bool UsesNativeEditorMenuBar() noexcept {
        return false;
    }

    /** @copydoc InstallNativeEditorMenuBar */
    void InstallNativeEditorMenuBar(const EditorMenuModel &, const ILocalizationService &) {
    }

    /** @copydoc PollNativeEditorMenuAction */
    std::optional<EditorMenuInvocation> PollNativeEditorMenuAction() noexcept {
        return std::nullopt;
    }
} // namespace Horo::Editor
#endif
