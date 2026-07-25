#pragma once

/**
 * @file AssetImportModalPresentation.h
 * @brief ImGui presentation for the Asset Import modal.
 */

#include "Horo/Editor/EditorModalHost.h"
#include "Horo/Editor/EditorTheme.h"

struct ImFont;

namespace Horo::Editor
{

class AssetImportModal;

/**
 * @brief Draws the host-owned Asset Import modal presentation.
 * @param modal The modal whose state is rendered.
 * @param fonts Theme fonts for typography.
 * @return A close request or no action.
 */
[[nodiscard]] ModalFrameResult DrawAssetImportModalPresentation(
    AssetImportModal &modal, const Theme::Fonts &fonts);

} // namespace Horo::Editor
