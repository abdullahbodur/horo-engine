#pragma once

#include <string_view>
#include <vector>

namespace Horo::Editor
{
/**
 * @file EditorMenuModel.h
 * @brief Platform-neutral editor application menu hierarchy.
 */

/** @brief Stable editor actions emitted by native and in-window menu renderers. */
enum class EditorMenuAction
{
    None,
    NewProject,
    OpenProject,
    SaveScene,
    OpenEditorSettings,
    ExitApplication,
};

/** @brief Structural kind of one menu model entry. */
enum class EditorMenuItemKind
{
    Command,
    Separator,
    Submenu,
};

/** @brief Platform-neutral menu item including localization, command, and child metadata. */
struct EditorMenuItem
{
    EditorMenuItemKind kind{EditorMenuItemKind::Command};
    std::string_view labelKey;
    EditorMenuAction action{EditorMenuAction::None};
    std::string_view shortcut;
    std::string_view macKeyEquivalent;
    bool enabledByDefault{false};
    std::vector<EditorMenuItem> children;
};

/** @brief Complete ordered application menu hierarchy shared by every platform renderer. */
struct EditorMenuModel
{
    std::vector<EditorMenuItem> menus;
};

/**
 * @brief Returns the immutable editor application menu model.
 * @return Process-lifetime menu model whose string views reference static storage.
 */
[[nodiscard]] const EditorMenuModel &GetEditorMenuModel();
} // namespace Horo::Editor
