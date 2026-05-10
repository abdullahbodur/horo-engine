/**
 * @file EditorSettingsModal.cpp
 * @brief Tabbed Settings modal: MCP and Appearance tabs with staged drafts,
 *        Apply/OK/Cancel footer, and central theme application on save.
 */
#include "ui/editor/components/EditorSettingsModal.h"

#include <algorithm>
#include <array>
#include <format>

#include <imgui.h>

#include "ui/HoroTheme.h"
#include "ui/UiComponents.h"

namespace Horo::Editor {
namespace {

/** @brief Fixed popup id used for automation and ImGui popup stack identity. */
constexpr const char *kPopupId = "Editor Settings";
constexpr float kModalDefaultWidth = 880.0f;
constexpr float kModalDefaultHeight = 560.0f;
constexpr float kLeftTabColumnWidth = 220.0f;
constexpr float kFooterHeight = 56.0f;

/** @brief Snapshot of available display size, clamped for the settings modal. */
ImVec2 ComputeModalSize(const ImGuiIO &io) {
    constexpr float kSafeMargin = 48.0f;
    const float width = std::min(kModalDefaultWidth,
                                 std::max(520.0f, io.DisplaySize.x - kSafeMargin));
    const float height = std::min(kModalDefaultHeight,
                                  std::max(400.0f, io.DisplaySize.y - kSafeMargin));
    return ImVec2(width, height);
}

/** @brief Static swatch colour used for preset previews. */
void DrawSwatch(ImDrawList *dl, ImVec2 topLeft, float size, const ImVec4 &color,
                float rounding) {
    dl->AddRectFilled(topLeft, ImVec2(topLeft.x + size, topLeft.y + size),
                      ImGui::ColorConvertFloat4ToU32(color), rounding);
}

/** @brief Returns the visible description string used on Appearance preset cards. */
const char *ThemePresetDescription(Horo::Ui::EditorThemePreset preset) {
    using enum Horo::Ui::EditorThemePreset;
    switch (preset) {
    case DarkBlue:
        return "Current Horo editor palette";
    case Graphite:
        return "Neutral dark workspace";
    case HighContrast:
        return "Maximum contrast for readability";
    }
    return "";
}

/** @brief Returns the stable automation marker id for a preset card. */
const char *ThemePresetMarker(Horo::Ui::EditorThemePreset preset) {
    using enum Horo::Ui::EditorThemePreset;
    switch (preset) {
    case DarkBlue:
        return "##settings_test/theme_dark_blue";
    case Graphite:
        return "##settings_test/theme_graphite";
    case HighContrast:
        return "##settings_test/theme_high_contrast";
    }
    return "##settings_test/theme_unknown";
}

} // namespace

/** @copydoc EditorSettingsModal::Open */
void EditorSettingsModal::Open(const Mcp::McpSettings &mcp,
                               const EditorUserSettings &user) {
    m_open = true;
    m_openRequested = true;
    m_activeTab = Tab::MCP;
    m_error.clear();
    m_mcpOriginal = mcp;
    m_mcpDraft = mcp;
    m_userOriginal = user;
    m_userDraft = user;
}

/** @copydoc EditorSettingsModal::IsDirty */
bool EditorSettingsModal::IsDirty() const {
    if (m_mcpDraft.enabled != m_mcpOriginal.enabled)
        return true;
    if (m_mcpDraft.autoStart != m_mcpOriginal.autoStart)
        return true;
    if (m_mcpDraft.port != m_mcpOriginal.port)
        return true;
    if (m_mcpDraft.host != m_mcpOriginal.host)
        return true;
    if (m_mcpDraft.transport != m_mcpOriginal.transport)
        return true;
    if (m_userDraft.themePreset != m_userOriginal.themePreset)
        return true;
    return false;
}

/** @copydoc EditorSettingsModal::ResetDrafts */
void EditorSettingsModal::ResetDrafts() {
    m_mcpDraft = m_mcpOriginal;
    m_userDraft = m_userOriginal;
    m_error.clear();
}

/** @copydoc EditorSettingsModal::SaveAll */
bool EditorSettingsModal::SaveAll() {
    m_error.clear();

    // Validate / sanitize MCP draft.
    m_mcpDraft.port = std::clamp(m_mcpDraft.port, 1, 65535);
    m_mcpDraft.host = Mcp::kDefaultMcpHost;

    // Validate appearance preset by ensuring it matches a known enumerant.
    const auto &presets = Horo::Ui::EditorThemePresets();
    if (std::ranges::find(presets, m_userDraft.themePreset) == presets.end()) {
        m_error = "Invalid theme preset selection.";
        return false;
    }

    // Apply MCP settings.
    if (!m_mcpController) {
        m_error = "MCP controller unavailable.";
        return false;
    }
    std::string mcpError;
    if (!m_mcpController->ApplySettings(m_mcpDraft, &mcpError)) {
        m_error = mcpError.empty() ? "Failed to apply MCP settings." : mcpError;
        return false;
    }

    // Persist user settings (theme preset).
    if (!m_userSettingsDocument) {
        m_error = "User settings document unavailable.";
        return false;
    }
    m_userSettingsDocument->settings = m_userDraft;
    std::string userError;
    if (!SaveEditorUserSettingsDocument(m_userSettingsDocument, &userError)) {
        m_error = userError.empty() ? "Failed to save editor user settings."
                                    : userError;
        return false;
    }

    // Refresh originals from the authoritative post-save state.
    m_mcpOriginal = m_mcpController->GetSettings();
    m_mcpDraft = m_mcpOriginal;
    m_userOriginal = m_userSettingsDocument->settings;
    m_userDraft = m_userOriginal;

    // Apply theme preset into the live editor style.
    if (m_applyThemePreset)
        m_applyThemePreset(m_userDraft.themePreset);

    return true;
}

/** @copydoc EditorSettingsModal::Draw */
void EditorSettingsModal::Draw() {
    if (!m_open)
        return;

    const auto &theme = Horo::Ui::GetEditorTheme();
    const ImGuiIO &io = ImGui::GetIO();

    if (m_openRequested) {
        ImGui::OpenPopup(kPopupId);
        m_openRequested = false;
    }

    const ImVec2 modalSize = ComputeModalSize(io);
    ImGui::SetNextWindowSize(modalSize, ImGuiCond_Appearing);
    constexpr ImGuiWindowFlags kFlags =
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings;

    if (!ImGui::BeginPopupModal(kPopupId, nullptr, kFlags))
        return;

    // Treat Escape as Cancel.
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        ResetDrafts();
        m_open = false;
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }

    // ── Header ───────────────────────────────────────────────────────────────
    {
        ImGui::PushStyleColor(ImGuiCol_Text, theme.palette.text);
        ImGui::PushFont(ImGui::GetFont());
        ImGui::TextUnformatted("Settings");
        ImGui::PopFont();
        ImGui::PopStyleColor();

        // Right-aligned close affordance: "X" button acts like Cancel.
        const float closeSize = ImGui::GetFrameHeight();
        const float avail = ImGui::GetContentRegionAvail().x;
        ImGui::SameLine(0.0f, 0.0f);
        if (avail > closeSize)
            ImGui::Dummy(ImVec2(avail - closeSize, 0.0f));
        ImGui::SameLine(0.0f, 0.0f);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, theme.palette.cardHover);
        ImGui::PushStyleColor(ImGuiCol_Text, theme.palette.textMuted);
        if (ImGui::Button("X##settings_test/header_close",
                          ImVec2(closeSize, closeSize))) {
            ResetDrafts();
            m_open = false;
            ImGui::CloseCurrentPopup();
            ImGui::PopStyleColor(3);
            ImGui::EndPopup();
            return;
        }
        ImGui::PopStyleColor(3);

        ImGui::PushStyleColor(ImGuiCol_Text, theme.palette.textMuted);
        ImGui::TextUnformatted("Editor preferences");
        ImGui::PopStyleColor();
        ImGui::Separator();
    }

    // ── Body: left tabs + right content ──────────────────────────────────────
    const float bodyHeight =
        std::max(120.0f, ImGui::GetContentRegionAvail().y - kFooterHeight);
    ImGui::BeginChild("##settings_body", ImVec2(0.0f, bodyHeight),
                      ImGuiChildFlags_None);

    const std::array<Horo::Ui::EditorVerticalTabItem, 2> tabs = {{
        {
            .id = "##settings_test/tab_mcp",
            .icon = nullptr,
            .label = "MCP",
            .description = "Built-in server",
            .selected = m_activeTab == Tab::MCP,
        },
        {
            .id = "##settings_test/tab_appearance",
            .icon = nullptr,
            .label = "Appearance",
            .description = "Theme presets",
            .selected = m_activeTab == Tab::Appearance,
        },
    }};

    const auto tabResult = Horo::Ui::RenderEditorVerticalTabs(
        theme, "##settings_vtabs", tabs, kLeftTabColumnWidth);
    if (tabResult.clickedIndex == 0)
        m_activeTab = Tab::MCP;
    else if (tabResult.clickedIndex == 1)
        m_activeTab = Tab::Appearance;

    ImGui::SameLine();

    ImGui::BeginChild("##settings_content", ImVec2(0.0f, 0.0f),
                      ImGuiChildFlags_None,
                      ImGuiWindowFlags_AlwaysUseWindowPadding);

    switch (m_activeTab) {
    case Tab::MCP: {
        ImGui::PushStyleColor(ImGuiCol_Text, theme.palette.text);
        ImGui::TextUnformatted("MCP");
        ImGui::PopStyleColor();
        Horo::Ui::TextMuted(theme,
                             "Built-in Model Context Protocol server settings.");
        ImGui::Spacing();

        if (Horo::Ui::BeginEditorSettingsCard(theme, "##settings_mcp_server_card",
                                              "Server")) {
            Horo::Ui::RenderEditorToggle(theme, "##mcp_toggle",
                                         "Enable built-in MCP",
                                         m_mcpDraft.enabled);
            Horo::Ui::RenderEditorCheckbox(theme, "Auto-start when editor opens",
                                           m_mcpDraft.autoStart);

            int port = m_mcpDraft.port;
            ImGui::SetNextItemWidth(160.0f);
            if (ImGui::InputInt("Port", &port))
                m_mcpDraft.port = std::clamp(port, 1, 65535);

            ImGui::Text("Host: %s", Mcp::kDefaultMcpHost);
            m_mcpDraft.host = Mcp::kDefaultMcpHost;

            const auto endpoint =
                std::format("{}://{}:{}/mcp", Mcp::kMcpUrlScheme,
                            m_mcpDraft.host, m_mcpDraft.port);
            ImGui::TextWrapped("Endpoint: %s##settings_test/mcp_endpoint",
                               endpoint.c_str());
        }
        Horo::Ui::EndEditorSettingsCard();
        break;
    }

    case Tab::Appearance: {
        ImGui::PushStyleColor(ImGuiCol_Text, theme.palette.text);
        ImGui::TextUnformatted("Appearance");
        ImGui::PopStyleColor();
        Horo::Ui::TextMuted(theme, "Editor theme and color preferences.");
        ImGui::Spacing();

        if (Horo::Ui::BeginEditorSettingsCard(theme,
                                              "##settings_appearance_theme_card",
                                              "Theme")) {
            for (const Horo::Ui::EditorThemePreset preset :
                 Horo::Ui::EditorThemePresets()) {
                const bool selected = m_userDraft.themePreset == preset;
                const char *label = Horo::Ui::EditorThemePresetLabel(preset);
                const char *description = ThemePresetDescription(preset);
                const char *marker = ThemePresetMarker(preset);

                // Draw preset row as a selectable card with a leading radio marker.
                ImGui::PushID(marker);
                const ImVec2 rowStart = ImGui::GetCursorScreenPos();
                const float rowHeight = 62.0f;
                const float rowWidth = ImGui::GetContentRegionAvail().x;

                const std::string hitId =
                    std::string("##preset_hit") + marker;
                if (ImGui::InvisibleButton(hitId.c_str(),
                                           ImVec2(rowWidth, rowHeight)))
                    m_userDraft.themePreset = preset;
                const bool hovered = ImGui::IsItemHovered();

                ImDrawList *dl = ImGui::GetWindowDrawList();
                const ImVec4 bg = selected ? theme.palette.selection
                                           : (hovered ? theme.palette.cardHover
                                                      : theme.palette.card);
                dl->AddRectFilled(
                    rowStart,
                    ImVec2(rowStart.x + rowWidth, rowStart.y + rowHeight),
                    ImGui::ColorConvertFloat4ToU32(bg),
                    theme.rounding.card);
                dl->AddRect(
                    rowStart,
                    ImVec2(rowStart.x + rowWidth, rowStart.y + rowHeight),
                    ImGui::ColorConvertFloat4ToU32(theme.palette.border),
                    theme.rounding.card, 0, 1.0f);

                // Radio indicator
                const float radioCx = rowStart.x + 18.0f;
                const float radioCy = rowStart.y + rowHeight * 0.5f;
                dl->AddCircle(ImVec2(radioCx, radioCy), 7.0f,
                              ImGui::ColorConvertFloat4ToU32(
                                  selected ? theme.palette.accent
                                           : theme.palette.border),
                              0, 1.5f);
                if (selected) {
                    dl->AddCircleFilled(
                        ImVec2(radioCx, radioCy), 3.5f,
                        ImGui::ColorConvertFloat4ToU32(theme.palette.accent));
                }

                // Label + description
                const float textX = rowStart.x + 40.0f;
                dl->AddText(
                    ImVec2(textX, rowStart.y + 12.0f),
                    ImGui::ColorConvertFloat4ToU32(theme.palette.text), label);
                dl->AddText(
                    ImVec2(textX, rowStart.y + 12.0f + ImGui::GetFontSize() + 2.0f),
                    ImGui::ColorConvertFloat4ToU32(theme.palette.textMuted),
                    description);

                // Swatch row on the right — static previews sampled from preset tokens.
                // To keep preview work lightweight we render swatches from the
                // active editor theme for the currently selected preset only.
                if (selected) {
                    constexpr float kSwatchSize = 14.0f;
                    constexpr float kSwatchGap = 4.0f;
                    const float swatchY = rowStart.y + (rowHeight - kSwatchSize) * 0.5f;
                    float swatchX = rowStart.x + rowWidth - (kSwatchSize * 4.0f + kSwatchGap * 3.0f + 16.0f);
                    DrawSwatch(dl, ImVec2(swatchX, swatchY), kSwatchSize,
                               theme.palette.panel, theme.rounding.input);
                    swatchX += kSwatchSize + kSwatchGap;
                    DrawSwatch(dl, ImVec2(swatchX, swatchY), kSwatchSize,
                               theme.palette.card, theme.rounding.input);
                    swatchX += kSwatchSize + kSwatchGap;
                    DrawSwatch(dl, ImVec2(swatchX, swatchY), kSwatchSize,
                               theme.palette.accent, theme.rounding.input);
                    swatchX += kSwatchSize + kSwatchGap;
                    DrawSwatch(dl, ImVec2(swatchX, swatchY), kSwatchSize,
                               theme.palette.textMuted, theme.rounding.input);
                }

                ImGui::PopID();
                ImGui::Spacing();
            }
        }
        Horo::Ui::EndEditorSettingsCard();
        break;
    }
    }

    ImGui::EndChild();
    ImGui::EndChild();

    // ── Footer: Cancel / Apply / OK ──────────────────────────────────────────
    if (!m_error.empty()) {
        Horo::Ui::ErrorText(theme, m_error.c_str());
    }

    const bool dirty = IsDirty();
    const auto footer =
        Horo::Ui::RenderEditorSettingsFooter(theme, /*canApply=*/dirty);

    if (footer.cancelled) {
        ResetDrafts();
        m_open = false;
        ImGui::CloseCurrentPopup();
    } else if (footer.applied) {
        if (SaveAll()) {
            // Stay open after Apply.
        }
        // On failure, m_error already set; modal remains open.
    } else if (footer.accepted) {
        if (!dirty) {
            m_open = false;
            ImGui::CloseCurrentPopup();
        } else if (SaveAll()) {
            m_open = false;
            ImGui::CloseCurrentPopup();
        }
        // On save failure the modal remains open with m_error populated.
    }

    ImGui::EndPopup();
}

} // namespace Horo::Editor
