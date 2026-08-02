#pragma once

#include "Horo/Editor/EditorMenuModel.h"

#include <filesystem>
#include <optional>

namespace Horo::Editor {
    class ILocalizationService;

    /** @brief Reports whether this host uses the operating system's global application menu bar. */
    [[nodiscard]] bool UsesNativeEditorMenuBar() noexcept;

    /**
     * @brief Installs or refreshes the native application menu from the shared model.
     * @param model Platform-neutral menu hierarchy.
     * @param localization Active editor localization service.
     */
    void InstallNativeEditorMenuBar(const EditorMenuModel &model, const ILocalizationService &localization);

    /**
     * @brief Takes the next native menu invocation queued by the platform callback.
     * @return Pending invocation, or empty when no native menu command is waiting.
     */
    [[nodiscard]] std::optional<EditorMenuInvocation> PollNativeEditorMenuAction() noexcept;

    /**
     * @brief Reveals one absolute editor-owned path in the native file manager.
     * @param absolutePath Existing absolute file or directory path.
     * @return True when the platform accepted the reveal request.
     */
    [[nodiscard]] bool RevealInNativeFileManager(const std::filesystem::path &absolutePath) noexcept;

    /**
     * @brief Opens one absolute source file in the operating system's configured external editor.
     * @param absolutePath Existing absolute source path.
     * @return True when the platform accepted the open request.
     */
    [[nodiscard]] bool OpenInExternalEditor(const std::filesystem::path &absolutePath) noexcept;
}  // namespace Horo::Editor
