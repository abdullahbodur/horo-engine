#pragma once

#include "Horo/Editor/EditorMenuModel.h"

#include <optional>

namespace Horo::Editor
{
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
 * @brief Takes the next native menu action queued by the platform callback.
 * @return Pending action, or empty when no native menu command is waiting.
 */
[[nodiscard]] std::optional<EditorMenuAction> PollNativeEditorMenuAction() noexcept;
} // namespace Horo::Editor
