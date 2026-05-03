#pragma once

#include <cstddef>
#include <filesystem>
#include <string_view>
#include <imgui.h>

namespace Horo {
class Texture;
} // namespace Horo

namespace Horo::Launcher::UI {

// ----------------------------------------------------------------------------
// Enums used by callers

enum class TemplateTileIcon {
    Cube,
    Image,
    Sandbox,
};

enum class LauncherActionIcon {
    Folder,
    Import,
};

enum class SidebarFooterIcon {
    Discord,
    Documentation,
};

// ----------------------------------------------------------------------------
// Background

// Draws the launcher backdrop gradient (or image-adjusted tint) into drawList.
void DrawBackdrop(ImDrawList* drawList, const ImVec2& pos, const ImVec2& size);

// ----------------------------------------------------------------------------
// Sidebar navigation

// Styled sidebar nav button. Returns true when clicked.
// Renders with accent color when selected, transparent otherwise.
bool SidebarNavItem(const char* label, bool selected,
                    const ImVec2& size = ImVec2(-1.0f, 40.0f));

// Draws a 1-pixel themed vertical divider line of the given height.
void VerticalDivider(float height);

// ----------------------------------------------------------------------------
// Recent projects

// Styled button for a recent project entry. Returns true when clicked.
bool RecentProjectButton(const char* title, const ImVec2& size);

// Three-dot overflow menu button for a recent project row. Returns true when
// clicked.
bool RecentProjectMenuButton(const char* id, const ImVec2& size);

// ----------------------------------------------------------------------------
// Template tiles

// Clickable tile card with icon. Returns true when clicked (only when enabled).
bool TemplateTile(const char* label, TemplateTileIcon icon,
                  bool selected, bool enabled, const ImVec2& size);

// ----------------------------------------------------------------------------
// Create-project header

// Renders the sparkle icon box followed by title and subtitle text.
void RenderCreateProjectHeader();

// Collapsible "Advanced Settings" toggle widget. Flips *open on click.
void AdvancedSettingsToggle(bool* open);

// ----------------------------------------------------------------------------
// Brand / logo

// Renders the engine logo texture if valid, or falls back to text.
void RenderLauncherBrand(const Texture* logoTexture);

// ----------------------------------------------------------------------------
// Sidebar footer

// Renders a labeled footer item with a built-in icon.
void SidebarFooterItem(const char* label, SidebarFooterIcon icon);

// Renders a labeled footer item with a texture icon (falls back to Discord icon).
void SidebarFooterImageItem(const char* label, const Texture* iconTexture);

// Renders a thin horizontal separator with spacing.
void SidebarFooterSeparator();

// ----------------------------------------------------------------------------
// Action cards

// Large clickable card with title, subtitle, and an action icon.
// Returns true when clicked.
bool LauncherActionCard(const char* id, const char* title, const char* subtitle,
                        LauncherActionIcon icon, const ImVec2& size);

// Draws a small calendar/date meta icon at pos with the given color.
void DrawRecentProjectMetaIcon(ImDrawList* drawList, const ImVec2& pos,
                               ImU32 color);

// ----------------------------------------------------------------------------
// Asset path utility

// Resolves a launcher visual asset (relative to the launcher asset directories).
// Returns an empty path if the asset is not found.
// Searches: SDK/assets/launcher/, project-root/assets/launcher/, etc.
std::filesystem::path ResolveLauncherVisualAsset(std::string_view relativePath);

} // namespace Horo::Launcher::UI
