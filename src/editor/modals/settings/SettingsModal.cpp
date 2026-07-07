#include "editor/modals/settings/SettingsModal.h"

#include "Horo/Editor/EditorSettingsStore.h"
#include "Horo/Editor/EditorTheme.h"
#include "editor/design_system/components/EditorUiComponents.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace Horo::Editor
{
    namespace
    {
        using Theme::Fonts;
        using Theme::ScopedTextStyle;

        namespace Layout
        {
            constexpr float ModalW = 960.0F;
            constexpr float ModalH = 680.0F;
            constexpr float ViewportPad = 48.0F;
            constexpr float HeaderH = 57.0F;
            constexpr float FooterH = 65.0F;
            constexpr float NavW = 200.0F;
            constexpr float Radius = 4.0F;
            constexpr float ModalRadius = 8.0F;
            constexpr float ControlW = 260.0F;
        } // namespace Layout

        enum class SettingsTab : int
        {
            General = 0,
            Appearance,
            Input,
            Rendering,
            Audio,
            Network,
            Diagnostics,
            Plugins,
        };

        struct NavItem
        {
            const char *label;
            const char *icon;
            SettingsTab tab;
        };

        void CopyString(char *dst, const size_t dstSize, const std::string &src)
        {
            if (!dst || dstSize == 0)
            {
                return;
            }
            std::snprintf(dst, dstSize, "%s", src.c_str());
        }

        [[nodiscard]] EditorSettings CollectDraftSettings(const SettingsState &st)
        {
            EditorSettings out{};
            out.startupBehavior = static_cast<EditorStartupBehavior>(std::clamp(st.startupAction, 0, 2));
            out.autoSaveIntervalMinutes = st.autoSaveInterval;
            out.confirmExitWithUnsavedChanges = st.confirmExit;
            out.restoreWorkspaceLayout = st.restoreWorkspace;
            out.defaultSceneOnProjectOpen = st.defaultScene;

            out.themePreset = EditorThemePreset::HoroDark;
            out.accentColorHex = st.accentHex;
            out.uiScalePercent = st.uiScale;
            out.codeFontSizePx = std::atoi(st.editorFontSize);

            out.orbitSensitivity = st.orbitSensitivity;
            out.panSensitivity = st.panSensitivity;
            out.invertOrbitY = st.invertOrbitY;

            out.viewportMode = static_cast<EditorViewportMode>(std::clamp(st.viewportMode, 0, 3));
            out.gridOverlay = st.gridOverlay;
            out.renderingTier = static_cast<EditorRenderingTier>(std::clamp(st.renderingTier, 0, 3));
            out.textureStreamingBudget = st.textureBudget;

            out.masterVolume = st.masterVolume;
            out.audioOutputDevice = static_cast<EditorAudioOutputDevice>(std::clamp(st.audioOutputDevice, 0, 2));
            out.audioEnabled = st.audioEnabled;

            out.maxPreviewClients = st.maxPreviewClients;
            out.simulatedLatencyMs = st.simulatedLatencyMs;
            out.packageDownloadThreads = st.packageDownloadThreads;

            out.consoleLogLevel = static_cast<EditorConsoleLogLevel>(std::clamp(st.consoleLogLevel, 0, 3));
            out.writeLogToFile = st.writeLogToFile;
            out.autoCaptureOnStutter = st.autoCaptureStutter;
            out.stutterThresholdMs = st.stutterThresholdMs;

            out.horoMcpBridgeEnabled = st.horoMcpBridge;
            out.fmodIntegrationEnabled = st.fmodIntegration;
            out.steamworksSdkEnabled = st.steamworksSdk;
            out.pluginDiscoveryPath = st.pluginPath;

            return out;
        }

        void ApplySettingsToDraft(SettingsState &st, const EditorSettings &settings)
        {
            st.startupAction = static_cast<int>(settings.startupBehavior);
            st.autoSaveInterval = settings.autoSaveIntervalMinutes;
            st.confirmExit = settings.confirmExitWithUnsavedChanges;
            st.restoreWorkspace = settings.restoreWorkspaceLayout;
            CopyString(st.defaultScene, sizeof(st.defaultScene), settings.defaultSceneOnProjectOpen);

            st.uiScale = settings.uiScalePercent;
            std::snprintf(st.editorFontSize, sizeof(st.editorFontSize), "%d", settings.codeFontSizePx);
            CopyString(st.accentHex, sizeof(st.accentHex), settings.accentColorHex);

            st.orbitSensitivity = settings.orbitSensitivity;
            st.panSensitivity = settings.panSensitivity;
            st.invertOrbitY = settings.invertOrbitY;

            st.viewportMode = static_cast<int>(settings.viewportMode);
            st.gridOverlay = settings.gridOverlay;
            st.renderingTier = static_cast<int>(settings.renderingTier);
            CopyString(st.textureBudget, sizeof(st.textureBudget), settings.textureStreamingBudget);

            st.masterVolume = settings.masterVolume;
            st.audioOutputDevice = static_cast<int>(settings.audioOutputDevice);
            st.audioEnabled = settings.audioEnabled;

            st.maxPreviewClients = settings.maxPreviewClients;
            st.simulatedLatencyMs = settings.simulatedLatencyMs;
            st.packageDownloadThreads = settings.packageDownloadThreads;

            st.consoleLogLevel = static_cast<int>(settings.consoleLogLevel);
            st.writeLogToFile = settings.writeLogToFile;
            st.autoCaptureStutter = settings.autoCaptureOnStutter;
            st.stutterThresholdMs = settings.stutterThresholdMs;

            st.horoMcpBridge = settings.horoMcpBridgeEnabled;
            st.fmodIntegration = settings.fmodIntegrationEnabled;
            st.steamworksSdk = settings.steamworksSdkEnabled;
            CopyString(st.pluginPath, sizeof(st.pluginPath), settings.pluginDiscoveryPath);
        }

        void LoadSettingsForModal(SettingsState &st)
        {
            st.document = LoadEditorSettingsDocument();
            st.committed = st.document.settings;
            ApplySettingsToDraft(st, st.committed);
            st.dirty = false;
            st.initialized = true;
            st.statusIsError = st.document.parseError;
            if (!st.document.error.empty())
            {
                st.statusMessage = st.document.error;
            }
            else
            {
                st.statusMessage = std::string{"Settings file: "} + st.document.path.string();
            }
        }

        [[nodiscard]] bool ApplySettings(SettingsState &st)
        {
            EditorSettings draft = CollectDraftSettings(st);
            std::string error;
            if (!ValidateEditorSettings(draft, &error))
            {
                ApplySettingsToDraft(st, draft);
                st.statusMessage = error;
                st.statusIsError = true;
                st.dirty = draft != st.committed;
                return false;
            }

            st.document.settings = draft;
            if (!SaveEditorSettingsDocument(&st.document, &error))
            {
                st.statusMessage = error.empty() ? "Failed to save editor settings." : error;
                st.statusIsError = true;
                st.dirty = true;
                return false;
            }

            st.committed = st.document.settings;
            ApplySettingsToDraft(st, st.committed);
            st.statusMessage = std::string{"Saved to "} + st.document.path.string();
            st.statusIsError = false;
            st.dirty = false;
            return true;
        }

        void DiscardSettingsAndClose(SettingsState &st)
        {
            ApplySettingsToDraft(st, st.committed);
            st.dirty = false;
            st.statusMessage = "Changes discarded.";
            st.statusIsError = false;
            st.open = false;
            ImGui::CloseCurrentPopup();
        }

        [[nodiscard]] ImVec4 AccentGlow()
        {
            return ImVec4{Theme::Accent().x, Theme::Accent().y, Theme::Accent().z, 0.14F};
        }

        void PushControlStyle()
        {
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2{10.0F, 8.0F});
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, Layout::Radius);
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0F);
            ImGui::PushStyleColor(ImGuiCol_FrameBg, Theme::Bg3());
            ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, Theme::Hover());
            ImGui::PushStyleColor(ImGuiCol_FrameBgActive, Theme::Hover());
            ImGui::PushStyleColor(ImGuiCol_Border, Theme::Border());
            ImGui::PushStyleColor(ImGuiCol_Text, Theme::Text());
        }

        void PopControlStyle()
        {
            ImGui::PopStyleColor(5);
            ImGui::PopStyleVar(3);
        }

        [[nodiscard]] bool ButtonCss(const char *label, const ImVec2 size, const bool primary, const Fonts &f)
        {
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2{16.0F, 8.0F});
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, Layout::Radius);
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0F);
            if (primary)
            {
                ImGui::PushStyleColor(ImGuiCol_Button, Theme::Accent());
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Theme::AccentHover());
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, Theme::AccentActive());
                ImGui::PushStyleColor(ImGuiCol_Text, Theme::DarkText());
                ImGui::PushStyleColor(ImGuiCol_Border, Theme::Accent());
            }
            else
            {
                ImGui::PushStyleColor(ImGuiCol_Button, Theme::Bg3());
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Theme::Hover());
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, Theme::Hover());
                ImGui::PushStyleColor(ImGuiCol_Text, Theme::Text());
                ImGui::PushStyleColor(ImGuiCol_Border, Theme::Border());
            }

            bool clicked = false;
            {
                ScopedTextStyle ts(f.mono, 13.0F, Theme::FontPx::Mono);
                clicked = ImGui::Button(label, size);
            }

            ImGui::PopStyleColor(5);
            ImGui::PopStyleVar(3);
            return clicked;
        }

        void SectionTitle(const char *label, const Fonts &f)
        {
            ScopedTextStyle ts(f.monoSemiBold, 18.0F, Theme::FontPx::MonoSemiBold);
            ImGui::PushStyleColor(ImGuiCol_Text, Theme::Text());
            ImGui::TextUnformatted(label);
            ImGui::PopStyleColor();
        }

        void SettingGroup(const char *label, const Fonts &f, const bool first = false)
        {
            if (!first)
            {
                ImGui::Dummy({0.0F, 18.0F});
            }

            ScopedTextStyle ts(f.monoSemiBold, 13.0F, Theme::FontPx::MonoSemiBold);
            ImGui::PushStyleColor(ImGuiCol_Text, Theme::Dim());
            ImGui::TextUnformatted(label);
            ImGui::PopStyleColor();

            const ImVec2 p = ImGui::GetCursorScreenPos();
            const float w = ImGui::GetContentRegionAvail().x;
            ImGui::GetWindowDrawList()->AddLine(p, {p.x + w, p.y}, Theme::U32(Theme::Border()), 1.0F);
            ImGui::Dummy({0.0F, 8.0F});
        }

        template <typename ControlFn>
        void SettingRow(const char *label, const char *description, const Fonts &f, ControlFn &&control)
        {
            ImGui::PushID(label);
            ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2{0.0F, 0.0F});
            if (ImGui::BeginTable("row", 2, ImGuiTableFlags_SizingStretchProp))
            {
                ImGui::TableSetupColumn("info", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("control", ImGuiTableColumnFlags_WidthFixed, Layout::ControlW);
                ImGui::TableNextRow();

                ImGui::TableSetColumnIndex(0);
                ImGui::BeginGroup();
                {
                    ScopedTextStyle ts(f.sans, 16.0F, Theme::FontPx::Sans);
                    ImGui::PushStyleColor(ImGuiCol_Text, Theme::Text());
                    ImGui::TextUnformatted(label);
                    ImGui::PopStyleColor();
                }
                if (description && description[0] != '\0')
                {
                    ScopedTextStyle ts(f.sans, 14.0F, Theme::FontPx::Sans);
                    ImGui::PushStyleColor(ImGuiCol_Text, Theme::Dim());
                    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - 16.0F);
                    ImGui::TextWrapped("%s", description);
                    ImGui::PopTextWrapPos();
                    ImGui::PopStyleColor();
                }
                ImGui::EndGroup();

                ImGui::TableSetColumnIndex(1);
                control();

                ImGui::EndTable();
            }
            ImGui::PopStyleVar();

            ImGui::Dummy({0.0F, 10.0F});
            const ImVec2 p = ImGui::GetCursorScreenPos();
            const float w = ImGui::GetContentRegionAvail().x;
            ImGui::GetWindowDrawList()->AddLine({p.x, p.y}, {p.x + w, p.y}, Theme::U32(Theme::Border()), 1.0F);
            ImGui::Dummy({0.0F, 10.0F});
            ImGui::PopID();
        }

        void ComboControl(const char *id, int *value, const char *const items[], const int itemCount, const Fonts &f)
        {
            PushControlStyle();
            ImGui::PushItemWidth(-1.0F);
            {
                ScopedTextStyle ts(f.mono, 13.0F, Theme::FontPx::Mono);
                ImGui::Combo(id, value, items, itemCount);
            }
            ImGui::PopItemWidth();
            PopControlStyle();
        }

        void InputTextControl(const char *id, char *buffer, const size_t bufferSize, const Fonts &f)
        {
            PushControlStyle();
            ImGui::PushItemWidth(-1.0F);
            {
                ScopedTextStyle ts(f.mono, 13.0F, Theme::FontPx::Mono);
                ImGui::InputText(id, buffer, bufferSize);
            }
            ImGui::PopItemWidth();
            PopControlStyle();
        }

        void InputIntControl(const char *id, int *value, const Fonts &f)
        {
            PushControlStyle();
            ImGui::PushItemWidth(-1.0F);
            {
                ScopedTextStyle ts(f.mono, 13.0F, Theme::FontPx::Mono);
                ImGui::InputInt(id, value, 1, 4);
            }
            ImGui::PopItemWidth();
            PopControlStyle();
        }

        void InputFloatControl(const char *id, float *value, const Fonts &f)
        {
            PushControlStyle();
            ImGui::PushItemWidth(-1.0F);
            {
                ScopedTextStyle ts(f.mono, 13.0F, Theme::FontPx::Mono);
                ImGui::InputFloat(id, value, 0.1F, 1.0F, "%.1f");
            }
            ImGui::PopItemWidth();
            PopControlStyle();
        }

        void SliderIntControl(const char *id,
                              int *value,
                              const int minValue,
                              const int maxValue,
                              const char *suffix,
                              const Fonts &f,
                              const int step = 1)
        {
            ImGui::PushID(id);
            constexpr float TrackW = Layout::ControlW - 54.0F;
            constexpr float TrackH = 4.0F;
            constexpr float HitH = 22.0F;
            constexpr float KnobR = 7.0F;

            const ImVec2 pos = ImGui::GetCursorScreenPos();
            ImGui::InvisibleButton("slider", {TrackW, HitH});
            const bool active = ImGui::IsItemActive();
            const bool hovered = ImGui::IsItemHovered();

            if (active && maxValue > minValue)
            {
                const float mouseT = (ImGui::GetIO().MousePos.x - pos.x) / TrackW;
                const float clampedT = std::clamp(mouseT, 0.0F, 1.0F);
                const float rawValue = static_cast<float>(minValue) +
                                       clampedT * static_cast<float>(maxValue - minValue);
                const int snapped = minValue +
                                    static_cast<int>(std::round((rawValue - static_cast<float>(minValue)) /
                                                                static_cast<float>(step))) *
                                        step;
                *value = std::clamp(snapped, minValue, maxValue);
            }

            const float t = maxValue > minValue
                                ? (static_cast<float>(*value - minValue) / static_cast<float>(maxValue - minValue))
                                : 0.0F;
            const float trackY = pos.y + (HitH - TrackH) * 0.5F;
            auto *dl = ImGui::GetWindowDrawList();
            dl->AddRectFilled({pos.x, trackY}, {pos.x + TrackW, trackY + TrackH}, Theme::U32(Theme::BorderStrong()), 2.0F);
            dl->AddRectFilled({pos.x, trackY}, {pos.x + TrackW * t, trackY + TrackH}, Theme::U32(Theme::Accent()), 2.0F);

            const ImVec2 knob{pos.x + TrackW * t, pos.y + HitH * 0.5F};
            dl->AddCircleFilled(knob, KnobR + (hovered || active ? 1.0F : 0.0F), Theme::U32(Theme::AccentHover()), 20);
            dl->AddCircleFilled(knob, KnobR, Theme::U32(Theme::Accent()), 20);
            dl->AddCircle(knob, KnobR + 1.0F, Theme::U32(Theme::Bg1()), 20, 2.0F);

            ImGui::SameLine(0.0F, 10.0F);

            char text[32]{};
            std::snprintf(text, sizeof(text), suffix, *value);
            {
                ScopedTextStyle ts(f.mono, 13.0F, Theme::FontPx::Mono);
                const ImVec2 textSize = ImGui::CalcTextSize(text);
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (HitH - textSize.y) * 0.5F);
                ImGui::PushStyleColor(ImGuiCol_Text, Theme::Text());
                ImGui::TextUnformatted(text);
                ImGui::PopStyleColor();
            }
            ImGui::PopID();
        }

        [[nodiscard]] bool ToggleControl(const char *id, bool *value, const Fonts &f, const bool showLabel = true)
        {
            ImGui::PushID(id);
            const ImVec2 pos = ImGui::GetCursorScreenPos();
            const ImVec2 size{36.0F, 20.0F};
            ImGui::InvisibleButton("toggle", size);
            const bool clicked = ImGui::IsItemClicked();
            if (clicked)
            {
                *value = !*value;
            }

            const bool hovered = ImGui::IsItemHovered();
            auto *dl = ImGui::GetWindowDrawList();
            const ImVec4 bg = *value ? Theme::Accent() : (hovered ? Theme::Hover() : Theme::Bg3());
            dl->AddRectFilled(pos, {pos.x + size.x, pos.y + size.y}, Theme::U32(bg), 10.0F);
            dl->AddRect(pos, {pos.x + size.x, pos.y + size.y}, Theme::U32(*value ? Theme::Accent() : Theme::Border()), 10.0F);
            const float knobX = *value ? pos.x + 21.0F : pos.x + 3.0F;
            dl->AddCircleFilled({knobX + 6.0F, pos.y + 10.0F}, 6.0F, Theme::U32(*value ? ImVec4{1, 1, 1, 1} : Theme::Dim()), 16);

            if (showLabel)
            {
                ImGui::SameLine(0.0F, 10.0F);
                ScopedTextStyle ts(f.sans, 13.0F, Theme::FontPx::Sans);
                ImGui::PushStyleColor(ImGuiCol_Text, Theme::Muted());
                ImGui::TextUnformatted(*value ? "Enabled" : "Disabled");
                ImGui::PopStyleColor();
            }
            ImGui::PopID();
            return clicked;
        }

        void ThemeChip(const char *label, const ImVec4 swatch, const bool active, const Fonts &f)
        {
            ImGui::PushID(label);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2{12.0F, 7.0F});
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, Layout::Radius);
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0F);
            ImGui::PushStyleColor(ImGuiCol_Button, active ? AccentGlow() : Theme::Bg3());
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Theme::Hover());
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, Theme::Hover());
            ImGui::PushStyleColor(ImGuiCol_Text, active ? Theme::Text() : Theme::Muted());
            ImGui::PushStyleColor(ImGuiCol_Border, active ? Theme::Accent() : Theme::Border());
            {
                ScopedTextStyle ts(f.mono, 12.0F, Theme::FontPx::Mono);
                ImGui::Button(label, ImVec2{82.0F, 32.0F});
            }
            const ImVec2 min = ImGui::GetItemRectMin();
            ImGui::GetWindowDrawList()->AddCircleFilled({min.x + 12.0F, min.y + 16.0F}, 5.0F, Theme::U32(swatch), 16);
            ImGui::PopStyleColor(5);
            ImGui::PopStyleVar(3);
            ImGui::PopID();
        }

        void ShortcutDisplay(const char *a, const char *b, const char *c, const Fonts &f)
        {
            const std::array<const char *, 3> keys = {a, b, c};
            for (int i = 0; i < 3; ++i)
            {
                if (!keys[i] || keys[i][0] == '\0')
                    continue;
                if (i > 0)
                {
                    ImGui::SameLine(0.0F, 4.0F);
                    ScopedTextStyle plus(f.mono, 12.0F, Theme::FontPx::Mono);
                    ImGui::PushStyleColor(ImGuiCol_Text, Theme::Dim());
                    ImGui::TextUnformatted("+");
                    ImGui::PopStyleColor();
                    ImGui::SameLine(0.0F, 4.0F);
                }
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2{7.0F, 3.0F});
                ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, Layout::Radius);
                ImGui::PushStyleColor(ImGuiCol_Button, Theme::Bg3());
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Theme::Bg3());
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, Theme::Bg3());
                ImGui::PushStyleColor(ImGuiCol_Text, Theme::Text());
                ScopedTextStyle keyText(f.mono, 12.0F, Theme::FontPx::Mono);
                ImGui::Button(keys[i], ImVec2{0.0F, 24.0F});
                ImGui::PopStyleColor(4);
                ImGui::PopStyleVar(2);
            }
        }

        void PluginRow(const char *name, const char *version, const char *description, bool *enabled, const Fonts &f)
        {
            SettingRow(name, nullptr, f, [&f, version, description, enabled]() {
                ImGui::BeginGroup();
                {
                    ScopedTextStyle ts(f.mono, 10.5F, Theme::FontPx::Mono);
                    ImGui::PushStyleColor(ImGuiCol_Text, Theme::Dim());
                    ImGui::TextUnformatted(version);
                    ImGui::PopStyleColor();
                }
                {
                    ScopedTextStyle ts(f.sans, 12.0F, Theme::FontPx::Sans);
                    ImGui::PushStyleColor(ImGuiCol_Text, Theme::Dim());
                    ImGui::TextWrapped("%s", description);
                    ImGui::PopStyleColor();
                }
                ImGui::EndGroup();
                ImGui::SameLine(Layout::ControlW - 42.0F);
                (void)ToggleControl("plugin-toggle", enabled, f, false);
            });
        }

        void DrawNavGroup(const char *label, const Fonts &f)
        {
            ImGui::Dummy({0.0F, 5.0F});
            ScopedTextStyle ts(f.monoSemiBold, 12.0F, Theme::FontPx::MonoSemiBold);
            ImGui::PushStyleColor(ImGuiCol_Text, Theme::Dim());
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 10.0F);
            ImGui::TextUnformatted(label);
            ImGui::PopStyleColor();
        }

        void DrawNavItem(SettingsState &st, const NavItem item, const Fonts &f)
        {
            const bool active = st.activeTab == static_cast<int>(item.tab);
            const ImVec2 pos = ImGui::GetCursorScreenPos();
            const float rowW = ImGui::GetContentRegionAvail().x;
            constexpr float rowH = 38.0F;

            ImGui::PushID(item.label);
            ImGui::InvisibleButton("nav", {rowW, rowH});
            if (ImGui::IsItemClicked())
            {
                st.activeTab = static_cast<int>(item.tab);
            }
            const bool hovered = ImGui::IsItemHovered();

            auto *dl = ImGui::GetWindowDrawList();
            if (active || hovered)
            {
                dl->AddRectFilled(pos, {pos.x + rowW, pos.y + rowH}, Theme::U32(active ? AccentGlow() : Theme::Hover()), Layout::Radius);
            }
            if (active)
            {
                dl->AddRectFilled(pos, {pos.x + 2.0F, pos.y + rowH}, Theme::U32(Theme::Accent()), 1.0F);
            }

            ImGui::SetCursorScreenPos({pos.x + 12.0F, pos.y + 10.0F});
            {
                ScopedTextStyle ts(f.monoSemiBold, 14.0F, Theme::FontPx::MonoSemiBold);
                ImGui::PushStyleColor(ImGuiCol_Text, active ? Theme::Accent() : Theme::Muted());
                ImGui::TextUnformatted(item.icon);
                ImGui::PopStyleColor();
            }
            ImGui::SameLine(0.0F, 10.0F);
            {
                ScopedTextStyle ts(f.sans, 15.0F, Theme::FontPx::Sans);
                ImGui::PushStyleColor(ImGuiCol_Text, active ? Theme::Text() : Theme::Muted());
                ImGui::TextUnformatted(item.label);
                ImGui::PopStyleColor();
            }
            ImGui::SetCursorScreenPos({pos.x, pos.y + rowH + 1.0F});
            ImGui::PopID();
        }

        void DrawHeader(SettingsState &st, const Fonts &f, const ::ImTextureID logo)
        {
            ImGui::PushStyleColor(ImGuiCol_ChildBg, Theme::Bg0());
            ImGui::BeginChild("SettingsHeader", {0.0F, Layout::HeaderH}, false,
                              ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
            ImGui::SetCursorPos({22.0F, 16.0F});
            if (logo != 0)
            {
                ImGui::Image(logo, {20.0F, 20.0F});
                ImGui::SameLine(0.0F, 10.0F);
            }
            else
            {
                const ImVec2 mark = ImGui::GetCursorScreenPos();
                ImGui::GetWindowDrawList()->AddRectFilled({mark.x + 4.0F, mark.y + 4.0F}, {mark.x + 14.0F, mark.y + 14.0F}, Theme::U32(Theme::Accent()), 2.0F);
                ImGui::Dummy({20.0F, 20.0F});
                ImGui::SameLine(0.0F, 10.0F);
            }
            {
                ScopedTextStyle ts(f.monoSemiBold, 13.0F, Theme::FontPx::MonoSemiBold);
                ImGui::PushStyleColor(ImGuiCol_Text, Theme::Text());
                ImGui::TextUnformatted("EDITOR SETTINGS");
                ImGui::PopStyleColor();
            }

            ImGui::SetCursorPos({ImGui::GetWindowWidth() - 50.0F, 10.0F});
            if (Ui::IconCloseButton("close-settings", {28.0F, 28.0F}))
            {
                DiscardSettingsAndClose(st);
            }

            const ImVec2 p = ImGui::GetWindowPos();
            ImGui::GetWindowDrawList()->AddLine({p.x, p.y + Layout::HeaderH - 1.0F},
                                                {p.x + ImGui::GetWindowWidth(), p.y + Layout::HeaderH - 1.0F},
                                                Theme::U32(Theme::Border()),
                                                1.0F);
            ImGui::EndChild();
            ImGui::PopStyleColor();
        }

        void DrawNavigation(SettingsState &st, const Fonts &f, const float bodyH)
        {
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{8.0F, 10.0F});
            ImGui::PushStyleColor(ImGuiCol_ChildBg, Theme::Bg0());
            ImGui::BeginChild("SettingsNav", {Layout::NavW, bodyH}, false, ImGuiWindowFlags_AlwaysUseWindowPadding);

            DrawNavGroup("Editor", f);
            DrawNavItem(st, {"General", "G", SettingsTab::General}, f);
            DrawNavItem(st, {"Appearance", "A", SettingsTab::Appearance}, f);
            DrawNavItem(st, {"Input", "I", SettingsTab::Input}, f);
            DrawNavGroup("Engine", f);
            DrawNavItem(st, {"Rendering", "R", SettingsTab::Rendering}, f);
            DrawNavItem(st, {"Audio", "S", SettingsTab::Audio}, f);
            DrawNavItem(st, {"Network", "N", SettingsTab::Network}, f);
            DrawNavGroup("Tools", f);
            DrawNavItem(st, {"Diagnostics", "D", SettingsTab::Diagnostics}, f);
            DrawNavItem(st, {"Plugins", "P", SettingsTab::Plugins}, f);

            const ImVec2 p = ImGui::GetWindowPos();
            ImGui::GetWindowDrawList()->AddLine({p.x + Layout::NavW - 1.0F, p.y},
                                                {p.x + Layout::NavW - 1.0F, p.y + bodyH},
                                                Theme::U32(Theme::Border()),
                                                1.0F);
            ImGui::EndChild();
            ImGui::PopStyleColor();
            ImGui::PopStyleVar();
        }

        void DrawGeneral(SettingsState &st, const Fonts &f)
        {
            static constexpr std::array<const char *, 3> kStartup = {"Welcome screen", "Last project", "Project browser"};
            SectionTitle("General", f);
            SettingGroup("STARTUP & PROJECT", f, true);
            SettingRow("Startup Behavior", "What to show when the editor launches.", f,
                       [&st, &f]() { ComboControl("##startup", &st.startupAction, kStartup.data(), static_cast<int>(kStartup.size()), f); });
            SettingRow("Auto-save Interval", "Minutes between automatic scene saves. Zero disables auto-save.", f,
                       [&st, &f]() { SliderIntControl("##autosave", &st.autoSaveInterval, 0, 30, "%d min", f); });
            SettingRow("Confirm Exit With Unsaved Changes", nullptr, f,
                       [&st, &f]() { (void)ToggleControl("confirm-exit", &st.confirmExit, f); });
            SettingGroup("EDITOR SESSION", f);
            SettingRow("Restore Workspace Layout", "Reopen tabs and panel layout from last session.", f,
                       [&st, &f]() { (void)ToggleControl("restore-workspace", &st.restoreWorkspace, f); });
            SettingRow("Default Scene On Project Open", nullptr, f,
                       [&st, &f]() { InputTextControl("##default-scene", st.defaultScene, sizeof(st.defaultScene), f); });
        }

        void DrawAppearance(SettingsState &st, const Fonts &f)
        {
            SectionTitle("Appearance", f);
            SettingGroup("THEME", f, true);
            SettingRow("Color Theme", "Base editor chrome palette.", f, [&st, &f]() {
                ThemeChip("  Horo Dark", Theme::Bg1(), true, f);
                ImGui::SameLine(0.0F, 6.0F);
                ThemeChip("  Midnight", ImVec4{0.063F, 0.090F, 0.133F, 1.0F}, false, f);
                ImGui::SameLine(0.0F, 6.0F);
                ThemeChip("  Light", ImVec4{0.941F, 0.925F, 0.890F, 1.0F}, false, f);
            });
            SettingRow("Accent Color", "Used for focus rings, active states, and primary actions.", f, [&st, &f]() {
                const ImVec2 p = ImGui::GetCursorScreenPos();
                ImGui::GetWindowDrawList()->AddRectFilled(p, {p.x + 34.0F, p.y + 34.0F}, Theme::U32(Theme::Accent()), Layout::Radius);
                ImGui::Dummy({42.0F, 34.0F});
                ImGui::SameLine(0.0F, 8.0F);
                ImGui::PushItemWidth(Layout::ControlW - 50.0F);
                InputTextControl("##accent", st.accentHex, sizeof(st.accentHex), f);
                ImGui::PopItemWidth();
            });
            SettingGroup("TYPOGRAPHY & SCALE", f);
            SettingRow("UI Scale", "Scales all editor chrome uniformly.", f,
                       [&st, &f]() { SliderIntControl("##ui-scale", &st.uiScale, 75, 200, "%d%%", f, 25); });
            SettingRow("Code Font Size", nullptr, f,
                       [&st, &f]() { InputTextControl("##font-size", st.editorFontSize, sizeof(st.editorFontSize), f); });
        }

        void DrawInput(SettingsState &st, const Fonts &f)
        {
            SectionTitle("Input", f);
            SettingGroup("NAVIGATION", f, true);
            SettingRow("Orbit Sensitivity", nullptr, f,
                       [&st, &f]() { SliderIntControl("##orbit", &st.orbitSensitivity, 10, 300, "%d", f); });
            SettingRow("Pan Sensitivity", nullptr, f,
                       [&st, &f]() { SliderIntControl("##pan", &st.panSensitivity, 10, 300, "%d", f); });
            SettingRow("Invert Orbit Y", nullptr, f,
                       [&st, &f]() { (void)ToggleControl("invert-y", &st.invertOrbitY, f); });
            SettingGroup("SHORTCUTS", f);
            SettingRow("Save Scene", nullptr, f, [&f]() { ShortcutDisplay("Ctrl", "S", nullptr, f); });
            SettingRow("Undo", nullptr, f, [&f]() { ShortcutDisplay("Ctrl", "Z", nullptr, f); });
            SettingRow("Build & Release", nullptr, f, [&f]() { ShortcutDisplay("Ctrl", "Shift", "B", f); });
        }

        void DrawRendering(SettingsState &st, const Fonts &f)
        {
            static constexpr std::array<const char *, 4> kViewport = {"Shaded", "Wireframe", "Lit", "Unlit"};
            static constexpr std::array<const char *, 4> kTier = {"High End", "DX12 / Vulkan", "DX11", "ES3"};
            SectionTitle("Rendering", f);
            SettingGroup("VIEWPORT", f, true);
            SettingRow("Default Viewport Mode", nullptr, f, [&st, &f]() { ComboControl("##viewport", &st.viewportMode, kViewport.data(), static_cast<int>(kViewport.size()), f); });
            SettingRow("Grid Overlay", nullptr, f, [&st, &f]() { (void)ToggleControl("grid", &st.gridOverlay, f); });
            SettingGroup("QUALITY", f);
            SettingRow("Editor Rendering Tier", "Maximum feature tier used in the editor viewport.", f,
                       [&st, &f]() { ComboControl("##tier", &st.renderingTier, kTier.data(), static_cast<int>(kTier.size()), f); });
            SettingRow("Texture Streaming Budget", nullptr, f,
                       [&st, &f]() { InputTextControl("##texture-budget", st.textureBudget, sizeof(st.textureBudget), f); });
        }

        void DrawAudio(SettingsState &st, const Fonts &f)
        {
            static constexpr std::array<const char *, 3> kDevices = {"System Default", "Headphones", "Speakers"};
            SectionTitle("Audio", f);
            SettingGroup("OUTPUT", f, true);
            SettingRow("Master Volume", nullptr, f,
                       [&st, &f]() { SliderIntControl("##volume", &st.masterVolume, 0, 100, "%d", f); });
            SettingRow("Audio Output Device", nullptr, f,
                       [&st, &f]() { ComboControl("##audio-device", &st.audioOutputDevice, kDevices.data(), static_cast<int>(kDevices.size()), f); });
            SettingRow("Enable Audio In Editor", nullptr, f,
                       [&st, &f]() { (void)ToggleControl("audio-enabled", &st.audioEnabled, f); });
        }

        void DrawNetwork(SettingsState &st, const Fonts &f)
        {
            SectionTitle("Network", f);
            SettingGroup("MULTIPLAYER PREVIEW", f, true);
            SettingRow("Max Preview Clients", nullptr, f,
                       [&st, &f]() { InputIntControl("##max-clients", &st.maxPreviewClients, f); });
            SettingRow("Simulate Latency", "Artificial one-way delay injected on loopback (ms).", f,
                       [&st, &f]() { SliderIntControl("##latency", &st.simulatedLatencyMs, 0, 500, "%d ms", f); });
            SettingRow("Package Download Threads", nullptr, f,
                       [&st, &f]() { InputIntControl("##download-threads", &st.packageDownloadThreads, f); });
        }

        void DrawDiagnostics(SettingsState &st, const Fonts &f)
        {
            static constexpr std::array<const char *, 4> kLogLevels = {"Debug", "Info", "Warning", "Error"};
            SectionTitle("Diagnostics", f);
            SettingGroup("LOGGING", f, true);
            SettingRow("Console Log Level", nullptr, f,
                       [&st, &f]() { ComboControl("##log-level", &st.consoleLogLevel, kLogLevels.data(), static_cast<int>(kLogLevels.size()), f); });
            SettingRow("Write Log To File", nullptr, f,
                       [&st, &f]() { (void)ToggleControl("write-log", &st.writeLogToFile, f); });
            SettingGroup("PROFILER", f);
            SettingRow("Auto-capture On Stutter", nullptr, f,
                       [&st, &f]() { (void)ToggleControl("capture-stutter", &st.autoCaptureStutter, f); });
            SettingRow("Stutter Threshold (ms)", nullptr, f,
                       [&st, &f]() { InputFloatControl("##stutter", &st.stutterThresholdMs, f); });
        }

        void DrawPlugins(SettingsState &st, const Fonts &f)
        {
            SectionTitle("Plugins", f);
            SettingGroup("INSTALLED", f, true);
            PluginRow("Horo MCP Bridge", "v0.4.0", "Enables MCP tool access for scene and asset operations.", &st.horoMcpBridge, f);
            PluginRow("Vendor FMOD Integration", "v2.02.20", "Full FMOD Studio authoring and runtime integration.", &st.fmodIntegration, f);
            PluginRow("Steamworks SDK", "v1.59", "Steam achievements, overlay, and networking features.", &st.steamworksSdk, f);
            SettingGroup("DISCOVERY", f);
            SettingRow("Plugin Discovery Path", nullptr, f,
                       [&st, &f]() { InputTextControl("##plugin-path", st.pluginPath, sizeof(st.pluginPath), f); });
        }

        void DrawContent(SettingsState &st, const Fonts &f, const float bodyH)
        {
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{26.0F, 22.0F});
            ImGui::PushStyleColor(ImGuiCol_ChildBg, Theme::Bg1());
            ImGui::BeginChild("SettingsContent", {0.0F, bodyH}, false, ImGuiWindowFlags_AlwaysUseWindowPadding);

            switch (static_cast<SettingsTab>(st.activeTab))
            {
            case SettingsTab::General:
                DrawGeneral(st, f);
                break;
            case SettingsTab::Appearance:
                DrawAppearance(st, f);
                break;
            case SettingsTab::Input:
                DrawInput(st, f);
                break;
            case SettingsTab::Rendering:
                DrawRendering(st, f);
                break;
            case SettingsTab::Audio:
                DrawAudio(st, f);
                break;
            case SettingsTab::Network:
                DrawNetwork(st, f);
                break;
            case SettingsTab::Diagnostics:
                DrawDiagnostics(st, f);
                break;
            case SettingsTab::Plugins:
                DrawPlugins(st, f);
                break;
            }

            ImGui::EndChild();
            ImGui::PopStyleColor();
            ImGui::PopStyleVar();
        }

        void DrawFooter(SettingsState &st, const Fonts &f)
        {
            ImGui::PushStyleColor(ImGuiCol_ChildBg, Theme::Bg0());
            ImGui::BeginChild("SettingsFooter", {0.0F, Layout::FooterH}, false,
                              ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
            const ImVec2 p = ImGui::GetWindowPos();
            ImGui::GetWindowDrawList()->AddLine(p, {p.x + ImGui::GetWindowWidth(), p.y}, Theme::U32(Theme::Border()), 1.0F);

            ImGui::SetCursorPos({22.0F, 22.0F});
            if (st.dirty)
            {
                ScopedTextStyle badge(f.mono, 10.5F, Theme::FontPx::Mono);
                ImGui::PushStyleColor(ImGuiCol_Text, Theme::Warn());
                ImGui::TextUnformatted("unsaved");
                ImGui::PopStyleColor();
                ImGui::SameLine(0.0F, 8.0F);
            }
            {
                ScopedTextStyle hint(f.mono, 11.5F, Theme::FontPx::Mono);
                ImGui::PushStyleColor(ImGuiCol_Text, st.statusIsError ? Theme::Err() : Theme::Dim());
                ImGui::TextUnformatted(st.statusMessage.empty() ? "Apply writes user preferences to ~/.horo/editor_settings.json" :
                                                                  st.statusMessage.c_str());
                ImGui::PopStyleColor();
            }

            constexpr float restoreW = 132.0F;
            constexpr float cancelW = 82.0F;
            constexpr float applyW = 74.0F;
            constexpr float gap = 8.0F;
            const float actionsW = restoreW + cancelW + applyW + gap * 2.0F;
            ImGui::SetCursorPos({ImGui::GetWindowWidth() - 22.0F - actionsW, 15.0F});
            if (ButtonCss("Restore Defaults", {restoreW, 34.0F}, false, f))
            {
                ApplySettingsToDraft(st, DefaultEditorSettings());
                st.statusMessage = "Defaults loaded into draft. Apply to persist.";
                st.statusIsError = false;
            }
            ImGui::SameLine(0.0F, gap);
            if (ButtonCss("Cancel", {cancelW, 34.0F}, false, f))
            {
                DiscardSettingsAndClose(st);
            }
            ImGui::SameLine(0.0F, gap);
            if (ButtonCss("Apply", {applyW, 34.0F}, true, f))
            {
                (void)ApplySettings(st);
            }

            ImGui::EndChild();
            ImGui::PopStyleColor();
        }

        void DrawSettingsModalImpl(SettingsState &st, const Fonts &f, const ::ImTextureID logo)
        {
            if (!st.open)
            {
                st.wasOpen = false;
                return;
            }

            if (!st.wasOpen || !st.initialized)
            {
                LoadSettingsForModal(st);
                st.wasOpen = true;
            }

            const ImGuiViewport *vp = ImGui::GetMainViewport();
            const float modalW = std::min(Layout::ModalW, std::max(360.0F, vp->WorkSize.x - Layout::ViewportPad));
            const float modalH = std::min(Layout::ModalH, std::max(360.0F, vp->WorkSize.y - Layout::ViewportPad));
            const ImVec2 modalPos{vp->WorkPos.x + (vp->WorkSize.x - modalW) * 0.5F,
                                  vp->WorkPos.y + (vp->WorkSize.y - modalH) * 0.5F};

            ImGui::SetNextWindowPos(modalPos, ImGuiCond_Always);
            ImGui::SetNextWindowSize({modalW, modalH}, ImGuiCond_Always);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{0.0F, 0.0F});
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, Layout::ModalRadius);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0F);
            ImGui::PushStyleColor(ImGuiCol_WindowBg, Theme::Bg1());
            ImGui::PushStyleColor(ImGuiCol_Border, Theme::Border());

            const bool isOpen = ImGui::BeginPopupModal("Settings",
                                                       &st.open,
                                                       ImGuiWindowFlags_NoResize |
                                                           ImGuiWindowFlags_NoTitleBar |
                                                           ImGuiWindowFlags_NoMove |
                                                           ImGuiWindowFlags_NoSavedSettings |
                                                           ImGuiWindowFlags_NoScrollbar |
                                                           ImGuiWindowFlags_NoScrollWithMouse);
            if (!isOpen)
            {
                ImGui::PopStyleColor(2);
                ImGui::PopStyleVar(3);
                return;
            }

            DrawHeader(st, f, logo);
            const float bodyH = ImGui::GetWindowHeight() - Layout::HeaderH - Layout::FooterH;
            DrawNavigation(st, f, bodyH);
            ImGui::SameLine(0.0F, 0.0F);
            DrawContent(st, f, bodyH);
            st.dirty = CollectDraftSettings(st) != st.committed;
            DrawFooter(st, f);

            if (!st.open)
            {
                st.wasOpen = false;
            }

            ImGui::EndPopup();
            ImGui::PopStyleColor(2);
            ImGui::PopStyleVar(3);
        }

    } // namespace

    /** @copydoc DrawSettingsModal */
    void DrawSettingsModal(SettingsState &state, const Theme::Fonts &fonts, const ::ImTextureID logo)
    {
        DrawSettingsModalImpl(state, fonts, logo);
    }

} // namespace Horo::Editor
