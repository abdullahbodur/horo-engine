#pragma once

#include "Horo/Runtime/Input.h"

#include <vector>

namespace Horo::Editor
{
inline constexpr const char *kEditorWorkspaceInputContext = "editor.workspace";
inline constexpr const char *kActionSave = "editor.save";
inline constexpr const char *kActionUndo = "editor.undo";
inline constexpr const char *kActionRedo = "editor.redo";
inline constexpr const char *kActionDuplicate = "editor.duplicate";
inline constexpr const char *kActionDelete = "editor.delete";
inline constexpr const char *kActionToolSelect = "editor.tool.select";
inline constexpr const char *kActionToolMove = "editor.tool.move";
inline constexpr const char *kActionToolRotate = "editor.tool.rotate";
inline constexpr const char *kActionToolScale = "editor.tool.scale";
inline constexpr const char *kActionViewportFocusSelected = "editor.viewport.focus_selected";

/** @brief Returns the authoritative editor action descriptors and default bindings. */
[[nodiscard]] std::vector<Input::ActionDescriptor> BuildEditorInputActions();
} // namespace Horo::Editor
