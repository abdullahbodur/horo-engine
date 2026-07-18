#pragma once

#include "Horo/Runtime/Input.h"

#include <vector>

namespace Horo::Editor
{
inline constexpr auto kEditorWorkspaceInputContext = "editor.workspace";
inline constexpr auto kActionSave = "editor.save";
inline constexpr auto kActionUndo = "editor.undo";
inline constexpr auto kActionRedo = "editor.redo";
inline constexpr auto kActionDuplicate = "editor.duplicate";
inline constexpr auto kActionDelete = "editor.delete";
inline constexpr auto kActionToolSelect = "editor.tool.select";
inline constexpr auto kActionToolMove = "editor.tool.move";
inline constexpr auto kActionToolRotate = "editor.tool.rotate";
inline constexpr auto kActionToolScale = "editor.tool.scale";
inline constexpr auto kActionViewportFocusSelected = "editor.viewport.focus_selected";

/** @brief Returns the authoritative editor action descriptors and default bindings. */
[[nodiscard]] std::vector<Input::ActionDescriptor> BuildEditorInputActions();
} // namespace Horo::Editor
