#include "ui/launcher/LauncherWidgets.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <string>
#include <string_view>

#include <imgui.h>

#include "core/ProjectPath.h"
#include "renderer/Texture.h"
#include "ui/common/HoroTheme.h"

namespace Horo::Launcher::UI {

namespace fs = std::filesystem;

// Public implementations of path utility and meta-icon (declared in header)
// are defined below the anonymous namespace section.

namespace {

// ---------------------------------------------------------------------------
// Icon draw helpers (internal — not part of the public API)

void DrawTemplateTileIcon(ImDrawList* drawList, const ImVec2& center,
                          TemplateTileIcon icon, ImU32 color) {
    if (icon == TemplateTileIcon::Image) {
        const ImVec2 min(center.x - 12.0f, center.y - 11.0f);
        const ImVec2 max(center.x + 12.0f, center.y + 11.0f);
        drawList->AddRect(min, max, color, 2.5f, 0, 2.0f);
        drawList->AddCircleFilled(ImVec2(min.x + 7.0f, min.y + 6.0f), 2.0f, color);
        drawList->AddLine(ImVec2(min.x + 4.0f, max.y - 4.0f),
                          ImVec2(center.x - 2.0f, center.y + 1.0f), color, 2.0f);
        drawList->AddLine(ImVec2(center.x - 2.0f, center.y + 1.0f),
                          ImVec2(center.x + 4.0f, max.y - 6.0f), color, 2.0f);
        drawList->AddLine(ImVec2(center.x + 4.0f, max.y - 6.0f),
                          ImVec2(max.x - 3.0f, center.y + 1.0f), color, 2.0f);
    } else if (icon == TemplateTileIcon::Sandbox) {
        const float r = 11.0f;
        drawList->AddNgon(center, r, color, 6, 2.0f);
    } else {
        // Cube
        const float h = 10.0f;
        const float w = 9.0f;
        const ImVec2 top(center.x, center.y - h);
        const ImVec2 rFront(center.x + w, center.y - h * 0.35f);
        const ImVec2 rBack(center.x + w, center.y + h * 0.35f);
        const ImVec2 bot(center.x, center.y + h);
        const ImVec2 lBack(center.x - w, center.y + h * 0.35f);
        const ImVec2 lFront(center.x - w, center.y - h * 0.35f);
        drawList->AddLine(top,    rFront, color, 1.8f);
        drawList->AddLine(rFront, rBack,  color, 1.8f);
        drawList->AddLine(rBack,  bot,    color, 1.8f);
        drawList->AddLine(bot,    lBack,  color, 1.8f);
        drawList->AddLine(lBack,  lFront, color, 1.8f);
        drawList->AddLine(lFront, top,    color, 1.8f);
        drawList->AddLine(top,    bot,    color, 1.3f);
        drawList->AddLine(rFront, lBack,  color, 1.3f);
        drawList->AddLine(lFront, rBack,  color, 1.3f);
    }
}

void DrawLauncherActionIcon(ImDrawList* drawList, const ImVec2& pos,
                            LauncherActionIcon icon) {
    const Horo::UI::HoroTheme& theme = Horo::UI::GetHoroTheme();
    const ImVec2 max(pos.x + 38.0f, pos.y + 38.0f);
    drawList->AddRectFilled(
        pos, max,
        ImGui::ColorConvertFloat4ToU32(ImVec4(0.10f, 0.22f, 0.40f, 1.0f)), 8.0f);

    const ImU32 iconColor = ImGui::ColorConvertFloat4ToU32(theme.accentHover);
    if (icon == LauncherActionIcon::Folder) {
        const ImVec2 tabMin(pos.x + 10.0f, pos.y + 12.0f);
        drawList->AddRectFilled(tabMin, ImVec2(pos.x + 22.0f, pos.y + 17.0f),
                                iconColor, 2.0f);
        drawList->AddRectFilled(ImVec2(pos.x + 9.0f, pos.y + 16.0f),
                                ImVec2(pos.x + 29.0f, pos.y + 27.0f), iconColor,
                                3.0f);
    } else {
        const float centerX = pos.x + 19.0f;
        drawList->AddLine(ImVec2(centerX, pos.y + 10.0f),
                          ImVec2(centerX, pos.y + 24.0f), iconColor, 2.0f);
        drawList->AddLine(ImVec2(centerX - 5.0f, pos.y + 19.0f),
                          ImVec2(centerX, pos.y + 24.0f), iconColor, 2.0f);
        drawList->AddLine(ImVec2(centerX + 5.0f, pos.y + 19.0f),
                          ImVec2(centerX, pos.y + 24.0f), iconColor, 2.0f);
        drawList->AddLine(ImVec2(pos.x + 12.0f, pos.y + 28.0f),
                          ImVec2(pos.x + 26.0f, pos.y + 28.0f), iconColor, 2.0f);
    }
}

void DrawSidebarFooterIcon(ImDrawList* drawList, const ImVec2& pos,
                           SidebarFooterIcon icon) {
    const Horo::UI::HoroTheme& theme = Horo::UI::GetHoroTheme();
    const ImU32 color = ImGui::ColorConvertFloat4ToU32(theme.textMuted);
    if (icon == SidebarFooterIcon::Discord) {
        const ImVec2 headMin(pos.x + 2.0f, pos.y + 4.0f);
        const ImVec2 headMax(pos.x + 16.0f, pos.y + 13.0f);
        drawList->AddRect(headMin, headMax, color, 4.0f, 0, 1.5f);
        drawList->AddLine(ImVec2(pos.x + 5.0f, pos.y + 4.0f),
                          ImVec2(pos.x + 4.0f, pos.y + 2.0f), color, 1.5f);
        drawList->AddLine(ImVec2(pos.x + 13.0f, pos.y + 4.0f),
                          ImVec2(pos.x + 14.0f, pos.y + 2.0f), color, 1.5f);
        drawList->AddCircleFilled(ImVec2(pos.x + 7.0f, pos.y + 9.0f), 1.1f, color);
        drawList->AddCircleFilled(ImVec2(pos.x + 12.0f, pos.y + 9.0f), 1.1f, color);
    } else {
        const ImVec2 pageMin(pos.x + 4.0f, pos.y + 2.0f);
        const ImVec2 pageMax(pos.x + 15.0f, pos.y + 16.0f);
        drawList->AddRect(pageMin, pageMax, color, 1.5f, 0, 1.5f);
        drawList->AddLine(ImVec2(pos.x + 7.0f, pos.y + 7.0f),
                          ImVec2(pos.x + 13.0f, pos.y + 7.0f), color, 1.1f);
        drawList->AddLine(ImVec2(pos.x + 7.0f, pos.y + 10.0f),
                          ImVec2(pos.x + 13.0f, pos.y + 10.0f), color, 1.1f);
        drawList->AddLine(ImVec2(pos.x + 7.0f, pos.y + 13.0f),
                          ImVec2(pos.x + 11.0f, pos.y + 13.0f), color, 1.1f);
    }
}

} // namespace

// ---------------------------------------------------------------------------
// Public API implementations

fs::path ResolveLauncherVisualAsset(std::string_view relativePath) {
    if (relativePath.empty())
        return {};
    const std::array<fs::path, 4> candidates = {
        ProjectPath::ResolveSdk("assets/launcher/" + std::string(relativePath)),
        ProjectPath::Root() / "assets" / "launcher" / std::string(relativePath),
        ProjectPath::Root() / "engine" / "assets" / "launcher" /
            std::string(relativePath),
        ProjectPath::Root() / std::string(relativePath),
    };
    for (const fs::path& candidate : candidates) {
        std::error_code ec;
        if (fs::is_regular_file(candidate, ec) && !ec)
            return candidate;
    }
    return {};
}

void DrawRecentProjectMetaIcon(ImDrawList* drawList, const ImVec2& pos,
                               ImU32 color) {
    drawList->AddRect(ImVec2(pos.x, pos.y + 2.0f),
                      ImVec2(pos.x + 12.0f, pos.y + 14.0f), color, 2.0f, 0, 1.2f);
    drawList->AddLine(ImVec2(pos.x, pos.y + 6.0f),
                      ImVec2(pos.x + 12.0f, pos.y + 6.0f), color, 1.2f);
    drawList->AddLine(ImVec2(pos.x + 3.0f, pos.y),
                      ImVec2(pos.x + 3.0f, pos.y + 4.0f), color, 1.4f);
    drawList->AddLine(ImVec2(pos.x + 9.0f, pos.y),
                      ImVec2(pos.x + 9.0f, pos.y + 4.0f), color, 1.4f);
}

void DrawBackdrop(ImDrawList* drawList, const ImVec2& pos, const ImVec2& size) {
    if (!drawList)
        return;
    const Horo::UI::HoroTheme& theme = Horo::UI::GetHoroTheme();
    static const bool hasCustomBackdrop =
        !ResolveLauncherVisualAsset("background.png").empty();
    const ImVec4 top = hasCustomBackdrop ? ImVec4(0.03f, 0.07f, 0.14f, 1.0f)
                                         : theme.backgroundTop;
    const ImVec4 bottom = hasCustomBackdrop ? ImVec4(0.02f, 0.04f, 0.09f, 1.0f)
                                            : theme.backgroundBottom;
    const ImVec2 max(pos.x + size.x, pos.y + size.y);
    drawList->AddRectFilledMultiColor(pos, max,
                                     ImGui::ColorConvertFloat4ToU32(top),
                                     ImGui::ColorConvertFloat4ToU32(top),
                                     ImGui::ColorConvertFloat4ToU32(bottom),
                                     ImGui::ColorConvertFloat4ToU32(bottom));
}

bool SidebarNavItem(const char* label, bool selected, const ImVec2& size) {
    const Horo::UI::HoroTheme& theme = Horo::UI::GetHoroTheme();
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(14.0f, 11.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 9.0f);
    if (selected) {
        ImGui::PushStyleColor(ImGuiCol_Button,        theme.accent);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, theme.accentHover);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  theme.accentActive);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.97f, 0.98f, 1.0f, 1.0f));
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.08f, 0.12f, 0.19f, 0.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.12f, 0.19f, 0.30f, 0.72f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.13f, 0.21f, 0.34f, 0.86f));
        ImGui::PushStyleColor(ImGuiCol_Text, theme.textMuted);
    }
    const bool pressed = ImGui::Button(label, size);
    ImGui::PopStyleColor(4);
    ImGui::PopStyleVar(2);
    return pressed;
}

void VerticalDivider(float height) {
    const Horo::UI::HoroTheme& theme = Horo::UI::GetHoroTheme();
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddLine(
        ImVec2(pos.x, pos.y + 2.0f), ImVec2(pos.x, pos.y + height - 2.0f),
        ImGui::ColorConvertFloat4ToU32(theme.border), 1.0f);
    ImGui::Dummy(ImVec2(1.0f, height));
}

bool RecentProjectButton(const char* title, const ImVec2& size) {
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,  ImVec2(14.0f, 8.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 7.0f);
    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.12f, 0.15f, 0.20f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.16f, 0.20f, 0.28f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.15f, 0.18f, 0.24f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.94f, 0.96f, 1.0f, 1.0f));
    const bool pressed = ImGui::Button(title, size);
    ImGui::PopStyleColor(4);
    ImGui::PopStyleVar(2);
    return pressed;
}

bool RecentProjectMenuButton(const char* id, const ImVec2& size) {
    const Horo::UI::HoroTheme& theme = Horo::UI::GetHoroTheme();
    ImGui::InvisibleButton(id, size);
    const bool hovered = ImGui::IsItemHovered();
    const bool active  = ImGui::IsItemActive();
    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    ImVec4 fill(0.10f, 0.14f, 0.20f, 0.54f);
    if (active)
        fill = ImVec4(0.12f, 0.18f, 0.27f, 0.90f);
    else if (hovered)
        fill = ImVec4(0.12f, 0.18f, 0.27f, 0.76f);
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(min, max, ImGui::ColorConvertFloat4ToU32(fill), 6.0f);
    const ImU32 dotColor = ImGui::ColorConvertFloat4ToU32(theme.textMuted);
    const ImVec2 center((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
    drawList->AddCircleFilled(ImVec2(center.x, center.y - 6.0f), 1.5f, dotColor);
    drawList->AddCircleFilled(center,                             1.5f, dotColor);
    drawList->AddCircleFilled(ImVec2(center.x, center.y + 6.0f), 1.5f, dotColor);
    return ImGui::IsItemClicked();
}

bool TemplateTile(const char* label, TemplateTileIcon icon,
                  bool selected, bool enabled, const ImVec2& size) {
    const Horo::UI::HoroTheme& theme = Horo::UI::GetHoroTheme();
    ImGui::InvisibleButton(label, size);
    const bool pressed = enabled && ImGui::IsItemClicked();
    const bool hovered = enabled && ImGui::IsItemHovered();
    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();

    ImVec4 fill(0.09f, 0.13f, 0.20f, 1.0f);
    if (!enabled)       fill = ImVec4(0.08f, 0.11f, 0.16f, 0.72f);
    else if (selected)  fill = ImVec4(0.10f, 0.18f, 0.30f, 1.0f);
    else if (hovered)   fill = ImVec4(0.13f, 0.19f, 0.28f, 1.0f);

    ImVec4 border(0.15f, 0.24f, 0.35f, 0.60f);
    if (!enabled)      border = ImVec4(0.13f, 0.20f, 0.29f, 0.42f);
    else if (selected) border = theme.accent;

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(min, max, ImGui::ColorConvertFloat4ToU32(fill), 6.0f);
    drawList->AddRect(min, max, ImGui::ColorConvertFloat4ToU32(border), 6.0f, 0,
                      selected ? 1.5f : 1.0f);

    ImVec4 iconColor = theme.textMuted;
    if (!enabled)
        iconColor = ImVec4(theme.textMuted.x, theme.textMuted.y,
                           theme.textMuted.z, 0.52f);
    else if (selected)
        iconColor = theme.accentHover;
    DrawTemplateTileIcon(drawList, ImVec2((min.x + max.x) * 0.5f, min.y + 27.0f),
                         icon, ImGui::ColorConvertFloat4ToU32(iconColor));

    const ImVec2 textSize = ImGui::CalcTextSize(label);
    drawList->AddText(
        ImVec2((min.x + max.x - textSize.x) * 0.5f, max.y - textSize.y - 10.0f),
        ImGui::ColorConvertFloat4ToU32(
            enabled ? ImVec4(0.92f, 0.95f, 0.99f, 1.0f)
                    : ImVec4(0.68f, 0.74f, 0.84f, 0.62f)),
        label);
    return pressed;
}

void RenderCreateProjectHeader() {
    const Horo::UI::HoroTheme& theme = Horo::UI::GetHoroTheme();
    const ImVec2 iconPos = ImGui::GetCursorScreenPos();
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImVec2 iconMax(iconPos.x + 38.0f, iconPos.y + 38.0f);
    drawList->AddRectFilled(
        iconPos, iconMax,
        ImGui::ColorConvertFloat4ToU32(ImVec4(0.10f, 0.22f, 0.40f, 1.0f)), 6.0f);
    const ImU32 sparkleColor = ImGui::ColorConvertFloat4ToU32(theme.accentHover);
    drawList->AddLine(ImVec2(iconPos.x + 19.0f, iconPos.y + 9.0f),
                      ImVec2(iconPos.x + 19.0f, iconPos.y + 18.0f), sparkleColor, 1.6f);
    drawList->AddLine(ImVec2(iconPos.x + 14.5f, iconPos.y + 13.5f),
                      ImVec2(iconPos.x + 23.5f, iconPos.y + 13.5f), sparkleColor, 1.6f);
    drawList->AddLine(ImVec2(iconPos.x + 27.0f, iconPos.y + 22.0f),
                      ImVec2(iconPos.x + 27.0f, iconPos.y + 28.0f), sparkleColor, 1.4f);
    drawList->AddLine(ImVec2(iconPos.x + 24.0f, iconPos.y + 25.0f),
                      ImVec2(iconPos.x + 30.0f, iconPos.y + 25.0f), sparkleColor, 1.4f);

    ImGui::Dummy(ImVec2(46.0f, 38.0f));
    ImGui::SameLine(0.0f, 8.0f);
    ImGui::BeginGroup();
    ImGui::TextUnformatted("Create New Project");
    ImGui::TextColored(theme.textMuted, "Start a new project from a template");
    ImGui::EndGroup();
}

void AdvancedSettingsToggle(bool* open) {
    const Horo::UI::HoroTheme& theme = Horo::UI::GetHoroTheme();
    const ImVec2 size(190.0f, 30.0f);
    ImGui::InvisibleButton("Advanced Settings", size);
    if (ImGui::IsItemClicked() && open)
        *open = !*open;

    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 textPos(min.x + 26.0f, min.y + 7.0f);
    const ImU32 color = ImGui::ColorConvertFloat4ToU32(theme.textMuted);
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    if (open && *open) {
        drawList->AddTriangleFilled(ImVec2(min.x + 7.0f, min.y + 11.0f),
                                    ImVec2(min.x + 17.0f, min.y + 11.0f),
                                    ImVec2(min.x + 12.0f, min.y + 17.0f), color);
    } else {
        drawList->AddTriangleFilled(ImVec2(min.x + 9.0f, min.y + 10.0f),
                                    ImVec2(min.x + 9.0f, min.y + 18.0f),
                                    ImVec2(min.x + 15.0f, min.y + 14.0f), color);
    }
    drawList->AddText(textPos, color, "Advanced Settings");
}

void RenderLauncherBrand(const Texture* logoTexture) {
    if (logoTexture && logoTexture->IsValid() &&
        logoTexture->GetNativeId() != 0) {
        const float availableWidth = ImGui::GetContentRegionAvail().x;
        const float logoWidth = std::min(availableWidth, 184.0f);
        constexpr float kLogoAspect = 1778.0f / 900.0f;
        ImGui::Image(
            (ImTextureID) static_cast<intptr_t>(logoTexture->GetNativeId()),
            ImVec2(logoWidth, logoWidth / kLogoAspect),
            ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f));
        return;
    }
    ImGui::TextUnformatted("HORO ENGINE");
}

void SidebarFooterItem(const char* label, SidebarFooterIcon icon) {
    const Horo::UI::HoroTheme& theme = Horo::UI::GetHoroTheme();
    const ImVec2 textPos  = ImGui::GetCursorPos();
    const ImVec2 iconPos  = ImGui::GetCursorScreenPos();
    DrawSidebarFooterIcon(ImGui::GetWindowDrawList(), iconPos, icon);
    ImGui::SetCursorPos(ImVec2(textPos.x + 26.0f, textPos.y));
    ImGui::TextColored(theme.textMuted, "%s", label);
    ImGui::Dummy(ImVec2(0.0f, 4.0f));
}

void SidebarFooterImageItem(const char* label, const Texture* iconTexture) {
    const Horo::UI::HoroTheme& theme = Horo::UI::GetHoroTheme();
    const ImVec2 textPos = ImGui::GetCursorPos();
    if (iconTexture && iconTexture->IsValid() && iconTexture->GetNativeId() != 0) {
        ImGui::SetCursorPosY(textPos.y + 1.0f);
        ImGui::Image(
            (ImTextureID) static_cast<intptr_t>(iconTexture->GetNativeId()),
            ImVec2(18.0f, 18.0f), ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f),
            theme.textMuted);
    } else {
        DrawSidebarFooterIcon(ImGui::GetWindowDrawList(),
                              ImGui::GetCursorScreenPos(),
                              SidebarFooterIcon::Discord);
    }
    ImGui::SetCursorPos(ImVec2(textPos.x + 26.0f, textPos.y));
    ImGui::TextColored(theme.textMuted, "%s", label);
    ImGui::Dummy(ImVec2(0.0f, 4.0f));
}

void SidebarFooterSeparator() {
    const Horo::UI::HoroTheme& theme = Horo::UI::GetHoroTheme();
    const ImVec2 start = ImGui::GetCursorScreenPos();
    const float width = std::min(154.0f, ImGui::GetContentRegionAvail().x);
    ImGui::GetWindowDrawList()->AddLine(
        start, ImVec2(start.x + width, start.y),
        ImGui::ColorConvertFloat4ToU32(theme.border), 1.0f);
    ImGui::Dummy(ImVec2(0.0f, 14.0f));
}

bool LauncherActionCard(const char* id, const char* title, const char* subtitle,
                        LauncherActionIcon icon, const ImVec2& size) {
    const Horo::UI::HoroTheme& theme = Horo::UI::GetHoroTheme();
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton(id, size);
    const bool hovered = ImGui::IsItemHovered();
    const bool active  = ImGui::IsItemActive();
    ImVec4 fill(0.05f, 0.10f, 0.17f, 0.76f);
    if (active)
        fill = ImVec4(0.08f, 0.14f, 0.23f, 0.88f);
    else if (hovered)
        fill = ImVec4(0.08f, 0.15f, 0.25f, 0.84f);
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y),
                            ImGui::ColorConvertFloat4ToU32(fill), 8.0f);
    drawList->AddRect(pos, ImVec2(pos.x + size.x, pos.y + size.y),
                      ImGui::ColorConvertFloat4ToU32(theme.border), 8.0f);

    const ImVec2 iconPos(pos.x + 18.0f, pos.y + 17.0f);
    DrawLauncherActionIcon(drawList, iconPos, icon);
    drawList->AddText(
        ImVec2(pos.x + 64.0f, pos.y + 19.0f),
        ImGui::ColorConvertFloat4ToU32(ImVec4(0.96f, 0.98f, 1.0f, 1.0f)), title);
    drawList->AddText(ImVec2(pos.x + 64.0f, pos.y + 43.0f),
                      ImGui::ColorConvertFloat4ToU32(theme.textMuted), subtitle);
    return ImGui::IsItemClicked();
}

} // namespace Horo::Launcher::UI
