#include "editor/modals/settings/SettingsModal.h"

#include "Horo/Editor/EditorSettingsStore.h"
#include "Horo/Editor/EditorTheme.h"
#include "Horo/Editor/EditorUiComponents.h"

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
        using namespace Theme;
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
                       [&st, &f]() { ComboControl("##startup", &st.startupAction, kStartup.data(), static_cast<int>(kStartup.size()), f); });
            SettingRow("Auto-save Interval", "Minutes between automatic scene saves. Zero disables auto-save.", f,
                       [&st, &f]() { SliderIntControl("##autosave", &st.autoSaveInterval, 0, 30, "%d min", f); });
            SettingRow("Confirm Exit With Unsaved Changes", "Prompt before closing when unsaved changes exist.", f,
                       [&st, &f]() { (void)ToggleControl("confirm-exit", &st.confirmExit, f); });
            SettingGroup("EDITOR SESSION", f);
            SettingRow("Restore Workspace Layout", "Reopen tabs and panel layout from last session.", f,
                       [&st, &f]() { (void)ToggleControl("restore-workspace", &st.restoreWorkspace, f); });
            SettingRow("Default Scene On Project Open", "Scene file loaded automatically when opening a project.", f,
                       [&st, &f]() { InputTextControl("##default-scene", st.defaultScene, sizeof(st.defaultScene), f); });
        }

        void DrawAppearance(SettingsState &st, const Fonts &f)
        {
            SectionTitle("Appearance", f);
            SettingGroup("THEME", f, true);
            SettingRow("Color Theme", "Built-in themes or custom JSON theme file.", f, [&st, &f]() {
                static constexpr std::array<const char *, 3> kThemes = {"Horo Dark", "Midnight", "Light"};
                const int prev = st.themeIndex;
                ComboControl("##theme", &st.themeIndex, kThemes.data(), static_cast<int>(kThemes.size()), f);
                if (st.themeIndex != prev)
                {
                    constexpr Theme::Preset kPresets[] = {Theme::Preset::HoroDark, Theme::Preset::Midnight, Theme::Preset::Light};
                    Theme::SetThemePreset(kPresets[st.themeIndex]);
                    Theme::ApplyCurrentTheme();
                    st.dirty = true;
                }
            });
            SettingRow("Custom Theme", "Path to a JSON theme file. Leave empty to use built-in.", f, [&st, &f]() {
                InputTextControl("##custom-theme", st.customThemePath, sizeof(st.customThemePath), f);
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
            SettingRow("Code Font Size", "Point size for the script editor and console output.", f,
                       [&st, &f]() { InputTextControl("##font-size", st.editorFontSize, sizeof(st.editorFontSize), f); });
        }

        void DrawInput(SettingsState &st, const Fonts &f)
        {
            SectionTitle("Input", f);
            SettingGroup("NAVIGATION", f, true);
            SettingRow("Orbit Sensitivity", "Mouse drag multiplier for orbiting the viewport camera.", f,
                       [&st, &f]() { SliderIntControl("##orbit", &st.orbitSensitivity, 10, 300, "%d", f); });
            SettingRow("Pan Sensitivity", "Mouse drag multiplier for panning the viewport camera.", f,
                       [&st, &f]() { SliderIntControl("##pan", &st.panSensitivity, 10, 300, "%d", f); });
            SettingRow("Invert Orbit Y", "Reverse the vertical orbit direction (push up to look down).", f,
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
            SettingRow("Default Viewport Mode", "Shading mode used for new viewport panels.", f, [&st, &f]() { ComboControl("##viewport", &st.viewportMode, kViewport.data(), static_cast<int>(kViewport.size()), f); });
            SettingRow("Grid Overlay", "Show the reference grid in the editor viewport.", f, [&st, &f]() { (void)ToggleControl("grid", &st.gridOverlay, f); });
            SettingGroup("QUALITY", f);
            SettingRow("Editor Rendering Tier", "Maximum feature tier used in the editor viewport.", f,
                       [&st, &f]() { ComboControl("##tier", &st.renderingTier, kTier.data(), static_cast<int>(kTier.size()), f); });
            SettingRow("Texture Streaming Budget", "Maximum texture memory pool used by the streaming system.", f,
                       [&st, &f]() { InputTextControl("##texture-budget", st.textureBudget, sizeof(st.textureBudget), f); });
        }

        void DrawAudio(SettingsState &st, const Fonts &f)
        {
            static constexpr std::array<const char *, 3> kDevices = {"System Default", "Headphones", "Speakers"};
            SectionTitle("Audio", f);
            SettingGroup("OUTPUT", f, true);
            SettingRow("Master Volume", "Global audio output level for the editor (0–100).", f,
                       [&st, &f]() { SliderIntControl("##volume", &st.masterVolume, 0, 100, "%d", f); });
            SettingRow("Audio Output Device", "Preferred playback device for editor audio.", f,
                       [&st, &f]() { ComboControl("##audio-device", &st.audioOutputDevice, kDevices.data(), static_cast<int>(kDevices.size()), f); });
            SettingRow("Enable Audio In Editor", "Play in-editor sounds and preview audio assets.", f,
                       [&st, &f]() { (void)ToggleControl("audio-enabled", &st.audioEnabled, f); });
        }

        void DrawNetwork(SettingsState &st, const Fonts &f)
        {
            SectionTitle("Network", f);
            SettingGroup("MULTIPLAYER PREVIEW", f, true);
            SettingRow("Max Preview Clients", "Maximum concurrent PIE (Play In Editor) client connections.", f,
                       [&st, &f]() { InputIntControl("##max-clients", &st.maxPreviewClients, f); });
            SettingRow("Simulate Latency", "Artificial one-way delay injected on loopback (ms).", f,
                       [&st, &f]() { SliderIntControl("##latency", &st.simulatedLatencyMs, 0, 500, "%d ms", f); });
            SettingRow("Package Download Threads", "Parallel download workers for template and asset packages.", f,
                       [&st, &f]() { InputIntControl("##download-threads", &st.packageDownloadThreads, f); });
        }

        void DrawDiagnostics(SettingsState &st, const Fonts &f)
        {
            static constexpr std::array<const char *, 4> kLogLevels = {"Debug", "Info", "Warning", "Error"};
            SectionTitle("Diagnostics", f);
            SettingGroup("LOGGING", f, true);
            SettingRow("Console Log Level", "Minimum severity shown in the editor console output.", f,
                       [&st, &f]() { ComboControl("##log-level", &st.consoleLogLevel, kLogLevels.data(), static_cast<int>(kLogLevels.size()), f); });
            SettingRow("Write Log To File", "Persist the console log to a timestamped file on disk.", f,
                       [&st, &f]() { (void)ToggleControl("write-log", &st.writeLogToFile, f); });
            SettingGroup("PROFILER", f);
            SettingRow("Auto-capture On Stutter", "Automatically start a profiler capture when a frame spike is detected.", f,
                       [&st, &f]() { (void)ToggleControl("capture-stutter", &st.autoCaptureStutter, f); });
            SettingRow("Stutter Threshold (ms)", "Frame time above this value triggers an auto-capture.", f,
                       [&st, &f]() { InputFloatControl("##stutter", &st.stutterThresholdMs, f); });
        }

        void DrawPluginSettingsToggle(const char *id, bool *expanded, const Fonts &f)
        {
            ImGui::PushID(id);
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 18.0F);
            ImGui::PushStyleColor(ImGuiCol_Text, Theme::Accent());
            {
                ScopedTextStyle ts(f.mono, 10.0F, Theme::FontPx::Mono);
                if (ImGui::Selectable(*expanded ? "▼ Settings" : "▶ Settings", false, 0, {150.0F, 18.0F}))
                {
                    *expanded = !*expanded;
                }
            }
            ImGui::PopStyleColor();
            ImGui::PopID();
        }

        void DrawPlugins(SettingsState &st, const Fonts &f)
        {
            SectionTitle("Plugins", f);
            SettingGroup("INSTALLED", f, true);

            // --- Horo MCP Bridge ---
            PluginRow("Horo MCP Bridge", "v0.4.0", "Enables MCP tool access for scene and asset operations.", &st.horoMcpBridge, f);
            DrawPluginSettingsToggle("mcp", &st.pluginMcpExpanded, f);
            if (st.pluginMcpExpanded)
            {
                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{10.0F, 10.0F});
                ImGui::PushStyleColor(ImGuiCol_ChildBg, Theme::Bg3());
                ImGui::BeginChild("mcp-settings", {0.0F, 100.0F}, true,
                                  ImGuiWindowFlags_AlwaysUseWindowPadding);
                SettingRow("MCP Port", "HTTP port for MCP server.", f,
                           [&st, &f]() { InputIntControl("##mcp-port", &st.mcpPort, f); });
                SettingRow("Allow Remote Connections", nullptr, f,
                           [&st, &f]() { (void)ToggleControl("##mcp-remote", &st.mcpAllowRemote, f); });
                ImGui::EndChild();
                ImGui::PopStyleColor();
                ImGui::PopStyleVar();
            }

            // --- Vendor FMOD Integration ---
            PluginRow("Vendor FMOD Integration", "v2.02.20", "Full FMOD Studio authoring and runtime integration.", &st.fmodIntegration, f);
            DrawPluginSettingsToggle("fmod", &st.pluginFmodExpanded, f);
            if (st.pluginFmodExpanded)
            {
                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{10.0F, 10.0F});
                ImGui::PushStyleColor(ImGuiCol_ChildBg, Theme::Bg3());
                ImGui::BeginChild("fmod-settings", {0.0F, 100.0F}, true,
                                  ImGuiWindowFlags_AlwaysUseWindowPadding);
                SettingRow("Bank Output Path", "Where compiled banks are written.", f,
                           [&st, &f]() { InputTextControl("##fmod-banks", st.fmodBankPath, sizeof(st.fmodBankPath), f); });
                SettingRow("Live Update", "Reload bapluginFmodExpandednks without restarting editor.", f,
                           [&st, &f]() { (void)ToggleControl("##fmod-live", &st.fmodLiveUpdate, f); });
                ImGui::EndChild();
                ImGui::PopStyleColor();
                ImGui::PopStyleVar();
            }

            // --- Steamworks SDK ---
            PluginRow("Steamworks SDK", "v1.59", "Steam achievements, overlay, and networking features.", &st.steamworksSdk, f);
            DrawPluginSettingsToggle("steam", &st.pluginSteamExpanded, f);
            if (st.pluginSteamExpanded)
            {
                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{10.0F, 10.0F});
                ImGui::PushStyleColor(ImGuiCol_ChildBg, Theme::Bg3());
                ImGui::BeginChild("steam-settings", {0.0F, 100.0F}, true,
                                  ImGuiWindowFlags_AlwaysUseWindowPadding);
                SettingRow("App ID", "Steam application identifier.", f,
                           [&st, &f]() { InputIntControl("##steam-appid", &st.steamAppId, f); });
                SettingRow("Auto-initialize", "Start Steam API on editor launch.", f,
                           [&st, &f]() { (void)ToggleControl("##steam-auto", &st.steamAutoInit, f); });
                ImGui::EndChild();
                ImGui::PopStyleColor();
                ImGui::PopStyleVar();
            }

            SettingGroup("DISCOVERY", f);
            SettingRow("Plugin Discovery Path", "Directory scanned for additional editor plugins on startup.", f,
                       [&st, &f]() { InputTextControl("##plugin-path", st.pluginPath, sizeof(st.pluginPath), f); });
        }

        void DrawContent(SettingsState &st, const Fonts &f, const float bodyH)
        {
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{26.0F, 22.0F});
            ImGui::PushStyleColor(ImGuiCol_ChildBg, Theme::Bg1());
            ImGui::BeginChild("SettingsContent", {0.0F, bodyH}, false, ImGuiWindowFlags_AlwaysUseWindowPadding);

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
            if (Button({.label = "Restore Defaults", .size = {restoreW, 34.0F}, .variant = Ui::ButtonVariant::Secondary, .fontSize = 13.0F, .font = f.mono, .baseFontSize = Theme::FontPx::Mono}))
            {
                ApplySettingsToDraft(st, DefaultEditorSettings());
                st.statusMessage = "Defaults loaded into draft. Apply to persist.";
                st.statusIsError = false;
            }
            ImGui::SameLine(0.0F, gap);
            if (Button({.label = "Cancel", .size = {cancelW, 34.0F}, .variant = Ui::ButtonVariant::Secondary, .fontSize = 13.0F, .font = f.mono, .baseFontSize = Theme::FontPx::Mono}))
            {
                DiscardSettingsAndClose(st);
            }
            ImGui::SameLine(0.0F, gap);
            if (Button({.label = "Apply", .size = {applyW, 34.0F}, .variant = Ui::ButtonVariant::Primary, .fontSize = 13.0F, .font = f.mono, .baseFontSize = Theme::FontPx::Mono}))
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

            if (const bool isOpen = ImGui::BeginPopupModal("Settings",
                                                       &st.open,
                                                       ImGuiWindowFlags_NoResize |
                                                           ImGuiWindowFlags_NoTitleBar |
                                                           ImGuiWindowFlags_NoMove |
                                                           ImGuiWindowFlags_NoSavedSettings |
                                                           ImGuiWindowFlags_NoScrollbar |
                                                           ImGuiWindowFlags_NoScrollWithMouse);
                !isOpen)
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
