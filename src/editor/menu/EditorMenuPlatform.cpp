#include "editor/menu/EditorMenuPlatform.h"

#if !defined(__APPLE__)
namespace Horo::Editor {
    /** @copydoc UsesNativeEditorMenuBar */
    bool UsesNativeEditorMenuBar() noexcept {
        return false;
    }

    /** @copydoc InstallNativeEditorMenuBar */
    void InstallNativeEditorMenuBar(const EditorMenuModel &, const ILocalizationService &) {
        // Non-Apple platforms use in-window ImGui menu bars rather than OS-level menus.
    }

    /** @copydoc PollNativeEditorMenuAction */
    std::optional<EditorMenuInvocation> PollNativeEditorMenuAction() noexcept {
        return std::nullopt;
    }

    /** @copydoc RevealInNativeFileManager */
    bool RevealInNativeFileManager(const std::filesystem::path &) noexcept {
        return false;
    }

    /** @copydoc OpenInExternalEditor */
    bool OpenInExternalEditor(const std::filesystem::path &) noexcept {
        return false;
    }
}  // namespace Horo::Editor
#endif
