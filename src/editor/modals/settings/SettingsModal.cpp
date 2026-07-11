#include <cstring>
#include "editor/modals/settings/SettingsModal.h"
#include "Horo/Editor/SettingsModal.h"

#include "Horo/Editor/EditorSettingsStore.h"
#include "Horo/Editor/EditorTheme.h"
#include "Horo/Foundation/Logging/Logger.h"
#include "Horo/Editor/EditorUiComponents.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <vector>

namespace Horo::Editor
{
    namespace
    {
        using Theme::Fonts;
        using namespace Theme;

        // Forward declarations for plugin detail functions
        void DrawInstalledPlugins(SettingsState &st, const Fonts &f);
        void DrawPluginDetailPanel(SettingsState &st, const Fonts &f, float w, bool embedded = false);
        void DrawMcpDetailContent(SettingsState &st, const Fonts &f, int activeTab);
        void DrawFmodDetailContent(SettingsState &st, const Fonts &f, int activeTab);
        void DrawSteamDetailContent(SettingsState &st, const Fonts &f, int activeTab);
        void DrawRuntimeDiscovery(SettingsState &st, const Fonts &f);
        using namespace Ui;
        using Theme::ScopedTextStyle;

        namespace Layout
        {
            constexpr float ModalW = 960.0F;
            constexpr float ModalH = 680.0F;
            constexpr float ViewportPad = 48.0F;
            constexpr float HeaderH = 57.0F;
            constexpr float FooterH = 72.0F;
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

        void DiscardSettings(SettingsState &st)
        {
            ApplySettingsToDraft(st, st.committed);
            st.dirty = false;
            st.statusMessage = "Changes discarded.";
            st.statusIsError = false;
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
                const auto accentGlow = ImVec4{Theme::Accent().x, Theme::Accent().y, Theme::Accent().z, 0.14F};
            dl->AddRectFilled(pos, {pos.x + rowW, pos.y + rowH}, Theme::U32(active ? accentGlow : Theme::Hover()), Layout::Radius);
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

        [[nodiscard]] bool DrawHeader(SettingsState &st, const Fonts &f, const ::ImTextureID logo)
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
            bool requestClose = false;
            if (Ui::IconCloseButton("close-settings", {28.0F, 28.0F}))
            {
                requestClose = true;
            }

            const ImVec2 p = ImGui::GetWindowPos();
            ImGui::GetWindowDrawList()->AddLine({p.x, p.y + Layout::HeaderH - 1.0F},
                                                {p.x + ImGui::GetWindowWidth(), p.y + Layout::HeaderH - 1.0F},
                                                Theme::U32(Theme::Border()),
                                                1.0F);
            ImGui::EndChild();
            ImGui::PopStyleColor();
            return requestClose;
        }

        void DrawNavigation(SettingsState &st, const Fonts &f, const float bodyH)
        {
            using enum SettingsTab;
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{8.0F, 10.0F});
            ImGui::PushStyleColor(ImGuiCol_ChildBg, Theme::Bg0());
            ImGui::BeginChild("SettingsNav", {Layout::NavW, bodyH}, false, ImGuiWindowFlags_AlwaysUseWindowPadding);

            DrawNavGroup("Editor", f);
            DrawNavItem(st, {"General", "G", General}, f);
            DrawNavItem(st, {"Appearance", "A", Appearance}, f);
            DrawNavItem(st, {"Input", "I", Input}, f);
            DrawNavGroup("Engine", f);
            DrawNavItem(st, {"Rendering", "R", Rendering}, f);
            DrawNavItem(st, {"Audio", "S", Audio}, f);
            DrawNavItem(st, {"Network", "N", Network}, f);
            DrawNavGroup("Tools", f);
            DrawNavItem(st, {"Diagnostics", "D", Diagnostics}, f);
            DrawNavItem(st, {"Plugins", "P", Plugins}, f);

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
                       [&st, &f]() { (void)ComboControl("##startup", &st.general.startupAction, kStartup.data(), static_cast<int>(kStartup.size()), f); });
            SettingRow("Auto-save Interval", "Minutes between automatic scene saves. Zero disables auto-save.", f,
                       [&st, &f]() { SliderIntControl("##autosave", &st.general.autoSaveInterval, 0, 30, SliderValueFormat::Minutes, f); });
            SettingRow("Confirm Exit With Unsaved Changes", "Prompt before closing when unsaved changes exist.", f,
                       [&st, &f]() { (void)ToggleControl("confirm-exit", &st.general.confirmExit, f); });
            SettingGroup("EDITOR SESSION", f);
            SettingRow("Restore Workspace Layout", "Reopen tabs and panel layout from last session.", f,
                       [&st, &f]() { (void)ToggleControl("restore-workspace", &st.general.restoreWorkspace, f); });
            SettingRow("Default Scene On Project Open", "Scene file loaded automatically when opening a project.", f,
                       [&st, &f]() { (void)InputTextControl("##default-scene", st.general.defaultScene, 64, f); });
        }

        void DrawAppearance(SettingsState &st, const Fonts &f)
        {
            SectionTitle("Appearance", f);
            SettingGroup("THEME", f, true);
            SettingRow("Color Theme", "Built-in themes or custom JSON theme file.", f, [&st, &f]() {
                const auto &themeList = Theme::GetThemeList();
                static std::vector<const char *> s_names;
                s_names.clear();
                for (const auto &t : themeList)
                    s_names.push_back(t.name.c_str());

                const int count = static_cast<int>(s_names.size());
                if (st.appearance.themeIndex >= count)
                    st.appearance.themeIndex = 0;

                const int prev = st.appearance.themeIndex;
                (void)ComboControl("##theme", &st.appearance.themeIndex, s_names.data(), count, f);
                if (st.appearance.themeIndex != prev)
                {
                    // Defer: apply at start of next frame to avoid mid-frame style glitches
                    st.appearance.pendingThemeIndex = st.appearance.themeIndex;
                    st.dirty = true;
                }
            });
            SettingRow("Custom Theme", "Path to a JSON theme file. Leave empty to use built-in.", f, [&st, &f]() {
                (void)InputTextControl("##custom-theme", st.appearance.customThemePath, 128, f);
            });
            SettingRow("Accent Color", "Used for focus rings, active states, and primary actions.", f, [&st, &f]() {
                (void)ColorHexControl("accent-color", st.appearance.accentHex, 16, f);
            });
            SettingGroup("TYPOGRAPHY & SCALE", f);
            SettingRow("UI Scale", "Scales all editor chrome uniformly.", f,
                       [&st, &f]() { SliderIntControl("##ui-scale", &st.appearance.uiScale, 75, 200, SliderValueFormat::Percent, f, 25); });
            SettingRow("Code Font Size", "Point size for the script editor and console output.", f,
                       [&st, &f]() { (void)InputTextControl("##font-size", st.appearance.editorFontSize, 8, f); });
        }

        void ResolveShortcutConflicts(SettingsState::InputTab &input, const int editedIndex)
        {
            for (int index = 0; index < SettingsState::InputTab::kShortcutActionCount; ++index)
                input.shortcuts[index].conflict = false;
            if (input.shortcuts[editedIndex].keys[0] == '\0') return;
            for (int index = 0; index < SettingsState::InputTab::kShortcutActionCount; ++index)
            {
                if (index == editedIndex || input.shortcuts[index].keys[0] == '\0') continue;
                if (std::strcmp(input.shortcuts[editedIndex].keys, input.shortcuts[index].keys) == 0)
                {
                    input.shortcuts[editedIndex].conflict = true;
                    input.shortcuts[index].conflict = true;
                }
            }
        }

        void DrawInput(SettingsState &st, const Fonts &f)
        {
            SectionTitle("Input", f);
            SettingGroup("NAVIGATION", f, true);
            SettingRow("Orbit Sensitivity", "Mouse drag multiplier for orbiting the viewport camera.", f,
                       [&st, &f]() { SliderIntControl("##orbit", &st.input.orbitSensitivity, 10, 300, SliderValueFormat::Integer, f); });
            SettingRow("Pan Sensitivity", "Mouse drag multiplier for panning the viewport camera.", f,
                       [&st, &f]() { SliderIntControl("##pan", &st.input.panSensitivity, 10, 300, SliderValueFormat::Integer, f); });
            SettingRow("Invert Orbit Y", "Reverse the vertical orbit direction (push up to look down).", f,
                       [&st, &f]() { (void)ToggleControl("invert-y", &st.input.invertOrbitY, f); });

            SettingGroup("SHORTCUTS", f);

            // ── Shortcut table ────────────────────────────────────────
            // Draw as a bordered table with action label | key recorder
            const float tableWidth = ImGui::GetContentRegionAvail().x;
            ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2{12.0F, 9.0F});
            ImGui::PushStyleColor(ImGuiCol_TableBorderStrong, Theme::Border());
            if (ImGui::BeginTable("##shortcut-table", 2,
                                  ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg |
                                      ImGuiTableFlags_NoSavedSettings,
                                  {tableWidth, 0.0F}))
            {
                ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Shortcut",
                                        ImGuiTableColumnFlags_WidthFixed,
                                        Theme::Layout::ControlW + 24.0F);

                for (int i = 0; i < SettingsState::InputTab::kShortcutActionCount; ++i)
                {
                    ImGui::PushID(i);
                    ImGui::TableNextRow();

                    // Action label
                    ImGui::TableSetColumnIndex(0);
                    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 6.0F);
                    ImGui::PushStyleColor(ImGuiCol_Text, Theme::Text());
                    ImGui::TextUnformatted(SettingsState::InputTab::kShortcutActions[i]);
                    ImGui::PopStyleColor();

                    // Key recorder
                    ImGui::TableSetColumnIndex(1);
                    const bool isListening = (st.input.listeningShortcut == i);
                    bool localListening = isListening;

                    if (Ui::ShortcutRecorder("recorder",
                                             st.input.shortcuts[i].keys,
                                             &localListening,
                                             st.input.shortcuts[i].keys,
                                             sizeof(st.input.shortcuts[i].keys),
                                             f))
                    {
                        ResolveShortcutConflicts(st.input, i);
                        st.input.listeningShortcut = -1;
                        st.dirty = true;
                    }
                    else if (localListening && !isListening)
                    {
                        st.input.listeningShortcut = i;
                    }
                    else if (!localListening && isListening)
                    {
                        st.input.listeningShortcut = -1;
                    }

                    // Show conflict indicator
                    if (st.input.shortcuts[i].conflict)
                    {
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        ImGui::TableSetColumnIndex(1);
                        ImGui::PushStyleColor(ImGuiCol_Text, Theme::Err());
                        {
                            ScopedTextStyle ts(f.mono, 10.5F, Theme::FontPx::Mono);
                            ImGui::TextUnformatted("⚠ Conflicts with another shortcut");
                        }
                        ImGui::PopStyleColor();
                    }

                    ImGui::PopID();
                }
                ImGui::EndTable();
            }
            ImGui::PopStyleColor();
            ImGui::PopStyleVar();
        }

        void DrawRendering(SettingsState &st, const Fonts &f)
        {
            static constexpr std::array<const char *, 4> kViewport = {"Shaded", "Wireframe", "Lit", "Unlit"};
            static constexpr std::array<const char *, 4> kTier = {"High End", "DX12 / Vulkan", "DX11", "ES3"};
            SectionTitle("Rendering", f);
            SettingGroup("VIEWPORT", f, true);
            SettingRow("Default Viewport Mode", "Shading mode used for new viewport panels.", f, [&st, &f]() { (void)ComboControl("##viewport", &st.rendering.viewportMode, kViewport.data(), static_cast<int>(kViewport.size()), f); });
            SettingRow("Grid Overlay", "Show the reference grid in the editor viewport.", f, [&st, &f]() { (void)ToggleControl("grid", &st.rendering.gridOverlay, f); });
            SettingGroup("QUALITY", f);
            SettingRow("Editor Rendering Tier", "Maximum feature tier used in the editor viewport.", f,
                       [&st, &f]() { (void)ComboControl("##tier", &st.rendering.renderingTier, kTier.data(), static_cast<int>(kTier.size()), f); });
            SettingRow("Texture Streaming Budget", "Maximum texture memory pool used by the streaming system.", f,
                       [&st, &f]() { (void)InputTextControl("##texture-budget", st.rendering.textureBudget, 32, f); });
        }

        void DrawAudio(SettingsState &st, const Fonts &f)
        {
            static constexpr std::array<const char *, 3> kDevices = {"System Default", "Headphones", "Speakers"};
            SectionTitle("Audio", f);
            SettingGroup("OUTPUT", f, true);
            SettingRow("Master Volume", "Global audio output level for the editor (0–100).", f,
                       [&st, &f]() { SliderIntControl("##volume", &st.audio.masterVolume, 0, 100, SliderValueFormat::Integer, f); });
            SettingRow("Audio Output Device", "Preferred playback device for editor audio.", f,
                       [&st, &f]() { (void)ComboControl("##audio-device", &st.audio.audioOutputDevice, kDevices.data(), static_cast<int>(kDevices.size()), f); });
            SettingRow("Enable Audio In Editor", "Play in-editor sounds and preview audio assets.", f,
                       [&st, &f]() { (void)ToggleControl("audio-enabled", &st.audio.audioEnabled, f); });
        }

        void DrawNetwork(SettingsState &st, const Fonts &f)
        {
            SectionTitle("Network", f);
            SettingGroup("MULTIPLAYER PREVIEW", f, true);
            SettingRow("Max Preview Clients", "Maximum concurrent PIE (Play In Editor) client connections.", f,
                       [&st, &f]() { InputIntControl("##max-clients", &st.network.maxPreviewClients, f); });
            SettingRow("Simulate Latency", "Artificial one-way delay injected on loopback (ms).", f,
                       [&st, &f]() { SliderIntControl("##latency", &st.network.simulatedLatencyMs, 0, 500, SliderValueFormat::Milliseconds, f); });
            SettingRow("Package Download Threads", "Parallel download workers for template and asset packages.", f,
                       [&st, &f]() { InputIntControl("##download-threads", &st.network.packageDownloadThreads, f); });
        }

        void DrawDiagnostics(SettingsState &st, const Fonts &f)
        {
            static constexpr std::array<const char *, 4> kLogLevels = {"Debug", "Info", "Warning", "Error"};
            SectionTitle("Diagnostics", f);
            SettingGroup("LOGGING", f, true);
            SettingRow("Console Log Level", "Minimum severity shown in the editor console output.", f,
                       [&st, &f]() { (void)ComboControl("##log-level", &st.diagnostics.consoleLogLevel, kLogLevels.data(), static_cast<int>(kLogLevels.size()), f); });
            SettingRow("Write Log To File", "Persist the console log to a timestamped file on disk.", f,
                       [&st, &f]() { (void)ToggleControl("write-log", &st.diagnostics.writeLogToFile, f); });
            SettingGroup("PROFILER", f);
            SettingRow("Auto-capture On Stutter", "Automatically start a profiler capture when a frame spike is detected.", f,
                       [&st, &f]() { (void)ToggleControl("capture-stutter", &st.diagnostics.autoCaptureStutter, f); });
            SettingRow("Stutter Threshold (ms)", "Frame time above this value triggers an auto-capture.", f,
                       [&st, &f]() { InputFloatControl("##stutter", &st.diagnostics.stutterThresholdMs, f); });
        }

        void DrawPlugins(SettingsState &st, const Fonts &f)
        {
            SectionTitle("Plugins", f);

            // Header copy + toolbar: keep the descriptive text on the left and
            // reserve an explicit action area so buttons never collide with wrapping text.
            {
                const float availW = ImGui::GetContentRegionAvail().x;
                constexpr float btnW = 116.0F;
                constexpr float btnGap = 8.0F;
                constexpr float actionsW = btnW * 2.0F + btnGap;
                const float copyW = std::max(260.0F, availW - actionsW - 28.0F);
                const float startY = ImGui::GetCursorPosY();

                ImGui::BeginGroup();
                ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + copyW);
                ImGui::PushStyleColor(ImGuiCol_Text, Theme::Muted());
                {
                    ScopedTextStyle ts(f.sans, 12.5F, Theme::FontPx::Sans);
                    ImGui::TextWrapped(
                        "Installed plugins expose settings, permissions, diagnostics, and manifest "
                        "metadata. Project plugins override editor plugins only when trusted.");
                }
                ImGui::PopStyleColor();
                ImGui::PopTextWrapPos();
                ImGui::EndGroup();

                ImGui::SameLine(0.0F, 0.0F);
                ImGui::SetCursorPos({ImGui::GetCursorPosX() + 24.0F, startY - 2.0F});
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2{12.0F, 6.0F});
                if (ImGui::Button("Install...", {btnW, 32.0F}))
                {
                    std::snprintf(st.modalFeedback, sizeof(st.modalFeedback),
                                  "Plugin installation dialog not yet implemented.");
                }
                ImGui::SameLine(0.0F, btnGap);
                if (ImGui::Button("Reload", {btnW, 32.0F}))
                {
                    std::snprintf(st.modalFeedback, sizeof(st.modalFeedback),
                                  "Plugins reloaded successfully.");
                }
                ImGui::PopStyleVar();
            }

            ImGui::Dummy({0.0F, 14.0F});

            // Segment tabs. Widths are explicit so the second label does not get clipped.
            {
                static constexpr const char *kSectionTabs[] = {"Installed", "Runtime & Discovery"};
                constexpr float pad = 4.0F;
                constexpr float tabH = 31.0F;
                const float containerW = ImGui::GetContentRegionAvail().x;
                const float installedW = 128.0F;
                const float runtimeW = std::min(186.0F, std::max(156.0F, containerW - installedW - pad * 4.0F));
                const float containerH = tabH + pad * 2.0F;
                const ImVec2 p = ImGui::GetCursorScreenPos();
                auto *dl = ImGui::GetWindowDrawList();

                dl->AddRectFilled(p, {p.x + containerW, p.y + containerH}, Theme::U32(Theme::Bg0()), Layout::Radius);
                dl->AddRect(p, {p.x + containerW, p.y + containerH}, Theme::U32(Theme::Border()), Layout::Radius);
                ImGui::SetCursorScreenPos({p.x + pad, p.y + pad});

                for (int i = 0; i < 2; ++i)
                {
                    if (i > 0) ImGui::SameLine(0.0F, 4.0F);
                    const bool active = st.pluginSectionTab == i;
                    const float tabW = i == 0 ? installedW : runtimeW;
                    ImGui::PushID(i + 100);
                    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, Layout::Radius);
                    ImGui::PushStyleColor(ImGuiCol_Button,
                                          active ? ImVec4{Theme::Accent().x, Theme::Accent().y, Theme::Accent().z, 0.12F}
                                                 : ImVec4{0.0F, 0.0F, 0.0F, 0.0F});
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                                          active ? ImVec4{Theme::Accent().x, Theme::Accent().y, Theme::Accent().z, 0.18F}
                                                 : Theme::Hover());
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                                          ImVec4{Theme::Accent().x, Theme::Accent().y, Theme::Accent().z, 0.22F});
                    ImGui::PushStyleColor(ImGuiCol_Text, active ? Theme::Text() : Theme::Muted());
                    {
                        ScopedTextStyle ts(f.sans, 14.5F, Theme::FontPx::Sans);
                        if (ImGui::Button(kSectionTabs[i], {tabW, tabH}))
                            st.pluginSectionTab = i;
                    }
                    ImGui::PopStyleColor(4);
                    ImGui::PopStyleVar();
                    ImGui::PopID();

                    if (active)
                    {
                        const ImVec2 bMin = ImGui::GetItemRectMin();
                        const ImVec2 bMax = ImGui::GetItemRectMax();
                        dl->AddLine({bMin.x + 8.0F, bMax.y - 2.0F},
                                    {bMax.x - 8.0F, bMax.y - 2.0F},
                                    Theme::U32(Theme::Accent()), 2.0F);
                    }
                }

                ImGui::SetCursorScreenPos({p.x, p.y + containerH + 12.0F});
            }

            if (st.pluginSectionTab == 0)
                DrawInstalledPlugins(st, f);
            else
                DrawRuntimeDiscovery(st, f);
        }

        // ── Badge helper ──────────────────────────────────────────────
        /** @brief Draws a pill badge with coloured background and text. */
        void DrawBadge(const char *text, ImVec4 colour, const Fonts &f)
        {
            auto *dl = ImGui::GetWindowDrawList();
            const float padX = 7.0F;
            const float padY = 4.0F;
            const ImVec2 textSize = ImGui::CalcTextSize(text);
            const ImVec2 badgeMin = ImGui::GetCursorScreenPos();
            const ImVec2 badgeMax{badgeMin.x + textSize.x + padX * 2.0F,
                                  badgeMin.y + textSize.y + padY * 2.0F};

            dl->AddRectFilled(badgeMin, badgeMax,
                              ImColor{colour.x, colour.y, colour.z, 0.10F}, 999.0F);
            dl->AddRect(badgeMin, badgeMax,
                        ImColor{colour.x, colour.y, colour.z, 0.28F}, 999.0F);

            ImGui::SetCursorScreenPos({badgeMin.x + padX, badgeMin.y + padY});
            ImGui::PushStyleColor(ImGuiCol_Text, colour);
            {
                ScopedTextStyle ts(f.mono, 10.5F, Theme::FontPx::Mono);
                ImGui::TextUnformatted(text);
            }
            ImGui::PopStyleColor();
            ImGui::SetCursorScreenPos({badgeMax.x + 6.0F, badgeMin.y});
        }

        struct PermissionRowSpec
        {
            const char *icon;
            const char *title;
            const char *desc;
            const char *badgeText;
            ImVec4 badgeColour;
        };

        struct DiagnosticMetricSpec
        {
            const char *label;
            const char *value;
            const char *hint;
            ImVec4 valueColour;
        };

        [[nodiscard]] float BadgeWidth(const char *text)
        {
            return ImGui::CalcTextSize(text).x + 18.0F;
        }

        template <typename DrawControl>
        void PluginSettingRow(const char *label,
                              const char *description,
                              const Fonts &f,
                              DrawControl drawControl)
        {
            const float rowW = ImGui::GetContentRegionAvail().x;
            const float startY = ImGui::GetCursorScreenPos().y;

            {
                ScopedTextStyle ts(f.sans, 14.0F, Theme::FontPx::Sans);
                ImGui::PushStyleColor(ImGuiCol_Text, Theme::Text());
                ImGui::TextUnformatted(label);
                ImGui::PopStyleColor();
            }

            if (description != nullptr && description[0] != '\0')
            {
                ScopedTextStyle ts(f.sans, 12.0F, Theme::FontPx::Sans);
                ImGui::PushStyleColor(ImGuiCol_Text, Theme::Muted());
                ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + rowW);
                ImGui::TextWrapped("%s", description);
                ImGui::PopTextWrapPos();
                ImGui::PopStyleColor();
            }

            ImGui::Dummy({0.0F, 6.0F});
            drawControl();
            ImGui::Dummy({0.0F, 8.0F});

            const ImVec2 sep = ImGui::GetCursorScreenPos();
            ImGui::GetWindowDrawList()->AddLine({sep.x, sep.y}, {sep.x + rowW, sep.y},
                                                Theme::U32(Theme::Border()), 1.0F);
            ImGui::Dummy({0.0F, 10.0F});

            if (ImGui::GetCursorScreenPos().y - startY < 58.0F)
                ImGui::Dummy({0.0F, 58.0F - (ImGui::GetCursorScreenPos().y - startY)});
        }

        void DrawToggleState(const char *id, bool *value, const Fonts &f)
        {
            (void)ToggleControl(id, value, f, false);
            ImGui::SameLine(0.0F, 8.0F);
            ScopedTextStyle ts(f.sans, 12.5F, Theme::FontPx::Sans);
            ImGui::PushStyleColor(ImGuiCol_Text, *value ? Theme::Text() : Theme::Muted());
            ImGui::TextUnformatted(*value ? "Enabled" : "Disabled");
            ImGui::PopStyleColor();
        }

        void DrawPermissionRows(const PermissionRowSpec *rows, const int rowCount, const Fonts &f)
        {
            for (int i = 0; i < rowCount; ++i)
            {
                const auto &perm = rows[i];
                ImGui::PushID(perm.title);
                const float cardW = ImGui::GetContentRegionAvail().x;
                constexpr float cardH = 66.0F;
                const ImVec2 p = ImGui::GetCursorScreenPos();
                const float badgeW = BadgeWidth(perm.badgeText);
                auto *dl = ImGui::GetWindowDrawList();

                dl->AddRectFilled(p, {p.x + cardW, p.y + cardH}, Theme::U32(Theme::Bg3()), Layout::Radius);
                dl->AddRect(p, {p.x + cardW, p.y + cardH}, Theme::U32(Theme::Border()), Layout::Radius);

                ImGui::SetCursorScreenPos({p.x + 13.0F, p.y + 19.0F});
                {
                    ScopedTextStyle ts(f.monoSemiBold, 14.0F, Theme::FontPx::MonoSemiBold);
                    ImGui::PushStyleColor(ImGuiCol_Text, perm.badgeColour);
                    ImGui::TextUnformatted(perm.icon);
                    ImGui::PopStyleColor();
                }

                ImGui::SetCursorScreenPos({p.x + 40.0F, p.y + 11.0F});
                {
                    ScopedTextStyle ts(f.sans, 12.5F, Theme::FontPx::Sans);
                    ImGui::PushStyleColor(ImGuiCol_Text, Theme::Text());
                    ImGui::TextUnformatted(perm.title);
                    ImGui::PopStyleColor();
                }

                ImGui::SetCursorScreenPos({p.x + 40.0F, p.y + 32.0F});
                {
                    ScopedTextStyle ts(f.sans, 11.5F, Theme::FontPx::Sans);
                    ImGui::PushStyleColor(ImGuiCol_Text, Theme::Muted());
                    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + cardW - badgeW - 68.0F);
                    ImGui::TextWrapped("%s", perm.desc);
                    ImGui::PopTextWrapPos();
                    ImGui::PopStyleColor();
                }

                ImGui::SetCursorScreenPos({p.x + cardW - badgeW - 14.0F, p.y + 20.0F});
                DrawBadge(perm.badgeText, perm.badgeColour, f);

                ImGui::SetCursorScreenPos({p.x, p.y + cardH + 8.0F});
                ImGui::PopID();
            }
        }

        void DrawDiagnosticMetrics(const DiagnosticMetricSpec *metrics, const int metricCount, const Fonts &f)
        {
            constexpr float gap = 8.0F;
            constexpr float cardH = 68.0F;
            const float availW = ImGui::GetContentRegionAvail().x;
            const float cardW = (availW - gap * static_cast<float>(metricCount - 1)) /
                                static_cast<float>(metricCount);
            const ImVec2 start = ImGui::GetCursorScreenPos();
            auto *dl = ImGui::GetWindowDrawList();

            for (int i = 0; i < metricCount; ++i)
            {
                const ImVec2 p{start.x + static_cast<float>(i) * (cardW + gap), start.y};
                const auto &m = metrics[i];
                dl->AddRectFilled(p, {p.x + cardW, p.y + cardH}, Theme::U32(Theme::Bg3()), Layout::Radius);
                dl->AddRect(p, {p.x + cardW, p.y + cardH}, Theme::U32(Theme::Border()), Layout::Radius);

                ImGui::SetCursorScreenPos({p.x + 12.0F, p.y + 10.0F});
                {
                    ScopedTextStyle ts(f.monoSemiBold, 9.5F, Theme::FontPx::MonoSemiBold);
                    ImGui::PushStyleColor(ImGuiCol_Text, Theme::Dim());
                    ImGui::TextUnformatted(m.label);
                    ImGui::PopStyleColor();
                }

                ImGui::SetCursorScreenPos({p.x + 12.0F, p.y + 28.0F});
                {
                    ScopedTextStyle ts(f.monoSemiBold, 15.0F, Theme::FontPx::MonoSemiBold);
                    ImGui::PushStyleColor(ImGuiCol_Text, m.valueColour);
                    ImGui::TextUnformatted(m.value);
                    ImGui::PopStyleColor();
                }

                if (m.hint != nullptr && m.hint[0] != '\0')
                {
                    ImGui::SetCursorScreenPos({p.x + 12.0F, p.y + 49.0F});
                    ScopedTextStyle ts(f.sans, 9.8F, Theme::FontPx::Sans);
                    ImGui::PushStyleColor(ImGuiCol_Text, Theme::Dim());
                    ImGui::TextUnformatted(m.hint);
                    ImGui::PopStyleColor();
                }
            }

            ImGui::SetCursorScreenPos({start.x, start.y + cardH + 12.0F});
        }

        void DrawDiagnosticActivity(const char *const *items, const int itemCount, const Fonts &f)
        {
            SettingGroup("RECENT ACTIVITY", f);
            for (int i = 0; i < itemCount; ++i)
            {
                ImGui::PushID(i);
                const float rowW = ImGui::GetContentRegionAvail().x;
                constexpr float rowH = 30.0F;
                const ImVec2 p = ImGui::GetCursorScreenPos();
                auto *dl = ImGui::GetWindowDrawList();
                dl->AddRectFilled(p, {p.x + rowW, p.y + rowH}, Theme::U32(i % 2 == 0 ? Theme::Bg3() : Theme::Bg2()), Layout::Radius);

                ImGui::SetCursorScreenPos({p.x + 10.0F, p.y + 7.0F});
                ScopedTextStyle ts(f.mono, 10.5F, Theme::FontPx::Mono);
                ImGui::PushStyleColor(ImGuiCol_Text, Theme::Muted());
                ImGui::TextUnformatted(items[i]);
                ImGui::PopStyleColor();
                ImGui::SetCursorScreenPos({p.x, p.y + rowH + 4.0F});
                ImGui::PopID();
            }
        }

        void DrawManifestBlock(const char *path, const char *manifest, const Fonts &f)
        {
            const ImVec2 headerPos = ImGui::GetCursorScreenPos();
            const float headerW = ImGui::GetContentRegionAvail().x;

            FieldLabel("MANIFEST", f);
            if (path != nullptr && path[0] != '\0')
            {
                ImGui::SameLine(0.0F, 8.0F);
                ScopedTextStyle ts(f.mono, 10.0F, Theme::FontPx::Mono);
                ImGui::PushStyleColor(ImGuiCol_Text, Theme::Dim());
                ImGui::TextUnformatted(path);
                ImGui::PopStyleColor();
            }

            ImGui::SetCursorScreenPos({headerPos.x + headerW - 58.0F, headerPos.y - 2.0F});
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2{8.0F, 3.0F});
            if (ImGui::Button("Copy", {58.0F, 24.0F}))
                ImGui::SetClipboardText(manifest);
            ImGui::PopStyleVar();
            ImGui::SetCursorScreenPos({headerPos.x, headerPos.y + 30.0F});

            ImGui::PushStyleColor(ImGuiCol_ChildBg, Theme::Bg0());
            ImGui::BeginChild("manifest-code", {0.0F, 168.0F}, true,
                              ImGuiWindowFlags_AlwaysUseWindowPadding |
                                  ImGuiWindowFlags_NoScrollbar |
                                  ImGuiWindowFlags_NoScrollWithMouse);
            {
                ScopedTextStyle ts(f.mono, 11.0F, Theme::FontPx::Mono);
                ImGui::PushStyleColor(ImGuiCol_Text, Theme::Muted());
                ImGui::TextUnformatted(manifest);
                ImGui::PopStyleColor();
            }
            ImGui::EndChild();
            ImGui::PopStyleColor();
        }
        [[nodiscard]] bool ContainsCaseInsensitive(const char *text, const char *query)
        {
            if (query == nullptr || query[0] == '\0') return true;
            for (const char *start = text; *start != '\0'; ++start)
            {
                const char *candidate = start;
                const char *needle = query;
                while (*candidate != '\0' && *needle != '\0' &&
                       std::tolower(static_cast<unsigned char>(*candidate)) ==
                           std::tolower(static_cast<unsigned char>(*needle)))
                {
                    ++candidate;
                    ++needle;
                }
                if (*needle == '\0') return true;
            }
            return false;
        }

        // ── Plugin list (left column) ─────────────────────────────────
        void DrawPluginList(SettingsState &st, const Fonts &f, float /*listW*/)
        {
            SettingGroup("INSTALLED PLUGINS", f, true);

            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2{10.0F, 7.0F});
            ImGui::PushStyleColor(ImGuiCol_FrameBg, Theme::Bg3());
            ImGui::PushStyleColor(ImGuiCol_Text, Theme::Text());
            ImGui::InputTextWithHint("##filter", "Filter plugins...", st.pluginFilter, sizeof(st.pluginFilter));
            ImGui::PopStyleColor(2);
            ImGui::PopStyleVar();
            ImGui::Dummy({0.0F, 10.0F});

            static const struct
            {
                const char *name;
                const char *desc;
                const char *version;
                const char *statusLabel;
                ImVec4 statusColour;
                const char *category;
                int idx;
                bool *enabled;
            } kPlugins[] = {
                {"Horo MCP Bridge",
                 "Local automation bridge for scene, asset, and editor operations.",
                 "v0.4.0", "Trusted", Theme::Ok(), "Editor Tool", 0,
                 &st.plugins.horoMcpBridge},
                {"Vendor FMOD Integration",
                 "FMOD Studio authoring, live update, bank import, and runtime binding.",
                 "v2.02.20", "Verified Vendor", Theme::Ok(), "Audio", 1,
                 &st.plugins.fmodIntegration},
                {"Steamworks SDK",
                 "Steam achievements, overlay, networking sockets, and build metadata.",
                 "v1.59", "Disabled", Theme::Warn(), "Platform", 2,
                 &st.plugins.steamworksSdk},
            };

            for (const auto &p : kPlugins)
            {
                if (!ContainsCaseInsensitive(p.name, st.pluginFilter)) continue;

                const bool active = (st.selectedPlugin == p.idx);
                const bool enabled = *p.enabled;
                ImGui::PushID(p.idx);

                const ImVec2 cardPos = ImGui::GetCursorScreenPos();
                const float cardW = ImGui::GetContentRegionAvail().x;
                constexpr float cardH = 96.0F;
                constexpr float padLeft = 14.0F;
                const float innerX = cardPos.x + padLeft + 24.0F;
                auto *dl = ImGui::GetWindowDrawList();

                if (active)
                {
                    dl->AddRectFilled(cardPos, {cardPos.x + cardW, cardPos.y + cardH},
                                      ImColor{Theme::Accent().x, Theme::Accent().y, Theme::Accent().z, 0.09F},
                                      Layout::Radius);
                    dl->AddRect(cardPos, {cardPos.x + cardW, cardPos.y + cardH}, Theme::U32(Theme::BorderStrong()), Layout::Radius);
                    dl->AddRectFilled(cardPos, {cardPos.x + 3.0F, cardPos.y + cardH},
                                      Theme::U32(Theme::Accent()), 1.0F);
                }
                else if (ImGui::IsMouseHoveringRect(cardPos, {cardPos.x + cardW, cardPos.y + cardH}))
                {
                    dl->AddRectFilled(cardPos, {cardPos.x + cardW, cardPos.y + cardH},
                                      Theme::U32(Theme::Hover()), Layout::Radius);
                    dl->AddRect(cardPos, {cardPos.x + cardW, cardPos.y + cardH}, Theme::U32(Theme::Border()), Layout::Radius);
                }
                else
                {
                    dl->AddRect(cardPos, {cardPos.x + cardW, cardPos.y + cardH}, Theme::U32(Theme::Border()), Layout::Radius);
                }

                ImGui::InvisibleButton("##card", {cardW, cardH});
                if (ImGui::IsItemClicked())
                {
                    st.selectedPlugin = p.idx;
                    st.pluginDetailTab[p.idx] = 0;
                }

                const ImVec2 dotCenter{cardPos.x + padLeft + 6.0F, cardPos.y + 18.0F};
                dl->AddCircleFilled(dotCenter, 4.5F, enabled ? Theme::U32(Theme::Ok()) : Theme::U32(Theme::Dim()));
                if (enabled)
                    dl->AddCircleFilled(dotCenter, 7.0F,
                                        ImColor{Theme::Ok().x, Theme::Ok().y, Theme::Ok().z, 0.13F});

                ImGui::SetCursorScreenPos({innerX, cardPos.y + 9.0F});
                {
                    ScopedTextStyle ts(f.sans, 14.0F, Theme::FontPx::Sans);
                    ImGui::PushStyleColor(ImGuiCol_Text, Theme::Text());
                    ImGui::TextUnformatted(p.name);
                    ImGui::PopStyleColor();
                }

                ImGui::SetCursorScreenPos({cardPos.x + cardW - 48.0F, cardPos.y + 9.0F});
                (void)ToggleControl("##tog", p.enabled, f, false);

                ImGui::SetCursorScreenPos({innerX, cardPos.y + 33.0F});
                {
                    ScopedTextStyle ts(f.sans, 11.8F, Theme::FontPx::Sans);
                    ImGui::PushStyleColor(ImGuiCol_Text, Theme::Muted());
                    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + cardW - (innerX - cardPos.x) - 56.0F);
                    ImGui::TextWrapped("%s", p.desc);
                    ImGui::PopTextWrapPos();
                    ImGui::PopStyleColor();
                }

                ImGui::SetCursorScreenPos({innerX, cardPos.y + cardH - 26.0F});
                DrawBadge(p.version, Theme::Accent(), f);
                DrawBadge(p.statusLabel, p.statusColour, f);
                {
                    ScopedTextStyle ts(f.mono, 10.5F, Theme::FontPx::Mono);
                    ImGui::PushStyleColor(ImGuiCol_Text, Theme::Dim());
                    ImGui::TextUnformatted(p.category);
                    ImGui::PopStyleColor();
                }

                ImGui::SetCursorScreenPos({cardPos.x, cardPos.y + cardH + 8.0F});
                ImGui::PopID();
            }
        }

        // ── Plugin detail panel (right column) ────────────────────────
        void DrawPluginDetailPanel(SettingsState &st, const Fonts &f, float w, bool embedded)
        {
            if (st.selectedPlugin < 0 || st.selectedPlugin > 2)
            {
                if (!embedded)
                {
                    ImGui::PushStyleColor(ImGuiCol_ChildBg, Theme::Bg2());
                    ImGui::BeginChild("PluginDetail", {w, 0.0F}, true,
                                      ImGuiWindowFlags_AlwaysUseWindowPadding |
                                          ImGuiWindowFlags_NoScrollbar);
                }
                ImGui::PushStyleColor(ImGuiCol_Text, Theme::Dim());
                ImGui::TextUnformatted("Select a plugin from the list.");
                ImGui::PopStyleColor();
                if (!embedded)
                {
                    ImGui::EndChild();
                    ImGui::PopStyleColor();
                }
                return;
            }

            static const struct
            {
                const char *name;
                const char *desc;
                const char *scopeBadge;
                const char *signedBadge;
                ImVec4 signedColour;
                const char *restartBadge;
                const char *action1;
                const char *action2;
            } kDetailHeaders[] = {
                {"Horo MCP Bridge",
                 "Exposes a local MCP endpoint so external tools can inspect project state, create assets, run editor commands, and query diagnostics.",
                 "Editor-wide", "Signed", Theme::Ok(), "Restart not required",
                 "Open Logs", "Disable"},
                {"Vendor FMOD Integration",
                 "Connects Horo audio assets with FMOD Studio banks, event references, live update, and build validation.",
                 "Project-scoped", "Verified Vendor", Theme::Ok(), "Needs SDK path",
                 "Validate", "Disable"},
                {"Steamworks SDK",
                 "Adds Steam platform services such as overlay, achievements, networking sockets, and App ID metadata.",
                 "Platform", "Disabled", Theme::Warn(), "Restart on enable",
                 "Enable", "Open Docs"},
            };

            const auto &hdr = kDetailHeaders[st.selectedPlugin];
            int &activeTab = st.pluginDetailTab[st.selectedPlugin];
            if (activeTab < 0 || activeTab > 3) activeTab = 0;
            static int s_lastSelectedPlugin = -1;
            static int s_lastPluginTab = -1;

            if (!embedded)
            {
                ImGui::PushStyleColor(ImGuiCol_ChildBg, Theme::Bg2());
                ImGui::BeginChild("PluginDetail", {w, 0.0F}, true,
                                  ImGuiWindowFlags_AlwaysUseWindowPadding |
                                      ImGuiWindowFlags_NoScrollbar |
                                      ImGuiWindowFlags_NoScrollWithMouse);
            }

            {
                const float headerW = ImGui::GetContentRegionAvail().x;
                constexpr float headerH = 126.0F;
                const ImVec2 p = ImGui::GetCursorScreenPos();
                auto *dl = ImGui::GetWindowDrawList();
                dl->AddRectFilled(p, {p.x + headerW, p.y + headerH}, Theme::U32(Theme::Bg3()), Layout::Radius);
                dl->AddRect(p, {p.x + headerW, p.y + headerH}, Theme::U32(Theme::Border()), Layout::Radius);

                const bool selectedEnabled = !(st.selectedPlugin == 2 && !st.plugins.steamworksSdk);
                const ImVec2 dotCenter{p.x + 18.0F, p.y + 20.0F};
                dl->AddCircleFilled(dotCenter, 4.5F, selectedEnabled ? Theme::U32(Theme::Ok()) : Theme::U32(Theme::Dim()));
                if (selectedEnabled)
                    dl->AddCircleFilled(dotCenter, 7.0F,
                                        ImColor{Theme::Ok().x, Theme::Ok().y, Theme::Ok().z, 0.13F});

                ImGui::SetCursorScreenPos({p.x + 34.0F, p.y + 12.0F});
                {
                    ScopedTextStyle ts(f.mono, 14.5F, Theme::FontPx::Mono);
                    ImGui::PushStyleColor(ImGuiCol_Text, Theme::Text());
                    ImGui::TextUnformatted(hdr.name);
                    ImGui::PopStyleColor();
                }

                ImGui::SetCursorScreenPos({p.x + 20.0F, p.y + 40.0F});
                {
                    ScopedTextStyle ts(f.sans, 12.0F, Theme::FontPx::Sans);
                    ImGui::PushStyleColor(ImGuiCol_Text, Theme::Muted());
                    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + headerW - 44.0F);
                    ImGui::TextWrapped("%s", hdr.desc);
                    ImGui::PopTextWrapPos();
                    ImGui::PopStyleColor();
                }

                ImGui::SetCursorScreenPos({p.x + 20.0F, p.y + 94.0F});
                DrawBadge(hdr.scopeBadge, Theme::Accent(), f);
                DrawBadge(hdr.signedBadge, hdr.signedColour, f);
                {
                    ScopedTextStyle ts(f.mono, 10.5F, Theme::FontPx::Mono);
                    ImGui::PushStyleColor(ImGuiCol_Text, Theme::Dim());
                    ImGui::TextUnformatted(hdr.restartBadge);
                    ImGui::PopStyleColor();
                }

                if (headerW >= 520.0F)
                {
                    constexpr float actionW = 88.0F;
                    constexpr float actionGap = 6.0F;
                    ImGui::SetCursorScreenPos({p.x + headerW - actionW * 2.0F - actionGap - 14.0F, p.y + headerH - 36.0F});
                    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2{8.0F, 4.0F});
                    ImGui::PushStyleColor(ImGuiCol_Button, Theme::Bg1());
                    if (ImGui::Button(hdr.action1, {actionW, 28.0F}))
                    {
                        if (std::strcmp(hdr.action1, "Open Logs") == 0)
                            std::snprintf(st.modalFeedback, sizeof(st.modalFeedback),
                                          "Opening Horo MCP Bridge logs...");
                        else if (std::strcmp(hdr.action1, "Validate") == 0)
                            std::snprintf(st.modalFeedback, sizeof(st.modalFeedback),
                                          "FMOD integration validated successfully.");
                        else if (std::strcmp(hdr.action1, "Enable") == 0)
                            st.plugins.steamworksSdk = true;
                    }
                    ImGui::PopStyleColor();
                    ImGui::SameLine(0.0F, actionGap);
                    const bool danger = std::strcmp(hdr.action2, "Disable") == 0;
                    if (danger) ImGui::PushStyleColor(ImGuiCol_Text, Theme::Err());
                    if (ImGui::Button(hdr.action2, {actionW, 28.0F}))
                    {
                        if (danger && st.selectedPlugin == 0)
                            st.plugins.horoMcpBridge = false;
                        else if (danger && st.selectedPlugin == 1)
                            st.plugins.fmodIntegration = false;
                        else if (std::strcmp(hdr.action2, "Open Docs") == 0)
                            std::snprintf(st.modalFeedback, sizeof(st.modalFeedback),
                                          "Opening Steamworks SDK documentation...");
                    }
                    if (danger) ImGui::PopStyleColor();
                    ImGui::PopStyleVar();
                }

                ImGui::SetCursorScreenPos({p.x, p.y + headerH + 12.0F});
            }

            {
                static constexpr const char *kDetailTabs[] = {"Settings", "Permissions", "Diagnostics", "Manifest"};
                const float tabAvail = ImGui::GetContentRegionAvail().x;
                const float tabGap = 4.0F;
                const float tabW = (tabAvail - tabGap * 3.0F) / 4.0F;
                constexpr float tabH = 34.0F;
                auto *dl = ImGui::GetWindowDrawList();

                ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{0.0F, 0.0F});
                for (int i = 0; i < 4; ++i)
                {
                    if (i > 0) ImGui::SameLine(0.0F, tabGap);
                    const bool active = activeTab == i;
                    ImGui::PushID(i + 200);
                    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, Layout::Radius);
                    ImGui::PushStyleColor(ImGuiCol_Button,
                                          active ? ImVec4{Theme::Accent().x, Theme::Accent().y, Theme::Accent().z, 0.09F}
                                                 : Theme::Bg2());
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Theme::Hover());
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                                          ImVec4{Theme::Accent().x, Theme::Accent().y, Theme::Accent().z, 0.16F});
                    ImGui::PushStyleColor(ImGuiCol_Text, active ? Theme::Accent() : Theme::Muted());
                    {
                        ScopedTextStyle ts(f.sans, 13.5F, Theme::FontPx::Sans);
                        if (ImGui::Button(kDetailTabs[i], {tabW, tabH}))
                            activeTab = i;
                    }
                    ImGui::PopStyleColor(4);
                    ImGui::PopStyleVar();
                    ImGui::PopID();

                    if (active)
                    {
                        const ImVec2 bMin = ImGui::GetItemRectMin();
                        const ImVec2 bMax = ImGui::GetItemRectMax();
                        dl->AddLine({bMin.x + 8.0F, bMax.y - 2.0F},
                                    {bMax.x - 8.0F, bMax.y - 2.0F},
                                    Theme::U32(Theme::Accent()), 2.0F);
                    }
                }
                ImGui::PopStyleVar();
            }

            ImGui::Dummy({0.0F, 10.0F});

            const bool contentChanged = s_lastSelectedPlugin != st.selectedPlugin || s_lastPluginTab != activeTab;
            const float contentH = std::max(120.0F, ImGui::GetContentRegionAvail().y);
            ImGui::PushStyleColor(ImGuiCol_ChildBg, Theme::Bg2());
            ImGui::BeginChild("PluginDetailContent", {0.0F, contentH}, false,
                              ImGuiWindowFlags_AlwaysUseWindowPadding);
            if (contentChanged)
                ImGui::SetScrollY(0.0F);

            switch (st.selectedPlugin)
            {
            case 0:
                DrawMcpDetailContent(st, f, activeTab);
                break;
            case 1:
                DrawFmodDetailContent(st, f, activeTab);
                break;
            case 2:
                DrawSteamDetailContent(st, f, activeTab);
                break;
            }

            ImGui::EndChild();
            ImGui::PopStyleColor();
            s_lastSelectedPlugin = st.selectedPlugin;
            s_lastPluginTab = activeTab;

            if (!embedded)
            {
                ImGui::EndChild();
                ImGui::PopStyleColor();
            }
        }

        // ── MCP Bridge detail content ─────────────────────────────────
        void DrawMcpDetailContent(SettingsState &st, const Fonts &f, int activeTab)
        {
            switch (activeTab)
            {
            case 0:
                SettingGroup("CONNECTION", f, true);
                PluginSettingRow("Transport Mode",
                                 "Use stdio for local tools; HTTP is useful for explicit local integrations.",
                                 f, [&st, &f]() {
                                     static constexpr const char *kModes[] = {"Local HTTP", "stdio", "Named Pipe"};
                                     (void)ComboControl("##transport", &st.mcp.transportMode, kModes, 3, f);
                                 });
                PluginSettingRow("MCP Port", "Bound to localhost unless remote access is enabled.", f,
                                 [&st, &f]() { InputIntControl("##mcp-port", &st.mcp.port, f); });
                PluginSettingRow("Require Session Token",
                                 "Reject tool calls unless they include the generated editor session token.",
                                 f, [&st, &f]() { DrawToggleState("##token", &st.mcp.requireToken, f); });
                PluginSettingRow("Allow Remote Connections", "Off by default to avoid accidental LAN exposure.", f,
                                 [&st, &f]() { DrawToggleState("##remote", &st.mcp.allowRemote, f); });
                SettingGroup("TOOL SCOPE", f);
                PluginSettingRow("Allowed Tool Groups", "Restrict what external tools can invoke.", f,
                                 [&st, &f]() {
                                     static constexpr const char *kScopes[] = {
                                         "Read + Safe Mutations", "Read Only", "Full Project Access", "Custom Policy..."};
                                     (void)ComboControl("##scope", &st.mcp.toolScope, kScopes, 4, f);
                                 });
                PluginSettingRow("Asset Write Root", "All generated assets must stay under this folder.", f,
                                 [&st, &f]() { (void)InputTextControl("##root", st.mcp.assetRoot, sizeof(st.mcp.assetRoot), f); });
                break;

            case 1:
                {
                    static const PermissionRowSpec kPerms[] = {
                        {"✓", "Read project metadata",
                         "Read project name, scene list, package graph, and editor state.",
                         "Allowed", Theme::Ok()},
                        {"✓", "Write generated assets",
                         "Create files only under Assets/Generated unless policy is elevated.",
                         "Scoped", Theme::Ok()},
                        {"!", "Execute build commands",
                         "Requires interactive confirmation before running build or release tasks.",
                         "Confirm", Theme::Warn()},
                    };
                    DrawPermissionRows(kPerms, static_cast<int>(std::size(kPerms)), f);
                }
                break;

            case 2:
                {
                    static const DiagnosticMetricSpec kMetrics[] = {
                        {"STATUS", "Running", "sandboxed", Theme::Ok()},
                        {"LAST CALL", "2m ago", "tool request", Theme::Text()},
                        {"ERRORS", "0", "last 24h", Theme::Ok()},
                    };
                    DrawDiagnosticMetrics(kMetrics, static_cast<int>(std::size(kMetrics)), f);
                    const char *kActivity[] = {
                        "14:22  project.read completed in 18ms",
                        "14:20  assets.write.scoped created /Assets/Generated/mesh.json",
                        "14:16  command.run requested confirmation"};
                    DrawDiagnosticActivity(kActivity, static_cast<int>(std::size(kActivity)), f);
                }
                break;

            case 3:
                DrawManifestBlock("plugins/mcp-bridge/plugin.yaml",
                                  "id: horo.mcp.bridge\n"
                                  "version: 0.4.0\n"
                                  "entry: plugins/mcp-bridge/bin/horo-mcp\n"
                                  "scope: editor\n"
                                  "permissions:\n"
                                  "  - project.read\n"
                                  "  - assets.write.scoped\n"
                                  "  - commands.run.confirmed",
                                  f);
                break;
            }
        }

        // ── FMOD detail content ───────────────────────────────────────
        void DrawFmodDetailContent(SettingsState &st, const Fonts &f, int activeTab)
        {
            switch (activeTab)
            {
            case 0:
                SettingGroup("AUTHORING", f, true);
                PluginSettingRow("FMOD Studio Path", "Used to open projects and compile banks from the editor.", f,
                                 [&st, &f]() { (void)InputTextControl("##fmod-path", st.fmod.studioPath, sizeof(st.fmod.studioPath), f); });
                PluginSettingRow("FMOD Project File", "Relative to project root.", f,
                                 [&st, &f]() { (void)InputTextControl("##fmod-proj", st.fmod.projectFile, sizeof(st.fmod.projectFile), f); });
                PluginSettingRow("Bank Output Path", "Compiled banks copied into the runtime asset tree.", f,
                                 [&st, &f]() { (void)InputTextControl("##fmod-bank", st.fmod.bankPath, sizeof(st.fmod.bankPath), f); });
                SettingGroup("RUNTIME & BUILD", f);
                PluginSettingRow("Live Update", "Reload event metadata and banks without restarting the editor.", f,
                                 [&st, &f]() { DrawToggleState("##fmod-live", &st.fmod.liveUpdate, f); });
                PluginSettingRow("Fail Build On Missing Banks", "Prevents shipping builds with unresolved audio events.", f,
                                 [&st, &f]() { DrawToggleState("##fmod-fail", &st.fmod.failOnMissing, f); });
                PluginSettingRow("Target Platform", "Bank platform used for editor preview.", f,
                                 [&st, &f]() {
                                     static constexpr const char *kPlatforms[] = {"Desktop", "Windows", "macOS", "Linux", "Console"};
                                     (void)ComboControl("##fmod-plat", &st.fmod.targetPlatform, kPlatforms, 5, f);
                                 });
                break;

            case 1:
                {
                    static const PermissionRowSpec kPerms[] = {
                        {"✓", "Read and write audio banks",
                         "Limited to configured FMOD project and bank output paths.",
                         "Scoped", Theme::Ok()},
                        {"!", "Launch external FMOD Studio process",
                         "Requires a configured executable path and user initiated action.",
                         "User action", Theme::Warn()},
                    };
                    DrawPermissionRows(kPerms, static_cast<int>(std::size(kPerms)), f);
                }
                break;

            case 2:
                {
                    static const DiagnosticMetricSpec kMetrics[] = {
                        {"BANKS", "14", "loaded", Theme::Text()},
                        {"UNRESOLVED", "2", "events", Theme::Warn()},
                        {"LIVE UPDATE", "On", "connected", Theme::Ok()},
                    };
                    DrawDiagnosticMetrics(kMetrics, static_cast<int>(std::size(kMetrics)), f);
                    const char *kActivity[] = {
                        "13:58  bank import finished with 2 unresolved event refs",
                        "13:44  live update connection established",
                        "13:31  Desktop bank validation completed"};
                    DrawDiagnosticActivity(kActivity, static_cast<int>(std::size(kActivity)), f);
                }
                break;

            case 3:
                DrawManifestBlock("plugins/fmod/plugin.yaml",
                                  "id: vendor.fmod\n"
                                  "version: 2.02.20\n"
                                  "entry: plugins/fmod/horo-fmod.plugin\n"
                                  "scope: project\n"
                                  "permissions:\n"
                                  "  - audio.bank.readwrite\n"
                                  "  - process.launch.user_action\n"
                                  "  - build.validation",
                                  f);
                break;
            }
        }

        // ── Steamworks detail content ─────────────────────────────────
        void DrawSteamDetailContent(SettingsState &st, const Fonts &f, int activeTab)
        {
            switch (activeTab)
            {
            case 0:
                SettingGroup("STEAM APP", f, true);
                PluginSettingRow("App ID", "Use 480 for local Spacewar-style testing only.", f,
                                 [&st, &f]() {
                                     static int steamAppId = 480;
                                     InputIntControl("##steam-appid", &steamAppId, f);
                                 });
                PluginSettingRow("SDK Path", "Path to the local Steamworks SDK root.", f,
                                 [&st, &f]() { (void)InputTextControl("##steam-sdk", st.steam.sdkPath, sizeof(st.steam.sdkPath), f); });
                PluginSettingRow("Initialize On", "Controls when Steam API is started during editor workflows.", f,
                                 [&st, &f]() {
                                     static constexpr const char *kModes[] = {"Play Mode Only", "Editor Launch", "Build Runtime Only"};
                                     (void)ComboControl("##steam-init", &st.steam.initMode, kModes, 3, f);
                                 });
                SettingGroup("FEATURES", f);
                PluginSettingRow("Overlay", "Enable Steam overlay while testing from Play Mode.", f,
                                 [&st, &f]() { DrawToggleState("##steam-overlay", &st.steam.overlay, f); });
                PluginSettingRow("Achievements", "Expose achievement authoring and validation panels.", f,
                                 [&st, &f]() { DrawToggleState("##steam-ach", &st.steam.achievements, f); });
                PluginSettingRow("Networking Sockets", "Enable Steam networking transport for multiplayer preview.", f,
                                 [&st, &f]() { DrawToggleState("##steam-net", &st.steam.networking, f); });
                break;

            case 1:
                {
                    static const PermissionRowSpec kPerms[] = {
                        {"✓", "Read platform config",
                         "Reads App ID, achievements config, and build target metadata.",
                         "Allowed", Theme::Ok()},
                        {"!", "Network access",
                         "Only enabled when Steam networking transport is selected.",
                         "Conditional", Theme::Warn()},
                    };
                    DrawPermissionRows(kPerms, static_cast<int>(std::size(kPerms)), f);
                }
                break;

            case 2:
                {
                    static const DiagnosticMetricSpec kMetrics[] = {
                        {"STATUS", "Disabled", "not loaded", Theme::Dim()},
                        {"SDK", "Missing", "path required", Theme::Warn()},
                        {"OVERLAY", "Ready", "waiting", Theme::Ok()},
                    };
                    DrawDiagnosticMetrics(kMetrics, static_cast<int>(std::size(kMetrics)), f);
                    const char *kActivity[] = {
                        "12:45  skipped init because Steamworks SDK is disabled",
                        "12:44  overlay check passed",
                        "12:42  missing SDK path warning emitted"};
                    DrawDiagnosticActivity(kActivity, static_cast<int>(std::size(kActivity)), f);
                }
                break;

            case 3:
                DrawManifestBlock("plugins/steamworks/plugin.yaml",
                                  "id: vendor.steamworks\n"
                                  "version: 1.59\n"
                                  "entry: plugins/steamworks/horo-steam.plugin\n"
                                  "scope: project\n"
                                  "permissions:\n"
                                  "  - platform.config.read\n"
                                  "  - network.conditional\n"
                                  "  - achievements.write",
                                  f);
                break;
            }
        }

        // ── Installed Plugins (split pane) ────────────────────────────
        void DrawInstalledPlugins(SettingsState &st, const Fonts &f)
        {
            const float availH = ImGui::GetContentRegionAvail().y;

            ImGui::PushStyleColor(ImGuiCol_ChildBg, Theme::Bg2());
            ImGui::BeginChild("PluginsWorkspace", {0.0F, availH}, true,
                              ImGuiWindowFlags_AlwaysUseWindowPadding);

            {
                ScopedTextStyle ts(f.sans, 12.5F, Theme::FontPx::Sans);
                ImGui::PushStyleColor(ImGuiCol_Text, Theme::Muted());
                ImGui::TextWrapped("Select a plugin to edit its settings, permissions, diagnostics, and manifest details. The selected plugin appears below in the same workspace so the layout stays simpler and easier to scan.");
                ImGui::PopStyleColor();
            }
            ImGui::Dummy({0.0F, 10.0F});

            DrawPluginList(st, f, ImGui::GetContentRegionAvail().x);

            ImGui::Dummy({0.0F, 10.0F});
            const ImVec2 sep = ImGui::GetCursorScreenPos();
            ImGui::GetWindowDrawList()->AddLine({sep.x, sep.y}, {sep.x + ImGui::GetContentRegionAvail().x, sep.y},
                                                Theme::U32(Theme::Border()), 1.0F);
            ImGui::Dummy({0.0F, 14.0F});

            DrawPluginDetailPanel(st, f, ImGui::GetContentRegionAvail().x, true);

            ImGui::EndChild();
            ImGui::PopStyleColor();
        }

        // ── Runtime & Discovery ───────────────────────────────────────
        void DrawRuntimeDiscovery(SettingsState &st, const Fonts &f)
        {
            SettingGroup("RUNTIME OVERVIEW", f, true);

            static const DiagnosticMetricSpec kRuntimeCards[] = {
                {"ISOLATION", "Sandboxed", "processes", Theme::Text()},
                {"DISCOVERY", "Project + editor", "paths", Theme::Text()},
                {"UPDATES", "Signed only", "registries", Theme::Ok()},
            };
            DrawDiagnosticMetrics(kRuntimeCards, static_cast<int>(std::size(kRuntimeCards)), f);

            SettingGroup("DISCOVERY", f);
            PluginSettingRow("Plugin Discovery Paths",
                             "Semicolon-separated paths. Project plugins override editor plugins only when trusted.",
                             f, [&st, &f]() {
                                 (void)InputTextControl("##disc-path", st.runtime.discoveryPaths,
                                                  sizeof(st.runtime.discoveryPaths), f);
                             });
            PluginSettingRow("Load Order Policy",
                             "Defines how editor, project, vendor, and local-development plugins are resolved.",
                             f, [&st, &f]() {
                                 static constexpr const char *kOrders[] = {
                                     "Project overrides editor if trusted", "Editor plugins first",
                                     "Project plugins first", "Locked by project manifest"};
                                 (void)ComboControl("##order", &st.runtime.loadOrder, kOrders, 4, f);
                             });
            PluginSettingRow("Development Plugin Path",
                             "Optional local path used for plugin authorship and hot-reload testing.",
                             f, [&st, &f]() {
                                 (void)InputTextControl("##dev-path", st.runtime.devPath,
                                                  sizeof(st.runtime.devPath), f);
                             });

            SettingGroup("SECURITY & ISOLATION", f);
            PluginSettingRow("Sandbox Plugin Processes",
                             "Run native/plugin processes with limited filesystem and network permissions.",
                             f, [&st, &f]() { DrawToggleState("##sandbox", &st.runtime.sandbox, f); });
            PluginSettingRow("Unsigned Plugin Policy",
                             "Controls what happens when a plugin is not signed by a trusted vendor or local workspace.",
                             f, [&st, &f]() {
                                 static constexpr const char *kPolicies[] = {"Block by default",
                                                                             "Allow after warning",
                                                                             "Allow local development only"};
                                 (void)ComboControl("##unsigned", &st.runtime.unsignedPolicy, kPolicies, 3, f);
                             });
            PluginSettingRow("Network Access Policy",
                             "Default network behavior for plugins unless a plugin-specific permission overrides it.",
                             f, [&st, &f]() {
                                 static constexpr const char *kNets[] = {"Deny by default", "Localhost only",
                                                                         "Prompt per plugin",
                                                                         "Allow trusted plugins"};
                                 (void)ComboControl("##net", &st.runtime.networkPolicy, kNets, 4, f);
                             });

            SettingGroup("UPDATES & COMPATIBILITY", f);
            PluginSettingRow("Auto-check Plugin Updates",
                             "Checks signed registries only; local plugins are never updated automatically.",
                             f, [&st, &f]() {
                                 static constexpr const char *kChecks[] = {"Weekly", "Daily", "Manual Only"};
                                 (void)ComboControl("##update", &st.runtime.updateCheck, kChecks, 3, f);
                             });
            PluginSettingRow("Compatibility Mode",
                             "How strictly plugin API versions are validated when opening a project.",
                             f, [&st, &f]() {
                                 static constexpr const char *kModes[] = {"Strict semantic versioning",
                                                                          "Allow compatible minors",
                                                                          "Prompt on mismatch"};
                                 (void)ComboControl("##compat", &st.runtime.compatMode, kModes, 3, f);
                             });

            ImGui::Dummy({0.0F, 4.0F});
            {
                const float noteW = ImGui::GetContentRegionAvail().x;
                const ImVec2 p = ImGui::GetCursorScreenPos();
                constexpr float noteH = 54.0F;
                auto *dl = ImGui::GetWindowDrawList();
                dl->AddRectFilled(p, {p.x + noteW, p.y + noteH},
                                  ImColor{Theme::Accent().x, Theme::Accent().y, Theme::Accent().z, 0.06F},
                                  Layout::Radius);
                dl->AddRect(p, {p.x + noteW, p.y + noteH}, Theme::U32(Theme::Border()), Layout::Radius);

                ImGui::SetCursorScreenPos({p.x + 12.0F, p.y + 10.0F});
                ScopedTextStyle ts(f.sans, 11.5F, Theme::FontPx::Sans);
                ImGui::PushStyleColor(ImGuiCol_Text, Theme::Muted());
                ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + noteW - 26.0F);
                ImGui::TextWrapped(
                    "Runtime settings are editor-wide defaults. Individual plugin settings live inside each plugin detail panel and can override these defaults only when the permission model allows it.");
                ImGui::PopTextWrapPos();
                ImGui::PopStyleColor();
                ImGui::SetCursorScreenPos({p.x, p.y + noteH + 4.0F});
            }
        }

        void DrawContent(SettingsState &st, const Fonts &f, const float bodyH)
        {
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{26.0F, 22.0F});
            ImGui::PushStyleColor(ImGuiCol_ChildBg, Theme::Bg1());
            ImGui::BeginChild("SettingsContent", {0.0F, bodyH}, false,
                              ImGuiWindowFlags_AlwaysUseWindowPadding);

            switch (static_cast<SettingsTab>(st.activeTab))
            {
                using enum SettingsTab;
            case General:
                DrawGeneral(st, f);
                break;
            case Appearance:
                DrawAppearance(st, f);
                break;
            case Input:
                DrawInput(st, f);
                break;
            case Rendering:
                DrawRendering(st, f);
                break;
            case Audio:
                DrawAudio(st, f);
                break;
            case Network:
                DrawNetwork(st, f);
                break;
            case Diagnostics:
                DrawDiagnostics(st, f);
                break;
            case Plugins:
                DrawPlugins(st, f);
                break;
            }

            ImGui::EndChild();
            ImGui::PopStyleColor();
            ImGui::PopStyleVar();
        }

        [[nodiscard]] bool DrawFooter(SettingsState &st, EditorSettingsService &settings, const Fonts &f)
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

            // ── Feedback line (one-shot modal notifications) ──────────
            if (st.modalFeedback[0] != '\0')
            {
                ImGui::SetCursorPosX(22.0F);
                ImGui::PushStyleColor(ImGuiCol_Text, Theme::Accent());
                {
                    ScopedTextStyle ts(f.mono, 11.0F, Theme::FontPx::Mono);
                    ImGui::TextUnformatted(st.modalFeedback);
                }
                ImGui::PopStyleColor();
            }

            constexpr float restoreW = 132.0F;
            constexpr float cancelW = 82.0F;
            constexpr float applyW = 74.0F;
            constexpr float gap = 8.0F;
            const float actionsW = restoreW + cancelW + applyW + gap * 2.0F;
            ImGui::SetCursorPos({ImGui::GetWindowWidth() - 22.0F - actionsW, 15.0F});
            bool requestClose = false;
            if (Button({.label = "Restore Defaults", .size = {restoreW, 34.0F}, .variant = Ui::ButtonVariant::Secondary, .fontSize = 13.0F, .font = f.mono, .baseFontSize = Theme::FontPx::Mono}))
            {
                ApplySettingsToDraft(st, DefaultEditorSettings());
                st.statusMessage = "Defaults loaded into draft. Apply to persist.";
                st.statusIsError = false;
            }
            ImGui::SameLine(0.0F, gap);
            if (Button({.label = "Cancel", .size = {cancelW, 34.0F}, .variant = Ui::ButtonVariant::Secondary, .fontSize = 13.0F, .font = f.mono, .baseFontSize = Theme::FontPx::Mono}))
            {
                requestClose = true;
            }
            ImGui::SameLine(0.0F, gap);
            if (Button({.label = "Apply", .size = {applyW, 34.0F}, .variant = Ui::ButtonVariant::Primary, .fontSize = 13.0F, .font = f.mono, .baseFontSize = Theme::FontPx::Mono}))
            {
                (void)ApplySettings(st, settings);
            }

            ImGui::EndChild();
            ImGui::PopStyleColor();
            return requestClose;
        }

        [[nodiscard]] ModalFrameResult DrawSettingsModalPresentationImpl(SettingsState &st, EditorSettingsService &settings, const Fonts &f, const ::ImTextureID logo)
        {
            if (st.appearance.pendingThemeIndex >= 0)
            {
                Theme::SelectThemeByIndex(st.appearance.pendingThemeIndex);
                st.appearance.pendingThemeIndex = -1;
            }
            
            st.modalFeedback[0] = '\0';

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

            ImGui::Begin("Settings", nullptr,
                         ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoTitleBar |
                         ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                         ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
            const bool headerRequestedClose = DrawHeader(st, f, logo);
            const float bodyH = ImGui::GetWindowHeight() - Layout::HeaderH - Layout::FooterH;
            DrawNavigation(st, f, bodyH);
            ImGui::SameLine(0.0F, 0.0F);
            DrawContent(st, f, bodyH);
            st.dirty = CollectDraftSettings(st) != st.committed;
            const bool footerRequestedClose = DrawFooter(st, settings, f);
            ImGui::End();
            ImGui::PopStyleColor(2);
            ImGui::PopStyleVar(3);

            return (headerRequestedClose || footerRequestedClose)
                ? ModalFrameResult::RequestClose(ModalCloseReason::Cancelled)
                : ModalFrameResult::None();
        }

    } // namespace

    ModalFrameResult DrawSettingsModalPresentation(SettingsState &state, EditorSettingsService &settings,
                                                   const Theme::Fonts &fonts, const ::ImTextureID logo)
    {
        return DrawSettingsModalPresentationImpl(state, settings, fonts, logo);
    }

    ModalFrameResult SettingsModal::Draw()
    {
        return DrawSettingsModalPresentation(m_draft, m_settings, m_fonts, static_cast<::ImTextureID>(m_logo));
    }
} // namespace Horo::Editor
