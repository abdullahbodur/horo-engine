#include "editor/modals/settings/SettingsModal.h"
#include "Horo/Editor/SettingsModal.h"

#include "Horo/Editor/EditorSettingsStore.h"
#include "Horo/Editor/EditorTheme.h"
#include "Horo/Editor/EditorUiComponents.h"
#include "Horo/Extensions/ExtensionInventory.h"
#include "Horo/Extensions/ExtensionMarketplace.h"
#include "Horo/Foundation/Logging/Logger.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <ranges>
#include <span>
#include <vector>

namespace Horo::Editor
{
    namespace
    {
        using Theme::Fonts;
        using namespace Theme;

        // Forward declarations for plugin detail functions
        void DrawInstalledPlugins(SettingsState& st, const EditorGuiContext& ctx);
        void DrawPluginDetailPanel(SettingsState& st, const EditorGuiContext& ctx, float w, bool embedded = false);
        void DrawMcpDetailContent(SettingsState& st, const EditorGuiContext& ctx, int activeTab);
        void DrawFmodDetailContent(SettingsState& st, const EditorGuiContext& ctx, int activeTab);
        void DrawSteamDetailContent(SettingsState& st, const EditorGuiContext& ctx, int activeTab);
        void DrawRuntimeDiscovery(SettingsState& st, const EditorGuiContext& ctx);
        void DrawExtensionManager(SettingsState& st, const EditorGuiContext& ctx);
        using namespace Ui;
        using Theme::ScopedTextStyle;

        namespace Layout
        {
            constexpr float ModalW = 960.0F;
            constexpr float ModalH = 680.0F;
            constexpr float ViewportPad = 48.0F;
            constexpr float HeaderH = 57.0F;
            constexpr float FooterH = Theme::Layout::FooterH;
            constexpr float NavW = 200.0F;
            constexpr float Radius = 4.0F;
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
            const char* label;
            const char* icon;
            SettingsTab tab;
        };

        struct PluginSpec
        {
            const char* name;
            const char* desc;
            const char* version;
            const char* statusLabel;
            BadgeTone statusTone;
            const char* category;
            int idx;
            bool* enabled;
        };

        struct PluginDetailHeaderSpec
        {
            const char* name;
            const char* desc;
            const char* scopeBadge;
            const char* signedBadge;
            BadgeTone signedTone;
            const char* restartBadge;
            const char* action1;
            const char* action2;
        };

        void DrawNavGroup(const char* label, const EditorGuiContext& ctx)
        {
            ImGui::Dummy({0.0F, 5.0F});
            ScopedTextStyle ts(ctx.theme.fonts.sansEmphasis, 12.0F, FontPx::SansEmphasis);
            ImGui::PushStyleColor(ImGuiCol_Text, Dim());
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 10.0F);
            ImGui::TextUnformatted(label);
            ImGui::PopStyleColor();
        }

        void DrawNavItem(SettingsState& st, const NavItem item, const EditorGuiContext& ctx)
        {
            const bool active = st.activeTab == static_cast<int>(item.tab);
            const ImVec2 pos = ImGui::GetCursorScreenPos();
            const float rowW = ImGui::GetContentRegionAvail().x;
            constexpr float rowH = 38.0F;

            ImGui::PushID(item.label);
            ImGui::InvisibleButton("nav", {rowW, rowH});
            if (ImGui::IsItemClicked())
            {
                if (st.activeTab != static_cast<int>(item.tab))
                {
                    LOG_DEBUG("editor.settings", "Settings tab changed to '%s'.", item.label);
                }
                st.activeTab = static_cast<int>(item.tab);
            }
            const bool hovered = ImGui::IsItemHovered();

            auto* dl = ImGui::GetWindowDrawList();
            if (active || hovered)
            {
                const auto accentGlow = ImVec4{Accent().x, Accent().y, Accent().z, 0.14F};
                dl->AddRectFilled(pos, {pos.x + rowW, pos.y + rowH}, U32(active ? accentGlow : Hover()),
                                  Layout::Radius);
            }
            if (active)
            {
                dl->AddRectFilled(pos, {pos.x + 2.0F, pos.y + rowH}, U32(Accent()), 1.0F);
            }

            ImGui::SetCursorScreenPos({pos.x + 12.0F, pos.y + 10.0F});
            {
                ScopedTextStyle ts(ctx.theme.fonts.sansEmphasis, 14.0F, FontPx::SansEmphasis);
                ImGui::PushStyleColor(ImGuiCol_Text, active ? Accent() : Muted());
                ImGui::TextUnformatted(item.icon);
                ImGui::PopStyleColor();
            }
            ImGui::SameLine(0.0F, 10.0F);
            {
                ScopedTextStyle ts(ctx.theme.fonts.sans, 15.0F, FontPx::Sans);
                ImGui::PushStyleColor(ImGuiCol_Text, active ? Text() : Muted());
                ImGui::TextUnformatted(item.label);
                ImGui::PopStyleColor();
            }
            ImGui::SetCursorScreenPos({pos.x, pos.y + rowH + 1.0F});
            ImGui::PopID();
        }

        void DrawNavigationContent(SettingsState& st, const EditorGuiContext& ctx)
        {
            using enum SettingsTab;
            const std::string editor = ctx.localization.Get("editor", "settings.nav.editor");
            const std::string general = ctx.localization.Get("editor", "settings.nav.general");
            const std::string appearance = ctx.localization.Get("editor", "settings.nav.appearance");
            const std::string input = ctx.localization.Get("editor", "settings.nav.input");
            const std::string engine = ctx.localization.Get("editor", "settings.nav.engine");
            const std::string rendering = ctx.localization.Get("editor", "settings.nav.rendering");
            const std::string audio = ctx.localization.Get("editor", "settings.nav.audio");
            const std::string network = ctx.localization.Get("editor", "settings.nav.network");
            const std::string tools = ctx.localization.Get("editor", "settings.nav.tools");
            const std::string diagnostics = ctx.localization.Get("editor", "settings.nav.diagnostics");
            const std::string plugins = ctx.localization.Get("editor", "settings.nav.plugins");
            DrawNavGroup(editor.c_str(), ctx);
            DrawNavItem(st, {general.c_str(), "G", General}, ctx);
            DrawNavItem(st, {appearance.c_str(), "A", Appearance}, ctx);
            DrawNavItem(st, {input.c_str(), "I", Input}, ctx);
            DrawNavGroup(engine.c_str(), ctx);
            DrawNavItem(st, {rendering.c_str(), "R", Rendering}, ctx);
            DrawNavItem(st, {audio.c_str(), "S", Audio}, ctx);
            DrawNavItem(st, {network.c_str(), "N", Network}, ctx);
            DrawNavGroup(tools.c_str(), ctx);
            DrawNavItem(st, {diagnostics.c_str(), "D", Diagnostics}, ctx);
            DrawNavItem(st, {plugins.c_str(), "P", Plugins}, ctx);
        }

        void DrawGeneral(SettingsState& st, const EditorGuiContext& ctx)
        {
            const std::array<std::string, 3> startupStr = {
                ctx.localization.Get("editor", "settings.general.startup_welcome"),
                ctx.localization.Get("editor", "settings.general.startup_last"),
                ctx.localization.Get("editor", "settings.general.startup_browser")
            };
            const std::array<const char*, 3> kStartup = {
                startupStr[0].c_str(), startupStr[1].c_str(), startupStr[2].c_str()
            };
            const std::string sectionTitle = ctx.localization.Get("editor", "settings.nav.general");
            SectionTitle(sectionTitle.c_str(), ctx.theme.fonts);
            const std::string startupGroup = ctx.localization.Get("editor", "settings.general.startup_group");
            SettingGroup(startupGroup.c_str(), ctx.theme.fonts, true);
            const std::string startupLabel = ctx.localization.Get("editor", "settings.general.startup_behavior");
            const std::string startupDescription =
                ctx.localization.Get("editor", "settings.general.startup_behavior.description");
            const std::string autosaveLabel = ctx.localization.Get("editor", "settings.general.autosave_interval");
            const std::string autosaveDescription =
                ctx.localization.Get("editor", "settings.general.autosave_interval.description");
            const std::string confirmLabel = ctx.localization.Get("editor", "settings.general.confirm_exit");
            const std::string confirmDescription = ctx.localization.Get(
                "editor", "settings.general.confirm_exit.description");
            const std::string restoreLabel = ctx.localization.Get("editor", "settings.general.restore_workspace");
            const std::string restoreDescription =
                ctx.localization.Get("editor", "settings.general.restore_workspace.description");
            const std::string defaultSceneLabel = ctx.localization.Get("editor", "settings.general.default_scene");
            const std::string defaultSceneDescription =
                ctx.localization.Get("editor", "settings.general.default_scene.description");
            SettingRow(startupLabel.c_str(), startupDescription.c_str(), ctx.theme.fonts, [&st, &ctx, kStartup]()
            {
                (void)ComboControl("##startup", &st.general.startupAction, kStartup.data(), kStartup.size(),
                                   ctx.theme.fonts);
            });
            SettingRow(autosaveLabel.c_str(), autosaveDescription.c_str(), ctx.theme.fonts, [&st, &ctx]()
            {
                SliderIntControl("##autosave", &st.general.autoSaveInterval, 0, 30, SliderValueFormat::Minutes,
                                 ctx.theme.fonts);
            });
            SettingRow(confirmLabel.c_str(), confirmDescription.c_str(), ctx.theme.fonts,
                       [&st, &ctx]()
                       {
                           (void)ToggleControl("confirm-exit", &st.general.confirmExit, ctx.theme.fonts);
                       });
            const std::string sessionGroup = ctx.localization.Get("editor", "settings.general.session_group");
            SettingGroup(sessionGroup.c_str(), ctx.theme.fonts);
            SettingRow(restoreLabel.c_str(), restoreDescription.c_str(), ctx.theme.fonts, [&st, &ctx]()
            {
                (void)ToggleControl("restore-workspace", &st.general.restoreWorkspace, ctx.theme.fonts);
            });
            SettingRow(defaultSceneLabel.c_str(), defaultSceneDescription.c_str(), ctx.theme.fonts, [&st, &ctx]()
            {
                (void)InputTextControl("##default-scene", st.general.defaultScene, 64, ctx.theme.fonts);
            });
            const std::string english = ctx.localization.Get("editor", "settings.language.english");
            const std::string turkish = ctx.localization.Get("editor", "settings.language.turkish");
            const std::array<const char*, 2> kLanguages = {english.c_str(), turkish.c_str()};
            const std::string languageLabel = ctx.localization.Get("editor", "settings.language");
            const std::string languageDescription = ctx.localization.Get("editor", "settings.language.description");
            SettingRow(languageLabel.c_str(), languageDescription.c_str(), ctx.theme.fonts, [&st, &ctx, kLanguages]()
            {
                int languageIndex = st.general.languageTag == "tr-TR" ? 1 : 0;
                if (ComboControl("##language", &languageIndex, kLanguages.data(), kLanguages.size(), ctx.theme.fonts))
                {
                    st.general.languageTag = languageIndex == 1 ? "tr-TR" : "en-US";
                    st.dirty = true;
                }
            });
        }

        void DrawAppearance(SettingsState& st, const EditorGuiContext& ctx)
        {
            const std::string sectionTitle = ctx.localization.Get("editor", "settings.nav.appearance");
            SectionTitle(sectionTitle.c_str(), ctx.theme.fonts);
            const std::string themeGroup = ctx.localization.Get("editor", "settings.appearance.theme_group");
            SettingGroup(themeGroup.c_str(), ctx.theme.fonts, true);
            const std::string colorThemeLabel = ctx.localization.Get("editor", "settings.appearance.color_theme");
            const std::string colorThemeDescription =
                ctx.localization.Get("editor", "settings.appearance.color_theme.description");
            const std::string customThemeLabel = ctx.localization.Get("editor", "settings.appearance.custom_theme");
            const std::string customThemeDescription =
                ctx.localization.Get("editor", "settings.appearance.custom_theme.description");
            const std::string accentLabel = ctx.localization.Get("editor", "settings.appearance.accent_color");
            const std::string accentDescription =
                ctx.localization.Get("editor", "settings.appearance.accent_color.description");
            const std::string scaleLabel = ctx.localization.Get("editor", "settings.appearance.ui_scale");
            const std::string scaleDescription = ctx.localization.Get(
                "editor", "settings.appearance.ui_scale.description");
            const std::string fontSizeLabel = ctx.localization.Get("editor", "settings.appearance.code_font_size");
            const std::string fontSizeDescription =
                ctx.localization.Get("editor", "settings.appearance.code_font_size.description");
            SettingRow(colorThemeLabel.c_str(), colorThemeDescription.c_str(), ctx.theme.fonts, [&st, &ctx]()
            {
                const auto& themeList = GetThemeList();
                static std::vector<const char*> s_names;
                s_names.clear();
                for (const auto& t : themeList)
                    s_names.push_back(t.name.c_str());

                const auto count = static_cast<int>(s_names.size());
                if (st.appearance.themeIndex >= count)
                    st.appearance.themeIndex = 0;

                const int prev = st.appearance.themeIndex;
                (void)ComboControl("##theme", &st.appearance.themeIndex, s_names.data(), count, ctx.theme.fonts);
                if (st.appearance.themeIndex != prev)
                {
                    // Defer: apply at start of next frame to avoid mid-frame style glitches
                    st.appearance.pendingThemeIndex = st.appearance.themeIndex;
                    st.dirty = true;
                }
            });
            SettingRow(customThemeLabel.c_str(), customThemeDescription.c_str(), ctx.theme.fonts, [&st, &ctx]()
            {
                (void)InputTextControl("##custom-theme", st.appearance.customThemePath, 128, ctx.theme.fonts);
            });
            SettingRow(accentLabel.c_str(), accentDescription.c_str(), ctx.theme.fonts,
                       [&st, &ctx]()
                       {
                           (void)ColorHexControl("accent-color", st.appearance.accentHex, 16, ctx.theme.fonts);
                       });
            const std::string typoGroup = ctx.localization.Get("editor", "settings.appearance.typography_group");
            SettingGroup(typoGroup.c_str(), ctx.theme.fonts);
            SettingRow(scaleLabel.c_str(), scaleDescription.c_str(), ctx.theme.fonts, [&st, &ctx]()
            {
                SliderIntControl("##ui-scale", &st.appearance.uiScale, 75, 200, SliderValueFormat::Percent,
                                 ctx.theme.fonts,
                                 25);
            });
            SettingRow(fontSizeLabel.c_str(), fontSizeDescription.c_str(), ctx.theme.fonts, [&st, &ctx]()
            {
                (void)InputTextControl("##font-size", st.appearance.editorFontSize, 8, ctx.theme.fonts);
            });
        }

        void DrawInput(SettingsState& st, const EditorGuiContext& ctx)
        {
            const std::string sectionTitle = ctx.localization.Get("editor", "settings.nav.input");
            SectionTitle(sectionTitle.c_str(), ctx.theme.fonts);
            const std::string navGroup = ctx.localization.Get("editor", "settings.input.navigation_group");
            SettingGroup(navGroup.c_str(), ctx.theme.fonts, true);
            const std::string orbitLabel = ctx.localization.Get("editor", "settings.input.orbit_sensitivity");
            const std::string orbitDescription = ctx.localization.Get(
                "editor", "settings.input.orbit_sensitivity.description");
            const std::string panLabel = ctx.localization.Get("editor", "settings.input.pan_sensitivity");
            const std::string panDescription = ctx.localization.Get(
                "editor", "settings.input.pan_sensitivity.description");
            const std::string invertLabel = ctx.localization.Get("editor", "settings.input.invert_orbit_y");
            const std::string invertDescription = ctx.localization.Get(
                "editor", "settings.input.invert_orbit_y.description");
            SettingRow(orbitLabel.c_str(), orbitDescription.c_str(), ctx.theme.fonts, [&st, &ctx]()
            {
                SliderIntControl("##orbit", &st.input.orbitSensitivity, 10, 300, SliderValueFormat::Integer,
                                 ctx.theme.fonts);
            });
            SettingRow(panLabel.c_str(), panDescription.c_str(), ctx.theme.fonts, [&st, &ctx]()
            {
                SliderIntControl("##pan", &st.input.panSensitivity, 10, 300, SliderValueFormat::Integer,
                                 ctx.theme.fonts);
            });
            SettingRow(invertLabel.c_str(), invertDescription.c_str(), ctx.theme.fonts,
                       [&st, &ctx]() { (void)ToggleControl("invert-y", &st.input.invertOrbitY, ctx.theme.fonts); });

            const std::string mappingsGroup = ctx.localization.Get("editor", "settings.input.mappings_group");
            const std::string mappingsLabel = ctx.localization.Get("editor", "settings.input.mappings_label");
            const std::string mappingsDescription =
                ctx.localization.Get("editor", "settings.input.mappings_description");
            SettingGroup(mappingsGroup.c_str(), ctx.theme.fonts);
            SettingRow(mappingsLabel.c_str(), mappingsDescription.c_str(), ctx.theme.fonts, []()
            {
            });
        }

        void DrawRendering(SettingsState& st, const EditorGuiContext& ctx)
        {
            const std::array<std::string, 4> viewportText = {
                ctx.localization.Get("editor", "settings.rendering.shaded"),
                ctx.localization.Get("editor", "settings.rendering.wireframe"),
                ctx.localization.Get("editor", "settings.rendering.lit"),
                ctx.localization.Get("editor", "settings.rendering.unlit")
            };
            const std::array<const char*, 4> kViewport = {
                viewportText[0].c_str(), viewportText[1].c_str(),
                viewportText[2].c_str(), viewportText[3].c_str()
            };
            const std::array<std::string, 4> tierText = {
                ctx.localization.Get("editor", "settings.rendering.high_end"),
                ctx.localization.Get("editor", "settings.rendering.dx12_vulkan"),
                ctx.localization.Get("editor", "settings.rendering.dx11"),
                ctx.localization.Get("editor", "settings.rendering.es3")
            };
            const std::array<const char*, 4> kTier = {
                tierText[0].c_str(), tierText[1].c_str(), tierText[2].c_str(),
                tierText[3].c_str()
            };
            const std::string sectionTitle = ctx.localization.Get("editor", "settings.nav.rendering");
            SectionTitle(sectionTitle.c_str(), ctx.theme.fonts);
            const std::string viewportGroup = ctx.localization.Get("editor", "settings.rendering.viewport_group");
            SettingGroup(viewportGroup.c_str(), ctx.theme.fonts, true);
            const std::string viewportLabel = ctx.localization.Get("editor", "settings.rendering.viewport_mode");
            const std::string viewportDescription =
                ctx.localization.Get("editor", "settings.rendering.viewport_mode.description");
            const std::string gridLabel = ctx.localization.Get("editor", "settings.rendering.grid_overlay");
            const std::string gridDescription = ctx.localization.Get(
                "editor", "settings.rendering.grid_overlay.description");
            const std::string tierLabel = ctx.localization.Get("editor", "settings.rendering.tier");
            const std::string tierDescription = ctx.localization.Get("editor", "settings.rendering.tier.description");
            const std::string budgetLabel = ctx.localization.Get("editor", "settings.rendering.texture_budget");
            const std::string budgetDescription =
                ctx.localization.Get("editor", "settings.rendering.texture_budget.description");
            SettingRow(viewportLabel.c_str(), viewportDescription.c_str(), ctx.theme.fonts, [&st, &ctx, kViewport]()
            {
                (void)ComboControl("##viewport", &st.rendering.viewportMode, kViewport.data(), kViewport.size(),
                                   ctx.theme.fonts);
            });
            SettingRow(gridLabel.c_str(), gridDescription.c_str(), ctx.theme.fonts,
                       [&st, &ctx]() { (void)ToggleControl("grid", &st.rendering.gridOverlay, ctx.theme.fonts); });
            const std::string qualityGroup = ctx.localization.Get("editor", "settings.rendering.quality_group");
            SettingGroup(qualityGroup.c_str(), ctx.theme.fonts);
            SettingRow(tierLabel.c_str(), tierDescription.c_str(), ctx.theme.fonts, [&st, &ctx, kTier]()
            {
                (void)ComboControl("##tier", &st.rendering.renderingTier, kTier.data(), kTier.size(), ctx.theme.fonts);
            });
            SettingRow(budgetLabel.c_str(), budgetDescription.c_str(), ctx.theme.fonts, [&st, &ctx]()
            {
                (void)InputTextControl("##texture-budget", st.rendering.textureBudget, 32, ctx.theme.fonts);
            });
        }

        void DrawAudio(SettingsState& st, const EditorGuiContext& ctx)
        {
            const std::array<std::string, 3> deviceText = {
                ctx.localization.Get("editor", "settings.audio.system_default"),
                ctx.localization.Get("editor", "settings.audio.headphones"),
                ctx.localization.Get("editor", "settings.audio.speakers")
            };
            const std::array<const char*, 3> kDevices = {
                deviceText[0].c_str(), deviceText[1].c_str(), deviceText[2].c_str()
            };
            const std::string sectionTitle = ctx.localization.Get("editor", "settings.nav.audio");
            SectionTitle(sectionTitle.c_str(), ctx.theme.fonts);
            const std::string outputGroup = ctx.localization.Get("editor", "settings.audio.output_group");
            SettingGroup(outputGroup.c_str(), ctx.theme.fonts, true);
            const std::string volumeLabel = ctx.localization.Get("editor", "settings.audio.master_volume");
            const std::string volumeDescription = ctx.localization.Get(
                "editor", "settings.audio.master_volume.description");
            const std::string deviceLabel = ctx.localization.Get("editor", "settings.audio.output_device");
            const std::string deviceDescription = ctx.localization.Get(
                "editor", "settings.audio.output_device.description");
            const std::string enabledLabel = ctx.localization.Get("editor", "settings.audio.enable_in_editor");
            const std::string enabledDescription =
                ctx.localization.Get("editor", "settings.audio.enable_in_editor.description");
            SettingRow(volumeLabel.c_str(), volumeDescription.c_str(), ctx.theme.fonts, [&st, &ctx]()
            {
                SliderIntControl("##volume", &st.audio.masterVolume, 0, 100, SliderValueFormat::Integer,
                                 ctx.theme.fonts);
            });
            SettingRow(deviceLabel.c_str(), deviceDescription.c_str(), ctx.theme.fonts, [&st, &ctx, kDevices]()
            {
                (void)ComboControl("##audio-device", &st.audio.audioOutputDevice, kDevices.data(), kDevices.size(),
                                   ctx.theme.fonts);
            });
            SettingRow(enabledLabel.c_str(), enabledDescription.c_str(), ctx.theme.fonts,
                       [&st, &ctx]()
                       {
                           (void)ToggleControl("audio-enabled", &st.audio.audioEnabled, ctx.theme.fonts);
                       });
        }

        void DrawNetwork(SettingsState& st, const EditorGuiContext& ctx)
        {
            const std::string sectionTitle = ctx.localization.Get("editor", "settings.nav.network");
            SectionTitle(sectionTitle.c_str(), ctx.theme.fonts);
            const std::string multiplayerGroup = ctx.localization.Get("editor", "settings.network.multiplayer_group");
            SettingGroup(multiplayerGroup.c_str(), ctx.theme.fonts, true);
            const std::string clientsLabel = ctx.localization.Get("editor", "settings.network.max_preview_clients");
            const std::string clientsDescription =
                ctx.localization.Get("editor", "settings.network.max_preview_clients.description");
            const std::string latencyLabel = ctx.localization.Get("editor", "settings.network.simulate_latency");
            const std::string latencyDescription =
                ctx.localization.Get("editor", "settings.network.simulate_latency.description");
            const std::string threadsLabel = ctx.localization.Get("editor", "settings.network.download_threads");
            const std::string threadsDescription =
                ctx.localization.Get("editor", "settings.network.download_threads.description");
            SettingRow(clientsLabel.c_str(), clientsDescription.c_str(), ctx.theme.fonts,
                       [&st, &ctx]()
                       {
                           InputIntControl("##max-clients", &st.network.maxPreviewClients, ctx.theme.fonts);
                       });
            SettingRow(latencyLabel.c_str(), latencyDescription.c_str(), ctx.theme.fonts, [&st, &ctx]()
            {
                SliderIntControl("##latency", &st.network.simulatedLatencyMs, 0, 500, SliderValueFormat::Milliseconds,
                                 ctx.theme.fonts);
            });
            SettingRow(threadsLabel.c_str(), threadsDescription.c_str(), ctx.theme.fonts, [&st, &ctx]()
            {
                InputIntControl("##download-threads", &st.network.packageDownloadThreads, ctx.theme.fonts);
            });
        }

        void DrawDiagnostics(SettingsState& st, const EditorGuiContext& ctx)
        {
            const std::array<std::string, 4> logLevelText = {
                ctx.localization.Get("editor", "settings.diagnostics.debug"),
                ctx.localization.Get("editor", "settings.diagnostics.info"),
                ctx.localization.Get("editor", "settings.diagnostics.warning"),
                ctx.localization.Get("editor", "settings.diagnostics.error")
            };
            const std::array<const char*, 4> kLogLevels = {
                logLevelText[0].c_str(), logLevelText[1].c_str(),
                logLevelText[2].c_str(), logLevelText[3].c_str()
            };
            const std::string sectionTitle = ctx.localization.Get("editor", "settings.nav.diagnostics");
            SectionTitle(sectionTitle.c_str(), ctx.theme.fonts);
            const std::string loggingGroup = ctx.localization.Get("editor", "settings.diagnostics.logging_group");
            SettingGroup(loggingGroup.c_str(), ctx.theme.fonts, true);
            const std::string logLevelLabel = ctx.localization.Get("editor", "settings.diagnostics.log_level");
            const std::string logLevelDescription =
                ctx.localization.Get("editor", "settings.diagnostics.log_level.description");
            const std::string writeLogLabel = ctx.localization.Get("editor", "settings.diagnostics.write_log");
            const std::string writeLogDescription =
                ctx.localization.Get("editor", "settings.diagnostics.write_log.description");
            const std::string captureLabel = ctx.localization.Get("editor", "settings.diagnostics.auto_capture");
            const std::string captureDescription =
                ctx.localization.Get("editor", "settings.diagnostics.auto_capture.description");
            const std::string thresholdLabel = ctx.localization.Get("editor", "settings.diagnostics.stutter_threshold");
            const std::string thresholdDescription =
                ctx.localization.Get("editor", "settings.diagnostics.stutter_threshold.description");
            SettingRow(logLevelLabel.c_str(), logLevelDescription.c_str(), ctx.theme.fonts, [&st, &ctx, kLogLevels]()
            {
                (void)ComboControl("##log-level", &st.diagnostics.consoleLogLevel, kLogLevels.data(), kLogLevels.size(),
                                   ctx.theme.fonts);
            });
            SettingRow(writeLogLabel.c_str(), writeLogDescription.c_str(), ctx.theme.fonts,
                       [&st, &ctx]()
                       {
                           (void)ToggleControl("write-log", &st.diagnostics.writeLogToFile, ctx.theme.fonts);
                       });
            const std::string profilerGroup = ctx.localization.Get("editor", "settings.diagnostics.profiler_group");
            SettingGroup(profilerGroup.c_str(), ctx.theme.fonts);
            SettingRow(captureLabel.c_str(), captureDescription.c_str(), ctx.theme.fonts, [&st, &ctx]()
            {
                (void)ToggleControl("capture-stutter", &st.diagnostics.autoCaptureStutter, ctx.theme.fonts);
            });
            SettingRow(thresholdLabel.c_str(), thresholdDescription.c_str(), ctx.theme.fonts,
                       [&st, &ctx]()
                       {
                           InputFloatControl("##stutter", &st.diagnostics.stutterThresholdMs, ctx.theme.fonts);
                       });
        }

        void DrawPluginsHeader(SettingsState& st, const EditorGuiContext& ctx)
        {
            const float availW = ImGui::GetContentRegionAvail().x;
            constexpr float btnW = 116.0F;
            constexpr float btnGap = 8.0F;
            constexpr float actionsW = btnW * 2.0F + btnGap;
            const float copyW = std::max(260.0F, availW - actionsW - 28.0F);
            const float startY = ImGui::GetCursorPosY();

            ImGui::BeginGroup();
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + copyW);
            ImGui::PushStyleColor(ImGuiCol_Text, Muted());
            {
                ScopedTextStyle ts(ctx.theme.fonts.sans, 12.5F, FontPx::Sans);
                const std::string pluginDescription = ctx.localization.Get("editor", "settings.plugins.description");
                ImGui::TextWrapped("%s", pluginDescription.c_str());
            }
            ImGui::PopStyleColor();
            ImGui::PopTextWrapPos();
            ImGui::EndGroup();

            ImGui::SameLine(0.0F, 0.0F);
            ImGui::SetCursorPos({ImGui::GetCursorPosX() + 24.0F, startY - 2.0F});
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2{12.0F, 6.0F});
            if (const std::string installAction = ctx.localization.Get("editor", "settings.plugins.action.install");
                ImGui::Button(installAction.empty() ? "Install..." : installAction.c_str(), {btnW, 32.0F}))
            {
                const std::string feedback =
                    ctx.localization.Get("editor", "settings.plugins.feedback.install_not_implemented");
                st.modalFeedback = feedback.empty() ? "Plugin installation dialog not yet implemented." : feedback;
            }
            ImGui::SameLine(0.0F, btnGap);
            if (const std::string reloadAction = ctx.localization.Get("editor", "settings.plugins.action.reload");
                ImGui::Button(reloadAction.empty() ? "Reload" : reloadAction.c_str(), {btnW, 32.0F}))
            {
                const std::string feedback = ctx.localization.Get("editor", "settings.plugins.feedback.reloaded");
                st.modalFeedback = feedback.empty() ? "Plugins reloaded successfully." : feedback;
            }
            ImGui::PopStyleVar();
        }

        void DrawPluginSectionTabs(SettingsState& st, const EditorGuiContext& ctx)
        {
            const std::array<std::string, 2> sectionTabsStr = {
                ctx.localization.Get("editor", "settings.plugins.installed"),
                ctx.localization.Get("editor", "settings.extensions.marketplace")
            };
            const std::array<const char*, 2> kSectionTabs = {sectionTabsStr[0].c_str(), sectionTabsStr[1].c_str()};
            constexpr float pad = 4.0F;
            constexpr float tabH = 31.0F;
            const float availableWidth = ImGui::GetContentRegionAvail().x;
            const float containerW = std::min(520.0F, availableWidth);
            const float installedW = 210.0F;
            const float runtimeW = containerW - installedW - pad * 3.0F;
            const float containerH = tabH + pad * 2.0F;
            const ImVec2 p = ImGui::GetCursorScreenPos();
            auto* dl = ImGui::GetWindowDrawList();

            dl->AddRectFilled(p, {p.x + containerW, p.y + containerH}, U32(Bg0()), Layout::Radius);
            dl->AddRect(p, {p.x + containerW, p.y + containerH}, U32(Border()), Layout::Radius);
            ImGui::SetCursorScreenPos({p.x + pad, p.y + pad});

            for (int i = 0; i < 2; ++i)
            {
                if (i > 0)
                    ImGui::SameLine(0.0F, 4.0F);
                const bool active = st.pluginSectionTab == i;
                const float tabW = i == 0 ? installedW : runtimeW;
                ImGui::PushID(i + 100);
                ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, Layout::Radius);
                ImGui::PushStyleColor(ImGuiCol_Button,
                                      active
                                          ? ImVec4{Accent().x, Accent().y, Accent().z, 0.12F}
                                          : ImVec4{0.0F, 0.0F, 0.0F, 0.0F});
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                                      active
                                          ? ImVec4{Accent().x, Accent().y, Accent().z, 0.18F}
                                          : Hover());
                ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                                      ImVec4{Accent().x, Accent().y, Accent().z, 0.22F});
                ImGui::PushStyleColor(ImGuiCol_Text, active ? Text() : Muted());
                {
                    ScopedTextStyle ts(ctx.theme.fonts.sans, 14.5F, FontPx::Sans);
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
                    dl->AddLine({bMin.x + 8.0F, bMax.y - 2.0F}, {bMax.x - 8.0F, bMax.y - 2.0F}, U32(Accent()),
                                2.0F);
                }
            }

            ImGui::SetCursorScreenPos({p.x, p.y + containerH + 12.0F});
        }

        void DrawPlugins(SettingsState& st, const EditorGuiContext& ctx)
        {
            DrawExtensionManager(st, ctx);
        }

        struct PermissionRowSpec
        {
            const char* icon;
            const char* title;
            const char* desc;
            const char* badgeText;
            BadgeTone badgeTone;
        };

        struct DiagnosticMetricSpec
        {
            const char* label;
            const char* value;
            const char* hint;
            ImVec4 valueColour;
        };

        template <typename DrawControl>
        void PluginSettingRow(const char* label, const char* description, const EditorGuiContext& ctx,
                              DrawControl drawControl)
        {
            const float rowW = ImGui::GetContentRegionAvail().x;
            const float startY = ImGui::GetCursorScreenPos().y;

            {
                ScopedTextStyle ts(ctx.theme.fonts.sans, 14.0F, FontPx::Sans);
                ImGui::PushStyleColor(ImGuiCol_Text, Text());
                ImGui::TextUnformatted(label);
                ImGui::PopStyleColor();
            }

            if (description != nullptr && description[0] != '\0')
            {
                ScopedTextStyle ts(ctx.theme.fonts.sans, 12.0F, FontPx::Sans);
                ImGui::PushStyleColor(ImGuiCol_Text, Muted());
                ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + rowW);
                ImGui::TextWrapped("%s", description);
                ImGui::PopTextWrapPos();
                ImGui::PopStyleColor();
            }

            ImGui::Dummy({0.0F, 6.0F});
            drawControl();
            ImGui::Dummy({0.0F, 8.0F});

            const ImVec2 sep = ImGui::GetCursorScreenPos();
            ImGui::GetWindowDrawList()->AddLine({sep.x, sep.y}, {sep.x + rowW, sep.y}, U32(Border()), 1.0F);
            ImGui::Dummy({0.0F, 10.0F});

            if (ImGui::GetCursorScreenPos().y - startY < 58.0F)
                ImGui::Dummy({0.0F, 58.0F - (ImGui::GetCursorScreenPos().y - startY)});
        }

        void DrawToggleState(const char* id, bool* value, const EditorGuiContext& ctx)
        {
            (void)ToggleControl(id, value, ctx.theme.fonts, false);
            ImGui::SameLine(0.0F, 8.0F);
            ScopedTextStyle ts(ctx.theme.fonts.sans, 12.5F, FontPx::Sans);
            ImGui::PushStyleColor(ImGuiCol_Text, *value ? Text() : Muted());
            const std::string enabledText = ctx.localization.Get("editor", "settings.plugins.status.enabled");
            const std::string disabledText = ctx.localization.Get("editor", "settings.plugins.status.disabled");
            ImGui::TextUnformatted(*value ? enabledText.c_str() : disabledText.c_str());
            ImGui::PopStyleColor();
        }

        void DrawPermissionRows(const std::span<const PermissionRowSpec> rows, const EditorGuiContext& ctx)
        {
            for (const auto& perm : rows)
            {
                ImGui::PushID(perm.title);
                const float cardW = ImGui::GetContentRegionAvail().x;
                constexpr float cardH = 66.0F;
                const ImVec2 p = ImGui::GetCursorScreenPos();
                const BadgeProps badge{
                    .label = perm.badgeText,
                    .tone = perm.badgeTone,
                };
                const float badgeW = Ui::BadgeWidth(badge, ctx.theme.fonts);
                auto* dl = ImGui::GetWindowDrawList();

                dl->AddRectFilled(p, {p.x + cardW, p.y + cardH}, U32(Bg3()), Layout::Radius);
                dl->AddRect(p, {p.x + cardW, p.y + cardH}, U32(Border()), Layout::Radius);

                ImGui::SetCursorScreenPos({p.x + 13.0F, p.y + 19.0F});
                {
                    ScopedTextStyle ts(ctx.theme.fonts.sansEmphasis, 14.0F, FontPx::SansEmphasis);
                    ImGui::PushStyleColor(
                        ImGuiCol_Text, BadgeToneColor(perm.badgeTone));
                    ImGui::TextUnformatted(perm.icon);
                    ImGui::PopStyleColor();
                }

                ImGui::SetCursorScreenPos({p.x + 40.0F, p.y + 11.0F});
                {
                    ScopedTextStyle ts(ctx.theme.fonts.sans, 12.5F, FontPx::Sans);
                    ImGui::PushStyleColor(ImGuiCol_Text, Text());
                    ImGui::TextUnformatted(perm.title);
                    ImGui::PopStyleColor();
                }

                ImGui::SetCursorScreenPos({p.x + 40.0F, p.y + 32.0F});
                {
                    ScopedTextStyle ts(ctx.theme.fonts.sans, 11.5F, FontPx::Sans);
                    ImGui::PushStyleColor(ImGuiCol_Text, Muted());
                    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + cardW - badgeW - 68.0F);
                    ImGui::TextWrapped("%s", perm.desc);
                    ImGui::PopTextWrapPos();
                    ImGui::PopStyleColor();
                }

                ImGui::SetCursorScreenPos({p.x + cardW - badgeW - 14.0F, p.y + 20.0F});
                Badge(badge, ctx.theme.fonts);

                ImGui::SetCursorScreenPos({p.x, p.y + cardH + 8.0F});
                ImGui::PopID();
            }
        }

        void DrawDiagnosticMetrics(const std::span<const DiagnosticMetricSpec> metrics, const EditorGuiContext& ctx)
        {
            constexpr float gap = 8.0F;
            constexpr float cardH = 68.0F;
            const float availW = ImGui::GetContentRegionAvail().x;
            const float cardW = (availW - gap * static_cast<float>(metrics.size() - 1)) / static_cast<float>(metrics.
                size());
            const ImVec2 start = ImGui::GetCursorScreenPos();
            auto* dl = ImGui::GetWindowDrawList();

            for (std::size_t i = 0; i < metrics.size(); ++i)
            {
                const ImVec2 p{start.x + static_cast<float>(i) * (cardW + gap), start.y};
                const auto& m = metrics[i];
                dl->AddRectFilled(p, {p.x + cardW, p.y + cardH}, U32(Bg3()), Layout::Radius);
                dl->AddRect(p, {p.x + cardW, p.y + cardH}, U32(Border()), Layout::Radius);

                ImGui::SetCursorScreenPos({p.x + 12.0F, p.y + 10.0F});
                {
                    ScopedTextStyle ts(ctx.theme.fonts.sansEmphasis, 9.5F, FontPx::SansEmphasis);
                    ImGui::PushStyleColor(ImGuiCol_Text, Dim());
                    ImGui::TextUnformatted(m.label);
                    ImGui::PopStyleColor();
                }

                ImGui::SetCursorScreenPos({p.x + 12.0F, p.y + 28.0F});
                {
                    ScopedTextStyle ts(ctx.theme.fonts.sansEmphasis, 15.0F, FontPx::SansEmphasis);
                    ImGui::PushStyleColor(ImGuiCol_Text, m.valueColour);
                    ImGui::TextUnformatted(m.value);
                    ImGui::PopStyleColor();
                }

                if (m.hint != nullptr && m.hint[0] != '\0')
                {
                    ImGui::SetCursorScreenPos({p.x + 12.0F, p.y + 49.0F});
                    ScopedTextStyle ts(ctx.theme.fonts.sans, 9.8F, FontPx::Sans);
                    ImGui::PushStyleColor(ImGuiCol_Text, Dim());
                    ImGui::TextUnformatted(m.hint);
                    ImGui::PopStyleColor();
                }
            }

            ImGui::SetCursorScreenPos({start.x, start.y + cardH + 12.0F});
        }

        void DrawDiagnosticActivity(const std::span<const char*const> items, const EditorGuiContext& ctx)
        {
            SettingGroup("RECENT ACTIVITY", ctx.theme.fonts);
            for (std::size_t i = 0; i < items.size(); ++i)
            {
                ImGui::PushID(static_cast<int>(i));
                const float rowW = ImGui::GetContentRegionAvail().x;
                constexpr float rowH = 30.0F;
                const ImVec2 p = ImGui::GetCursorScreenPos();
                auto* dl = ImGui::GetWindowDrawList();
                dl->AddRectFilled(p, {p.x + rowW, p.y + rowH}, U32(i % 2 == 0 ? Bg3() : Bg2()),
                                  Layout::Radius);

                ImGui::SetCursorScreenPos({p.x + 10.0F, p.y + 7.0F});
                ScopedTextStyle ts(ctx.theme.fonts.sansCompact, 10.5F, FontPx::SansCompact);
                ImGui::PushStyleColor(ImGuiCol_Text, Muted());
                ImGui::TextUnformatted(items[i]);
                ImGui::PopStyleColor();
                ImGui::SetCursorScreenPos({p.x, p.y + rowH + 4.0F});
                ImGui::PopID();
            }
        }

        void DrawManifestBlock(const char* path, const char* manifest, const EditorGuiContext& ctx)
        {
            const ImVec2 headerPos = ImGui::GetCursorScreenPos();
            const float headerW = ImGui::GetContentRegionAvail().x;

            FieldLabel("MANIFEST", ctx.theme.fonts);
            if (path != nullptr && path[0] != '\0')
            {
                ImGui::SameLine(0.0F, 8.0F);
                ScopedTextStyle ts(ctx.theme.fonts.sansCompact, 10.0F, FontPx::SansCompact);
                ImGui::PushStyleColor(ImGuiCol_Text, Dim());
                ImGui::TextUnformatted(path);
                ImGui::PopStyleColor();
            }

            ImGui::SetCursorScreenPos({headerPos.x + headerW - 58.0F, headerPos.y - 2.0F});
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2{8.0F, 3.0F});
            if (ImGui::Button("Copy", {58.0F, 24.0F}))
                ImGui::SetClipboardText(manifest);
            ImGui::PopStyleVar();
            ImGui::SetCursorScreenPos({headerPos.x, headerPos.y + 30.0F});

            ImGui::PushStyleColor(ImGuiCol_ChildBg, Bg0());
            ImGui::BeginChild("manifest-code", {0.0F, 168.0F}, true,
                              ImGuiWindowFlags_AlwaysUseWindowPadding | ImGuiWindowFlags_NoScrollbar |
                              ImGuiWindowFlags_NoScrollWithMouse);
            {
                ScopedTextStyle ts(ctx.theme.fonts.sansCompact, 11.0F, FontPx::SansCompact);
                ImGui::PushStyleColor(ImGuiCol_Text, Muted());
                ImGui::TextUnformatted(manifest);
                ImGui::PopStyleColor();
            }
            ImGui::EndChild();
            ImGui::PopStyleColor();
        }

        [[nodiscard]] bool ContainsCaseInsensitive(const char* text, const std::string& query)
        {
            if (query.empty())
                return true;
            for (const char* start = text; *start != '\0'; ++start)
            {
                const char* candidate = start;
                const char* needle = query.c_str();
                while (*candidate != '\0' && *needle != '\0' &&
                    std::tolower(static_cast<unsigned char>(*candidate)) ==
                    std::tolower(static_cast<unsigned char>(*needle)))
                {
                    ++candidate;
                    ++needle;
                }
                if (*needle == '\0')
                    return true;
            }
            return false;
        }

        // ── Plugin list (left column) ─────────────────────────────────
        void DrawPluginList(SettingsState& st, const EditorGuiContext& ctx, float /*listW*/)
        {
            SettingGroup("INSTALLED PLUGINS", ctx.theme.fonts, true);

            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2{10.0F, 7.0F});
            ImGui::PushStyleColor(ImGuiCol_FrameBg, Bg3());
            ImGui::PushStyleColor(ImGuiCol_Text, Text());
            st.pluginFilter.resize(std::min(st.pluginFilter.size(), std::size_t{63}));
            st.pluginFilter.resize(63, '\0');
            ImGui::InputTextWithHint("##filter", "Filter plugins...", st.pluginFilter.data(),
                                     st.pluginFilter.size() + 1);
            st.pluginFilter.resize(st.pluginFilter.find('\0'));
            ImGui::PopStyleColor(2);
            ImGui::PopStyleVar();
            ImGui::Dummy({0.0F, 10.0F});

            const std::string mcpDesc = ctx.localization.Get("editor", "settings.plugins.mcp.desc");
            const std::string mcpStatus = ctx.localization.Get("editor", "settings.plugins.status.trusted");
            const std::string mcpCat = ctx.localization.Get("editor", "settings.plugins.category.editor");

            const std::string fmodDesc = ctx.localization.Get("editor", "settings.plugins.fmod.desc");
            const std::string fmodStatus = ctx.localization.Get("editor", "settings.plugins.status.vendor");
            const std::string fmodCat = ctx.localization.Get("editor", "settings.plugins.category.audio");

            const std::string steamDesc = ctx.localization.Get("editor", "settings.plugins.steam.desc");
            const std::string steamStatus = ctx.localization.Get("editor", "settings.plugins.status.disabled");
            const std::string steamCat = ctx.localization.Get("editor", "settings.plugins.category.platform");

            const std::array<PluginSpec, 3> kPlugins = {
                {
                    {
                        "Horo MCP Bridge", mcpDesc.c_str(), "v0.4.0", mcpStatus.c_str(),
                        BadgeTone::Success, mcpCat.c_str(), 0,
                        &st.plugins.horoMcpBridge
                    },
                    {
                        "Vendor FMOD Integration", fmodDesc.c_str(), "v2.02.20", fmodStatus.c_str(),
                        BadgeTone::Success,
                        fmodCat.c_str(), 1,
                        &st.plugins.fmodIntegration
                    },
                    {
                        "Steamworks SDK", steamDesc.c_str(), "v1.59", steamStatus.c_str(),
                        BadgeTone::Warning, steamCat.c_str(), 2,
                        &st.plugins.steamworksSdk
                    },
                }
            };

            for (const auto& p : kPlugins)
            {
                if (!ContainsCaseInsensitive(p.name, st.pluginFilter))
                    continue;

                const bool active = (st.selectedPlugin == p.idx);
                const bool enabled = *p.enabled;
                ImGui::PushID(p.idx);

                const ImVec2 cardPos = ImGui::GetCursorScreenPos();
                const float cardW = ImGui::GetContentRegionAvail().x;
                constexpr float cardH = 96.0F;
                constexpr float padLeft = 14.0F;
                const float innerX = cardPos.x + padLeft + 24.0F;
                auto* dl = ImGui::GetWindowDrawList();

                if (active)
                {
                    dl->AddRectFilled(cardPos, {cardPos.x + cardW, cardPos.y + cardH},
                                      ImColor{Accent().x, Accent().y, Accent().z, 0.09F}, Layout::Radius);
                    dl->AddRect(cardPos, {cardPos.x + cardW, cardPos.y + cardH}, U32(BorderStrong()),
                                Layout::Radius);
                    dl->AddRectFilled(cardPos, {cardPos.x + 3.0F, cardPos.y + cardH}, U32(Accent()), 1.0F);
                }
                else if (ImGui::IsMouseHoveringRect(cardPos, {cardPos.x + cardW, cardPos.y + cardH}))
                {
                    dl->AddRectFilled(cardPos, {cardPos.x + cardW, cardPos.y + cardH}, U32(Hover()),
                                      Layout::Radius);
                    dl->AddRect(cardPos, {cardPos.x + cardW, cardPos.y + cardH}, U32(Border()), Layout::Radius);
                }
                else
                {
                    dl->AddRect(cardPos, {cardPos.x + cardW, cardPos.y + cardH}, U32(Border()), Layout::Radius);
                }

                ImGui::InvisibleButton("##card", {cardW, cardH});
                if (ImGui::IsItemClicked())
                {
                    st.selectedPlugin = p.idx;
                    st.pluginDetailTab[p.idx] = 0;
                }

                const ImVec2 dotCenter{cardPos.x + padLeft + 6.0F, cardPos.y + 18.0F};
                dl->AddCircleFilled(dotCenter, 4.5F, enabled ? U32(Ok()) : U32(Dim()));
                if (enabled)
                    dl->AddCircleFilled(dotCenter, 7.0F, ImColor{Ok().x, Ok().y, Ok().z, 0.13F});

                ImGui::SetCursorScreenPos({innerX, cardPos.y + 9.0F});
                {
                    ScopedTextStyle ts(ctx.theme.fonts.sans, 14.0F, FontPx::Sans);
                    ImGui::PushStyleColor(ImGuiCol_Text, Text());
                    ImGui::TextUnformatted(p.name);
                    ImGui::PopStyleColor();
                }

                ImGui::SetCursorScreenPos({cardPos.x + cardW - 48.0F, cardPos.y + 9.0F});
                (void)ToggleControl("##tog", p.enabled, ctx.theme.fonts, false);

                ImGui::SetCursorScreenPos({innerX, cardPos.y + 33.0F});
                {
                    ScopedTextStyle ts(ctx.theme.fonts.sans, 11.8F, FontPx::Sans);
                    ImGui::PushStyleColor(ImGuiCol_Text, Muted());
                    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + cardW - (innerX - cardPos.x) - 56.0F);
                    ImGui::TextWrapped("%s", p.desc);
                    ImGui::PopTextWrapPos();
                    ImGui::PopStyleColor();
                }

                ImGui::SetCursorScreenPos({innerX, cardPos.y + cardH - 26.0F});
                Badge(
                    {.label = p.version, .tone = BadgeTone::Accent},
                    ctx.theme.fonts);
                Badge(
                    {.label = p.statusLabel, .tone = p.statusTone},
                    ctx.theme.fonts);
                {
                    ScopedTextStyle ts(ctx.theme.fonts.sansCompact, 10.5F, FontPx::SansCompact);
                    ImGui::PushStyleColor(ImGuiCol_Text, Dim());
                    ImGui::TextUnformatted(p.category);
                    ImGui::PopStyleColor();
                }

                ImGui::SetCursorScreenPos({cardPos.x, cardPos.y + cardH + 8.0F});
                ImGui::PopID();
            }
        }

        // ── Plugin detail panel (right column) ────────────────────────
        void DrawPluginDetailPanelPrimaryAction(SettingsState& st)
        {
            switch (st.selectedPlugin)
            {
            case 0:
                st.modalFeedback = "Opening Horo MCP Bridge logs...";
                break;
            case 1:
                st.modalFeedback = "FMOD integration validated successfully.";
                break;
            case 2:
                st.plugins.steamworksSdk = true;
                break;
            default:
                break;
            }
        }

        void DrawPluginDetailPanelSecondaryAction(SettingsState& st)
        {
            switch (st.selectedPlugin)
            {
            case 0:
                st.plugins.horoMcpBridge = false;
                break;
            case 1:
                st.plugins.fmodIntegration = false;
                break;
            case 2:
                st.modalFeedback = "Opening Steamworks SDK documentation...";
                break;
            default:
                break;
            }
        }

        void DrawPluginDetailHeaderCard(SettingsState& st, const EditorGuiContext& ctx,
                                        const PluginDetailHeaderSpec& hdr)
        {
            const float headerW = ImGui::GetContentRegionAvail().x;
            constexpr float headerH = 126.0F;
            const ImVec2 p = ImGui::GetCursorScreenPos();
            auto* dl = ImGui::GetWindowDrawList();
            dl->AddRectFilled(p, {p.x + headerW, p.y + headerH}, U32(Bg3()), Layout::Radius);
            dl->AddRect(p, {p.x + headerW, p.y + headerH}, U32(Border()), Layout::Radius);

            const bool selectedEnabled = !(st.selectedPlugin == 2 && !st.plugins.steamworksSdk);
            const ImVec2 dotCenter{p.x + 18.0F, p.y + 20.0F};
            dl->AddCircleFilled(dotCenter, 4.5F, selectedEnabled ? U32(Ok()) : U32(Dim()));
            if (selectedEnabled)
                dl->AddCircleFilled(dotCenter, 7.0F, ImColor{Ok().x, Ok().y, Ok().z, 0.13F});

            ImGui::SetCursorScreenPos({p.x + 34.0F, p.y + 12.0F});
            {
                ScopedTextStyle ts(ctx.theme.fonts.sansCompact, 14.5F, FontPx::SansCompact);
                ImGui::PushStyleColor(ImGuiCol_Text, Text());
                ImGui::TextUnformatted(hdr.name);
                ImGui::PopStyleColor();
            }

            ImGui::SetCursorScreenPos({p.x + 20.0F, p.y + 40.0F});
            {
                ScopedTextStyle ts(ctx.theme.fonts.sans, 12.0F, FontPx::Sans);
                ImGui::PushStyleColor(ImGuiCol_Text, Muted());
                ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + headerW - 44.0F);
                ImGui::TextWrapped("%s", hdr.desc);
                ImGui::PopTextWrapPos();
                ImGui::PopStyleColor();
            }

            ImGui::SetCursorScreenPos({p.x + 20.0F, p.y + 94.0F});
            Badge(
                {.label = hdr.scopeBadge, .tone = BadgeTone::Accent},
                ctx.theme.fonts);
            Badge(
                {.label = hdr.signedBadge, .tone = hdr.signedTone},
                ctx.theme.fonts);
            {
                ScopedTextStyle ts(ctx.theme.fonts.sansCompact, 10.5F, FontPx::SansCompact);
                ImGui::PushStyleColor(ImGuiCol_Text, Dim());
                ImGui::TextUnformatted(hdr.restartBadge);
                ImGui::PopStyleColor();
            }

            if (headerW >= 520.0F)
            {
                constexpr float actionW = 88.0F;
                constexpr float actionGap = 6.0F;
                ImGui::SetCursorScreenPos({p.x + headerW - actionW * 2.0F - actionGap - 14.0F, p.y + headerH - 36.0F});
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2{8.0F, 4.0F});
                ImGui::PushStyleColor(ImGuiCol_Button, Bg1());
                if (ImGui::Button(hdr.action1, {actionW, 28.0F}))
                    DrawPluginDetailPanelPrimaryAction(st);
                ImGui::PopStyleColor();
                ImGui::SameLine(0.0F, actionGap);
                const bool danger = (st.selectedPlugin == 0 || st.selectedPlugin == 1);
                if (danger)
                    ImGui::PushStyleColor(ImGuiCol_Text, Err());
                if (ImGui::Button(hdr.action2, {actionW, 28.0F}))
                    DrawPluginDetailPanelSecondaryAction(st);
                if (danger)
                    ImGui::PopStyleColor();
                ImGui::PopStyleVar();
            }

            ImGui::SetCursorScreenPos({p.x, p.y + headerH + 12.0F});
        }

        void DrawPluginDetailTabs(int& activeTab, const EditorGuiContext& ctx)
        {
            static constexpr std::array kDetailTabs = {"Settings", "Permissions", "Diagnostics", "Manifest"};
            const float tabAvail = ImGui::GetContentRegionAvail().x;
            const float tabGap = 4.0F;
            const float tabW = (tabAvail - tabGap * 3.0F) / 4.0F;
            constexpr float tabH = 34.0F;
            auto* dl = ImGui::GetWindowDrawList();

            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{0.0F, 0.0F});
            for (int i = 0; i < 4; ++i)
            {
                if (i > 0)
                    ImGui::SameLine(0.0F, tabGap);
                const bool active = activeTab == i;
                ImGui::PushID(i + 200);
                ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, Layout::Radius);
                ImGui::PushStyleColor(ImGuiCol_Button,
                                      active
                                          ? ImVec4{Accent().x, Accent().y, Accent().z, 0.09F}
                                          : Bg2());
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Hover());
                ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                                      ImVec4{Accent().x, Accent().y, Accent().z, 0.16F});
                ImGui::PushStyleColor(ImGuiCol_Text, active ? Accent() : Muted());
                {
                    ScopedTextStyle ts(ctx.theme.fonts.sans, 13.5F, FontPx::Sans);
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
                    dl->AddLine({bMin.x + 8.0F, bMax.y - 2.0F}, {bMax.x - 8.0F, bMax.y - 2.0F}, U32(Accent()),
                                2.0F);
                }
            }
            ImGui::PopStyleVar();
        }

        void DrawPluginDetailContentForSelection(SettingsState& st, const EditorGuiContext& ctx, const int activeTab)
        {
            switch (st.selectedPlugin)
            {
            case 0:
                DrawMcpDetailContent(st, ctx, activeTab);
                break;
            case 1:
                DrawFmodDetailContent(st, ctx, activeTab);
                break;
            case 2:
                DrawSteamDetailContent(st, ctx, activeTab);
                break;
            default:
                break;
            }
        }

        void DrawPluginDetailPanel(SettingsState& st, const EditorGuiContext& ctx, float w, bool embedded)
        {
            if (st.selectedPlugin < 0 || st.selectedPlugin > 2)
            {
                if (!embedded)
                {
                    ImGui::PushStyleColor(ImGuiCol_ChildBg, Bg2());
                    ImGui::BeginChild("PluginDetail", {w, 0.0F}, true,
                                      ImGuiWindowFlags_AlwaysUseWindowPadding | ImGuiWindowFlags_NoScrollbar);
                }
                ImGui::PushStyleColor(ImGuiCol_Text, Dim());
                ImGui::TextUnformatted("Select a plugin from the list.");
                ImGui::PopStyleColor();
                if (!embedded)
                {
                    ImGui::EndChild();
                    ImGui::PopStyleColor();
                }
                return;
            }

            const std::string mcpDDesc = ctx.localization.Get("editor", "settings.plugins.mcp.detail_desc");
            const std::string mcpScope = ctx.localization.Get("editor", "settings.plugins.scope.editor");
            const std::string mcpSigned = ctx.localization.Get("editor", "settings.plugins.signed.signed");
            const std::string mcpRestart = ctx.localization.Get("editor", "settings.plugins.restart.not_required");
            const std::string openLogs = ctx.localization.Get("editor", "settings.plugins.action.open_logs");
            const std::string disableStr = ctx.localization.Get("editor", "settings.plugins.action.disable");

            const std::string fmodDDesc = ctx.localization.Get("editor", "settings.plugins.fmod.detail_desc");
            const std::string fmodScope = ctx.localization.Get("editor", "settings.plugins.scope.project");
            const std::string fmodSigned = ctx.localization.Get("editor", "settings.plugins.signed.vendor");
            const std::string fmodRestart = ctx.localization.Get("editor", "settings.plugins.restart.needs_sdk");
            const std::string validateStr = ctx.localization.Get("editor", "settings.plugins.action.validate");

            const std::string steamDDesc = ctx.localization.Get("editor", "settings.plugins.steam.detail_desc");
            const std::string steamScope = ctx.localization.Get("editor", "settings.plugins.scope.platform");
            const std::string steamSigned = ctx.localization.Get("editor", "settings.plugins.signed.disabled");
            const std::string steamRestart = ctx.localization.Get("editor", "settings.plugins.restart.on_enable");
            const std::string enableStr = ctx.localization.Get("editor", "settings.plugins.action.enable");
            const std::string openDocs = ctx.localization.Get("editor", "settings.plugins.action.open_docs");
            const std::array<PluginDetailHeaderSpec, 3> kDetailHeaders = {
                {
                    {
                        "Horo MCP Bridge", mcpDDesc.c_str(), mcpScope.c_str(), mcpSigned.c_str(),
                        BadgeTone::Success,
                        mcpRestart.c_str(),
                        openLogs.c_str(), disableStr.c_str()
                    },
                    {
                        "Vendor FMOD Integration", fmodDDesc.c_str(), fmodScope.c_str(), fmodSigned.c_str(),
                        BadgeTone::Success,
                        fmodRestart.c_str(), validateStr.c_str(), disableStr.c_str()
                    },
                    {
                        "Steamworks SDK", steamDDesc.c_str(), steamScope.c_str(), steamSigned.c_str(),
                        BadgeTone::Warning,
                        steamRestart.c_str(), enableStr.c_str(), openDocs.c_str()
                    },
                }
            };
            const auto& hdr = kDetailHeaders[st.selectedPlugin];
            int& activeTab = st.pluginDetailTab[st.selectedPlugin];
            if (activeTab < 0 || activeTab > 3)
                activeTab = 0;
            static int s_lastSelectedPlugin = -1;
            static int s_lastPluginTab = -1;

            if (!embedded)
            {
                ImGui::PushStyleColor(ImGuiCol_ChildBg, Bg2());
                ImGui::BeginChild("PluginDetail", {w, 0.0F}, true,
                                  ImGuiWindowFlags_AlwaysUseWindowPadding | ImGuiWindowFlags_NoScrollbar |
                                  ImGuiWindowFlags_NoScrollWithMouse);
            }

            DrawPluginDetailHeaderCard(st, ctx, hdr);
            DrawPluginDetailTabs(activeTab, ctx);

            ImGui::Dummy({0.0F, 10.0F});

            const bool contentChanged = s_lastSelectedPlugin != st.selectedPlugin || s_lastPluginTab != activeTab;
            if (contentChanged && !embedded)
                ImGui::SetScrollHereY(0.0F);

            DrawPluginDetailContentForSelection(st, ctx, activeTab);

            s_lastSelectedPlugin = st.selectedPlugin;
            s_lastPluginTab = activeTab;

            if (!embedded)
            {
                ImGui::EndChild();
                ImGui::PopStyleColor();
            }
        }

        // ── MCP Bridge detail content ─────────────────────────────────
        void DrawMcpDetailContent(SettingsState& st, const EditorGuiContext& ctx, const int activeTab)
        {
            switch (activeTab)
            {
            case 0:
                SettingGroup("CONNECTION", ctx.theme.fonts, true);
                PluginSettingRow("Transport Mode",
                                 "Use stdio for local tools; HTTP is useful for explicit local integrations.",
                                 ctx, [&st, &ctx]()
                                 {
                                     static constexpr std::array kModes = {"Local HTTP", "stdio", "Named Pipe"};
                                     (void)ComboControl("##transport", &st.mcp.transportMode, kModes.data(), 3,
                                                        ctx.theme.fonts);
                                 });
                PluginSettingRow("MCP Port", "Bound to localhost unless remote access is enabled.", ctx,
                                 [&st, &ctx]() { InputIntControl("##mcp-port", &st.mcp.port, ctx.theme.fonts); });
                PluginSettingRow("Require Session Token",
                                 "Reject tool calls unless they include the generated editor session token.", ctx,
                                 [&st, &ctx]() { DrawToggleState("##token", &st.mcp.requireToken, ctx); });
                PluginSettingRow("Allow Remote Connections", "Off by default to avoid accidental LAN exposure.", ctx,
                                 [&st, &ctx]() { DrawToggleState("##remote", &st.mcp.allowRemote, ctx); });
                SettingGroup("TOOL SCOPE", ctx.theme.fonts);
                PluginSettingRow("Allowed Tool Groups", "Restrict what external tools can invoke.", ctx, [&st, &ctx]()
                {
                    static constexpr std::array kScopes = {
                        "Read + Safe Mutations", "Read Only", "Full Project Access",
                        "Custom Policy..."
                    };
                    (void)ComboControl("##scope", &st.mcp.toolScope, kScopes.data(), 4, ctx.theme.fonts);
                });
                PluginSettingRow("Asset Write Root", "All generated assets must stay under this folder.", ctx,
                                 [&st, &ctx]()
                                 {
                                     (void)InputTextControl("##root", st.mcp.assetRoot, 64, ctx.theme.fonts);
                                 });
                break;

            case 1:
                {
                    static const std::array<PermissionRowSpec, 3> kPerms = {
                        {
                            {
                                "✓", "Read project metadata",
                                "Read project name, scene list, package graph, and editor state.", "Allowed",
                                BadgeTone::Success
                            },
                            {
                                "✓", "Write generated assets",
                                "Create files only under Assets/Generated unless policy is elevated.",
                                "Scoped", BadgeTone::Success
                            },
                            {
                                "!", "Execute build commands",
                                "Requires interactive confirmation before running build or release tasks.",
                                "Confirm", BadgeTone::Warning
                            },
                        }
                    };
                    DrawPermissionRows(kPerms, ctx);
                }
                break;

            case 2:
                {
                    static const std::array<DiagnosticMetricSpec, 3> kMetrics = {
                        {
                            {"STATUS", "Running", "sandboxed", Ok()},
                            {"LAST CALL", "2m ago", "tool request", Text()},
                            {"ERRORS", "0", "last 24h", Ok()},
                        }
                    };
                    DrawDiagnosticMetrics(kMetrics, ctx);
                    const std::array kActivity = {
                        "14:22  project.read completed in 18ms",
                        "14:20  assets.write.scoped created /Assets/Generated/mesh.json",
                        "14:16  command.run requested confirmation"
                    };
                    DrawDiagnosticActivity(kActivity, ctx);
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
                                  ctx);
                break;
            default:
                break;
            }
        }

        // ── FMOD detail content ───────────────────────────────────────
        void DrawFmodDetailContent(SettingsState& st, const EditorGuiContext& ctx, const int activeTab)
        {
            switch (activeTab)
            {
            case 0:
                SettingGroup("AUTHORING", ctx.theme.fonts, true);
                PluginSettingRow(
                    "FMOD Studio Path", "Used to open projects and compile banks from the editor.", ctx,
                    [&st, &ctx]() { (void)InputTextControl("##fmod-path", st.fmod.studioPath, 128, ctx.theme.fonts); });
                PluginSettingRow("FMOD Project File", "Relative to project root.", ctx, [&st, &ctx]()
                {
                    (void)InputTextControl("##fmod-proj", st.fmod.projectFile, 64, ctx.theme.fonts);
                });
                PluginSettingRow("Bank Output Path", "Compiled banks copied into the runtime asset tree.", ctx,
                                 [&st, &ctx]()
                                 {
                                     (void)InputTextControl("##fmod-bank", st.fmod.bankPath, 64, ctx.theme.fonts);
                                 });
                SettingGroup("RUNTIME & BUILD", ctx.theme.fonts);
                PluginSettingRow("Live Update", "Reload event metadata and banks without restarting the editor.", ctx,
                                 [&st, &ctx]() { DrawToggleState("##fmod-live", &st.fmod.liveUpdate, ctx); });
                PluginSettingRow("Fail Build On Missing Banks",
                                 "Prevents shipping builds with unresolved audio events.", ctx,
                                 [&st, &ctx]() { DrawToggleState("##fmod-fail", &st.fmod.failOnMissing, ctx); });
                PluginSettingRow("Target Platform", "Bank platform used for editor preview.", ctx, [&st, &ctx]()
                {
                    static constexpr std::array kPlatforms = {"Desktop", "Windows", "macOS", "Linux", "Console"};
                    (void)ComboControl("##fmod-plat", &st.fmod.targetPlatform, kPlatforms.data(), 5, ctx.theme.fonts);
                });
                break;

            case 1:
                {
                    static const std::array<PermissionRowSpec, 2> kPerms = {
                        {
                            {
                                "✓", "Read and write audio banks",
                                "Limited to configured FMOD project and bank output paths.", "Scoped",
                                BadgeTone::Success
                            },
                            {
                                "!", "Launch external FMOD Studio process",
                                "Requires a configured executable path and user initiated action.", "User action",
                                BadgeTone::Warning
                            },
                        }
                    };
                    DrawPermissionRows(kPerms, ctx);
                }
                break;

            case 2:
                {
                    static const std::array<DiagnosticMetricSpec, 3> kMetrics = {
                        {
                            {"BANKS", "14", "loaded", Text()},
                            {"UNRESOLVED", "2", "events", Warn()},
                            {"LIVE UPDATE", "On", "connected", Ok()},
                        }
                    };
                    DrawDiagnosticMetrics(kMetrics, ctx);
                    const std::array kActivity = {
                        "13:58  bank import finished with 2 unresolved event refs",
                        "13:44  live update connection established",
                        "13:31  Desktop bank validation completed"
                    };
                    DrawDiagnosticActivity(kActivity, ctx);
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
                                  ctx);
                break;
            default:
                break;
            }
        }

        // ── Steamworks detail content ─────────────────────────────────
        void DrawSteamDetailContent(SettingsState& st, const EditorGuiContext& ctx, const int activeTab)
        {
            switch (activeTab)
            {
            case 0:
                SettingGroup("STEAM APP", ctx.theme.fonts, true);
                PluginSettingRow("App ID", "Use 480 for local Spacewar-style testing only.", ctx, [&ctx]()
                {
                    static int steamAppId = 480;
                    InputIntControl("##steam-appid", &steamAppId, ctx.theme.fonts);
                });
                PluginSettingRow("SDK Path", "Path to the local Steamworks SDK root.", ctx, [&st, &ctx]()
                {
                    (void)InputTextControl("##steam-sdk", st.steam.sdkPath, 64, ctx.theme.fonts);
                });
                PluginSettingRow(
                    "Initialize On", "Controls when Steam API is started during editor workflows.", ctx, [&st, &ctx]()
                    {
                        static constexpr std::array kModes = {"Play Mode Only", "Editor Launch", "Build Runtime Only"};
                        (void)ComboControl("##steam-init", &st.steam.initMode, kModes.data(), 3, ctx.theme.fonts);
                    });
                SettingGroup("FEATURES", ctx.theme.fonts);
                PluginSettingRow("Overlay", "Enable Steam overlay while testing from Play Mode.", ctx,
                                 [&st, &ctx]() { DrawToggleState("##steam-overlay", &st.steam.overlay, ctx); });
                PluginSettingRow("Achievements", "Expose achievement authoring and validation panels.", ctx,
                                 [&st, &ctx]() { DrawToggleState("##steam-ach", &st.steam.achievements, ctx); });
                PluginSettingRow("Networking Sockets", "Enable Steam networking transport for multiplayer preview.",
                                 ctx,
                                 [&st, &ctx]() { DrawToggleState("##steam-net", &st.steam.networking, ctx); });
                break;

            case 1:
                {
                    static const std::array<PermissionRowSpec, 2> kPerms = {
                        {
                            {
                                "✓", "Read platform config",
                                "Reads App ID, achievements config, and build target metadata.", "Allowed",
                                BadgeTone::Success
                            },
                            {
                                "!", "Network access", "Only enabled when Steam networking transport is selected.",
                                "Conditional",
                                BadgeTone::Warning
                            },
                        }
                    };
                    DrawPermissionRows(kPerms, ctx);
                }
                break;

            case 2:
                {
                    static const std::array<DiagnosticMetricSpec, 3> kMetrics = {
                        {
                            {"STATUS", "Disabled", "not loaded", Dim()},
                            {"SDK", "Missing", "path required", Warn()},
                            {"OVERLAY", "Ready", "waiting", Ok()},
                        }
                    };
                    DrawDiagnosticMetrics(kMetrics, ctx);
                    const std::array kActivity = {
                        "12:45  skipped init because Steamworks SDK is disabled",
                        "12:44  overlay check passed", "12:42  missing SDK path warning emitted"
                    };
                    DrawDiagnosticActivity(kActivity, ctx);
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
                                  ctx);
                break;
            default:
                break;
            }
        }

        // ── Installed Plugins (split pane) ────────────────────────────
        void DrawInstalledPlugins(SettingsState& st, const EditorGuiContext& ctx)
        {
            {
                ScopedTextStyle ts(ctx.theme.fonts.sans, 12.5F, FontPx::Sans);
                ImGui::PushStyleColor(ImGuiCol_Text, Muted());
                ImGui::TextWrapped(
                    "Select a plugin to edit its settings, permissions, diagnostics, and manifest details. The selected plugin "
                    "appears below in the same workspace so the layout stays simpler and easier to scan.");
                ImGui::PopStyleColor();
            }
            ImGui::Dummy({0.0F, 10.0F});

            DrawPluginList(st, ctx, ImGui::GetContentRegionAvail().x);

            ImGui::Dummy({0.0F, 10.0F});
            const ImVec2 sep = ImGui::GetCursorScreenPos();
            ImGui::GetWindowDrawList()->AddLine({sep.x, sep.y}, {sep.x + ImGui::GetContentRegionAvail().x, sep.y},
                                                U32(Border()), 1.0F);
            ImGui::Dummy({0.0F, 14.0F});

            DrawPluginDetailPanel(st, ctx, ImGui::GetContentRegionAvail().x, true);
        }

        // ── Runtime & Discovery ───────────────────────────────────────
        void DrawRuntimeDiscovery(SettingsState& st, const EditorGuiContext& ctx)
        {
            SettingGroup("RUNTIME OVERVIEW", ctx.theme.fonts, true);

            static const std::array<DiagnosticMetricSpec, 3> kRuntimeCards = {
                {
                    {"ISOLATION", "Sandboxed", "processes", Text()},
                    {"DISCOVERY", "Project + editor", "paths", Text()},
                    {"UPDATES", "Signed only", "registries", Ok()},
                }
            };
            DrawDiagnosticMetrics(kRuntimeCards, ctx);

            SettingGroup("DISCOVERY", ctx.theme.fonts);
            PluginSettingRow(
                "Plugin Discovery Paths",
                "Semicolon-separated paths. Project plugins override editor plugins only when trusted.", ctx,
                [&st, &ctx]()
                {
                    (void)InputTextControl("##disc-path", st.runtime.discoveryPaths, 128, ctx.theme.fonts);
                });
            PluginSettingRow(
                "Load Order Policy", "Defines how editor, project, vendor, and local-development plugins are resolved.",
                ctx,
                [&st, &ctx]()
                {
                    static constexpr std::array kOrders = {
                        "Project overrides editor if trusted", "Editor plugins first",
                        "Project plugins first", "Locked by project manifest"
                    };
                    (void)ComboControl("##order", &st.runtime.loadOrder, kOrders.data(), 4, ctx.theme.fonts);
                });
            PluginSettingRow("Development Plugin Path",
                             "Optional local path used for plugin authorship and hot-reload testing.", ctx,
                             [&st, &ctx]()
                             {
                                 (void)InputTextControl("##dev-path", st.runtime.devPath, 64, ctx.theme.fonts);
                             });

            SettingGroup("SECURITY & ISOLATION", ctx.theme.fonts);
            PluginSettingRow("Sandbox Plugin Processes",
                             "Run native/plugin processes with limited filesystem and network permissions.", ctx,
                             [&st, &ctx]() { DrawToggleState("##sandbox", &st.runtime.sandbox, ctx); });
            PluginSettingRow("Unsigned Plugin Policy",
                             "Controls what happens when a plugin is not signed by a trusted vendor or local workspace.",
                             ctx,
                             [&st, &ctx]()
                             {
                                 static constexpr std::array kPolicies = {
                                     "Block by default", "Allow after warning",
                                     "Allow local development only"
                                 };
                                 (void)ComboControl("##unsigned", &st.runtime.unsignedPolicy, kPolicies.data(), 3,
                                                    ctx.theme.fonts);
                             });
            PluginSettingRow("Network Access Policy",
                             "Default network behavior for plugins unless a plugin-specific permission overrides it.",
                             ctx,
                             [&st, &ctx]()
                             {
                                 static constexpr std::array kNets = {
                                     "Deny by default", "Localhost only", "Prompt per plugin",
                                     "Allow trusted plugins"
                                 };
                                 (void)ComboControl("##net", &st.runtime.networkPolicy, kNets.data(), 4,
                                                    ctx.theme.fonts);
                             });

            SettingGroup("UPDATES & COMPATIBILITY", ctx.theme.fonts);
            PluginSettingRow("Auto-check Plugin Updates",
                             "Checks signed registries only; local plugins are never updated automatically.", ctx,
                             [&st, &ctx]()
                             {
                                 static constexpr std::array kChecks = {"Weekly", "Daily", "Manual Only"};
                                 (void)ComboControl("##update", &st.runtime.updateCheck, kChecks.data(), 3,
                                                    ctx.theme.fonts);
                             });
            PluginSettingRow("Compatibility Mode",
                             "How strictly plugin API versions are validated when opening a project.",
                             ctx, [&st, &ctx]()
                             {
                                 static constexpr std::array kModes = {
                                     "Strict semantic versioning", "Allow compatible minors",
                                     "Prompt on mismatch"
                                 };
                                 (void)ComboControl("##compat", &st.runtime.compatMode, kModes.data(), 3,
                                                    ctx.theme.fonts);
                             });

            ImGui::Dummy({0.0F, 4.0F});
            {
                const float noteW = ImGui::GetContentRegionAvail().x;
                const ImVec2 p = ImGui::GetCursorScreenPos();
                constexpr float noteH = 54.0F;
                auto* dl = ImGui::GetWindowDrawList();
                dl->AddRectFilled(p, {p.x + noteW, p.y + noteH},
                                  ImColor{Accent().x, Accent().y, Accent().z, 0.06F}, Layout::Radius);
                dl->AddRect(p, {p.x + noteW, p.y + noteH}, U32(Border()), Layout::Radius);

                ImGui::SetCursorScreenPos({p.x + 12.0F, p.y + 10.0F});
                ScopedTextStyle ts(ctx.theme.fonts.sans, 11.5F, FontPx::Sans);
                ImGui::PushStyleColor(ImGuiCol_Text, Muted());
                ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + noteW - 26.0F);
                ImGui::TextWrapped(
                    "Runtime settings are editor-wide defaults. Individual plugin settings live inside each plugin detail "
                    "panel and can override these defaults only when the permission model allows it.");
                ImGui::PopTextWrapPos();
                ImGui::PopStyleColor();
                ImGui::SetCursorScreenPos({p.x, p.y + noteH + 4.0F});
            }
        }

        [[nodiscard]] const Extensions::ExtensionInventoryEntry* FindSelectedExtension(
            const SettingsState& st)
        {
            if (st.extensionInventory == nullptr)
                return nullptr;
            const auto& entries = st.extensionInventory->Entries();
            const auto selected =
                std::ranges::find(entries, st.selectedExtensionId,
                                  &Extensions::ExtensionInventoryEntry::packageId);
            return selected != entries.end() ? &*selected : nullptr;
        }

        /** @brief Draws a detail section heading and its inter-section divider. */
        void DrawExtensionDetailSection(
            const char* label, const EditorGuiContext& ctx, const bool first)
        {
            if (!first)
            {
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2.0F);
                const ImVec2 divider = ImGui::GetCursorScreenPos();
                ImGui::GetWindowDrawList()->AddLine(
                    divider,
                    {divider.x + ImGui::GetContentRegionAvail().x, divider.y},
                    U32(Border()), 1.0F);
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.0F);
            }

            ScopedTextStyle heading(
                ctx.theme.fonts.sansEmphasis, 16.0F, FontPx::SansEmphasis);
            ImGui::PushStyleColor(ImGuiCol_Text, Text());
            ImGui::TextUnformatted(label);
            ImGui::PopStyleColor();
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4.0F);
        }

        /** @brief Draws one compact extension detail label/value pair without a divider. */
        void DrawExtensionDetailRow(
            const char* label, const char* value, const EditorGuiContext& ctx)
        {
            const float availableWidth = ImGui::GetContentRegionAvail().x;
            const ImVec2 itemSpacing = ImGui::GetStyle().ItemSpacing;
            ImGui::PushStyleVar(
                ImGuiStyleVar_ItemSpacing, ImVec2{itemSpacing.x, 3.0F});
            {
                ScopedTextStyle title(
                    ctx.theme.fonts.sans, 15.0F, FontPx::Sans);
                ImGui::PushStyleColor(ImGuiCol_Text, Text());
                ImGui::TextUnformatted(label);
                ImGui::PopStyleColor();
            }
            if (value != nullptr && value[0] != '\0')
            {
                ScopedTextStyle detail(
                    ctx.theme.fonts.sans, 13.5F, FontPx::Sans);
                ImGui::PushStyleColor(ImGuiCol_Text, Muted());
                ImGui::PushTextWrapPos(
                    ImGui::GetCursorPosX() + availableWidth);
                ImGui::TextWrapped("%s", value);
                ImGui::PopTextWrapPos();
                ImGui::PopStyleColor();
            }
            ImGui::PopStyleVar();
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 5.0F);
        }

        /** @brief Draws a standalone value inside the current extension detail section. */
        void DrawExtensionDetailValue(
            const char* value, const EditorGuiContext& ctx)
        {
            ScopedTextStyle detail(
                ctx.theme.fonts.sans, 13.5F, FontPx::Sans);
            ImGui::PushStyleColor(ImGuiCol_Text, Muted());
            ImGui::TextWrapped("%s", value);
            ImGui::PopStyleColor();
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4.0F);
        }

        void DrawExtensionDetails(SettingsState& st, const EditorGuiContext& ctx)
        {
            const Extensions::ExtensionInventoryEntry* entry = FindSelectedExtension(st);
            if (entry == nullptr)
            {
                ImGui::PushStyleColor(ImGuiCol_Text, Muted());
                ImGui::TextWrapped("%s",
                    ctx.localization.Get("editor", "settings.extensions.select_prompt").c_str());
                ImGui::PopStyleColor();
                return;
            }

            if (!entry->loadError.empty())
            {
                ImGui::PushStyleColor(ImGuiCol_Text, Err());
                ImGui::TextWrapped("%s", entry->loadError.c_str());
                ImGui::PopStyleColor();
                ImGui::Dummy({0.0F, 12.0F});
            }

            DrawExtensionDetailSection(
                ctx.localization.Get("editor", "settings.extensions.modules").c_str(),
                ctx, true);
            if (entry->modules.empty())
            {
                DrawExtensionDetailValue(
                    ctx.localization.Get(
                        "editor", "settings.extensions.none").c_str(),
                    ctx);
            }
            for (const auto& module : entry->modules)
            {
                const std::string moduleDescription =
                    module.kind + "  ·  v" + module.version;
                DrawExtensionDetailRow(
                    module.id.c_str(), moduleDescription.c_str(), ctx);
            }

            DrawExtensionDetailSection(
                ctx.localization.Get("editor", "settings.extensions.contributions").c_str(),
                ctx, false);
            if (entry->contributions.empty())
            {
                DrawExtensionDetailValue(
                    ctx.localization.Get(
                        "editor", "settings.extensions.none").c_str(),
                    ctx);
            }
            for (const auto& contribution : entry->contributions)
            {
                DrawExtensionDetailRow(
                    contribution.type.c_str(), contribution.id.c_str(), ctx);
            }

            DrawExtensionDetailSection(
                ctx.localization.Get("editor", "settings.extensions.tab.manifest").c_str(),
                ctx, false);
            if (entry->absoluteManifestPath.empty())
            {
                DrawExtensionDetailValue(
                    ctx.localization.Get(
                        "editor", "settings.extensions.builtin_manifest").c_str(),
                    ctx);
            }
            else
            {
                DrawExtensionDetailValue(
                    entry->absoluteManifestPath.string().c_str(), ctx);
            }
        }

        void DrawExtensionCards(SettingsState& st, const EditorGuiContext& ctx)
        {
            Extensions::ExtensionInventory& inventory = *st.extensionInventory;
            const auto& entries = inventory.Entries();
            if (!entries.empty() &&
                std::ranges::find(entries, st.selectedExtensionId,
                                  &Extensions::ExtensionInventoryEntry::packageId) == entries.end())
                st.selectedExtensionId = entries.front().packageId;

            const float availableWidth = ImGui::GetContentRegionAvail().x;
            constexpr float paneGap = 14.0F;
            constexpr float minimumDetailWidth = 360.0F;
            const float maximumListWidth = std::max(
                250.0F, availableWidth - paneGap - minimumDetailWidth);
            const float listWidth = std::clamp(
                availableWidth * 0.42F, 250.0F,
                std::min(640.0F, maximumListWidth));
            const float paneHeight = std::max(0.0F, ImGui::GetContentRegionAvail().y);

            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{14.0F, 14.0F});
            ImGui::PushStyleColor(ImGuiCol_ChildBg, Bg2());
            ImGui::BeginChild("ExtensionListPane", {listWidth, paneHeight}, true,
                              ImGuiWindowFlags_AlwaysUseWindowPadding);
            {
                ScopedTextStyle heading(
                    ctx.theme.fonts.sansEmphasis, 11.5F, FontPx::SansEmphasis);
                ImGui::PushStyleColor(ImGuiCol_Text, Dim());
                ImGui::TextUnformatted(
                    ctx.localization.Get(
                        "editor", "settings.extensions.installed_heading").c_str());
                ImGui::PopStyleColor();
            }
            const std::string totalLabel =
                std::to_string(entries.size()) + " " +
                ctx.localization.Get("editor", "settings.extensions.total");
            ImGui::SameLine();
            ImGui::SetCursorPosX(
                ImGui::GetWindowContentRegionMax().x - ImGui::CalcTextSize(totalLabel.c_str()).x);
            ImGui::PushStyleColor(ImGuiCol_Text, Muted());
            ImGui::TextUnformatted(totalLabel.c_str());
            ImGui::PopStyleColor();
            ImGui::Dummy({0.0F, 8.0F});

            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2{10.0F, 7.0F});
            st.pluginFilter.resize(std::min(st.pluginFilter.size(), std::size_t{127}));
            st.pluginFilter.resize(127, '\0');
            const std::string filterHint =
                ctx.localization.Get("editor", "settings.extensions.filter");
            ImGui::SetNextItemWidth(-1.0F);
            ImGui::InputTextWithHint("##extension-filter", filterHint.c_str(),
                                     st.pluginFilter.data(), st.pluginFilter.size() + 1);
            st.pluginFilter.resize(st.pluginFilter.find('\0'));
            ImGui::PopStyleVar();
            ImGui::Dummy({0.0F, 12.0F});

            bool drewAny = false;
            for (const auto& entry : entries)
            {
                if (!ContainsCaseInsensitive(entry.displayName.c_str(), st.pluginFilter) &&
                    !ContainsCaseInsensitive(entry.packageId.c_str(), st.pluginFilter))
                    continue;
                drewAny = true;
                ImGui::PushID(entry.packageId.c_str());
                const float cardWidth = ImGui::GetContentRegionAvail().x;
                constexpr float cardHeight = 136.0F;
                const ImVec2 cardMin = ImGui::GetCursorScreenPos();
                const ImVec2 cardMax{cardMin.x + cardWidth, cardMin.y + cardHeight};
                const bool selected = st.selectedExtensionId == entry.packageId;
                const bool restartRequired = entry.RestartRequired();
                auto* drawList = ImGui::GetWindowDrawList();
                drawList->AddRectFilled(
                    cardMin, cardMax,
                    U32(selected ? ImVec4{Accent().x, Accent().y, Accent().z, 0.09F} : Bg1()),
                    Layout::Radius);
                drawList->AddRect(cardMin, cardMax,
                                  U32(selected ? ImVec4{Accent().x, Accent().y, Accent().z, 0.50F}
                                               : Border()),
                                  Layout::Radius);
                if (selected)
                {
                    drawList->AddRectFilled(
                        cardMin, {cardMin.x + 3.0F, cardMax.y}, U32(Accent()),
                        Layout::Radius);
                }
                ImGui::InvisibleButton("card", {cardWidth, cardHeight});
                if (ImGui::IsItemClicked())
                {
                    st.selectedExtensionId = entry.packageId;
                }

                const ImVec2 dotCenter{cardMin.x + 18.0F, cardMin.y + 23.0F};
                drawList->AddCircleFilled(
                    dotCenter, 8.0F,
                    U32(entry.runtimeActive
                            ? ImVec4{Ok().x, Ok().y, Ok().z, 0.14F}
                            : ImVec4{Dim().x, Dim().y, Dim().z, 0.12F}));
                drawList->AddCircleFilled(
                    dotCenter, 4.5F, U32(entry.runtimeActive ? Ok() : Dim()));

                const std::string toggleLabel = ctx.localization.Get(
                    "editor", entry.enabled ? "settings.plugins.status.enabled"
                                             : "settings.plugins.status.disabled");
                float toggleLabelWidth = 0.0F;
                {
                    ScopedTextStyle toggleText(
                        ctx.theme.fonts.sans, 12.5F, FontPx::Sans);
                    toggleLabelWidth = ImGui::CalcTextSize(toggleLabel.c_str()).x;
                }
                const float toggleClusterWidth = 36.0F + 8.0F + toggleLabelWidth;
                ImGui::SetCursorScreenPos({cardMin.x + 34.0F, cardMin.y + 13.0F});
                {
                    ScopedTextStyle title(
                        ctx.theme.fonts.sansEmphasis, 14.5F, FontPx::SansEmphasis);
                    ImGui::PushClipRect(
                        {cardMin.x + 34.0F, cardMin.y},
                        {cardMax.x - toggleClusterWidth - 28.0F, cardMin.y + 42.0F},
                        true);
                    ImGui::TextUnformatted(entry.displayName.c_str());
                    ImGui::PopClipRect();
                }

                ImGui::SetCursorScreenPos({cardMin.x + 16.0F, cardMin.y + 45.0F});
                ImGui::PushStyleColor(ImGuiCol_Text, Muted());
                ImGui::PushClipRect(
                    {cardMin.x + 16.0F, cardMin.y + 43.0F},
                    {cardMax.x - 16.0F, cardMax.y - 42.0F}, true);
                ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + cardWidth - 32.0F);
                ImGui::TextWrapped("%s", entry.description.empty()
                                             ? entry.packageId.c_str()
                                             : entry.description.c_str());
                ImGui::PopTextWrapPos();
                ImGui::PopClipRect();
                ImGui::PopStyleColor();

                ImGui::SetCursorScreenPos({cardMin.x + 16.0F, cardMax.y - 34.0F});
                ImGui::PushClipRect(
                    {cardMin.x + 14.0F, cardMax.y - 38.0F},
                    {cardMax.x - 14.0F, cardMax.y - 4.0F}, true);
                const char* originKey =
                    entry.origin == Extensions::ExtensionOrigin::BuiltIn
                        ? "settings.extensions.origin.builtin"
                        : "settings.extensions.origin.user";
                Badge(
                    {.label = ("v" + entry.version).c_str(),
                     .tone = BadgeTone::Accent},
                    ctx.theme.fonts);
                Badge(
                    {.label =
                         ctx.localization.Get("editor", originKey).c_str(),
                     .tone = BadgeTone::Neutral},
                    ctx.theme.fonts);
                if (restartRequired)
                    Badge(
                        {.label = ctx.localization.Get(
                             "editor",
                             "settings.extensions.restart_required").c_str(),
                         .tone = BadgeTone::Warning},
                        ctx.theme.fonts);
                ImGui::PopClipRect();

                bool enabled = entry.enabled;
                ImGui::SetCursorScreenPos(
                    {cardMax.x - toggleClusterWidth - 16.0F, cardMin.y + 12.0F});
                if (ToggleControl(
                        "##extension-enabled", &enabled, ctx.theme.fonts, false))
                {
                    if (Result<void> changed = inventory.SetEnabled(entry.packageId, enabled);
                        changed.HasError())
                    {
                        st.modalFeedback = changed.ErrorValue().message;
                    }
                    else
                    {
                        st.modalFeedback = ctx.localization.Get(
                            "editor", "settings.extensions.feedback.restart");
                    }
                }
                ImGui::SameLine(0.0F, 8.0F);
                {
                    ScopedTextStyle toggleText(
                        ctx.theme.fonts.sans, 12.5F, FontPx::Sans);
                    ImGui::PushStyleColor(ImGuiCol_Text, enabled ? Text() : Muted());
                    ImGui::TextUnformatted(
                        ctx.localization.Get(
                            "editor", enabled ? "settings.plugins.status.enabled"
                                              : "settings.plugins.status.disabled").c_str());
                    ImGui::PopStyleColor();
                }
                ImGui::SetCursorScreenPos({cardMin.x, cardMax.y + 8.0F});
                ImGui::PopID();
            }
            if (!drewAny)
            {
                ImGui::PushStyleColor(ImGuiCol_Text, Muted());
                ImGui::TextUnformatted(
                    ctx.localization.Get("editor", "settings.extensions.empty").c_str());
                ImGui::PopStyleColor();
            }

            ImGui::EndChild();
            ImGui::PopStyleColor();
            ImGui::PopStyleVar();

            ImGui::SameLine(0.0F, paneGap);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{8.0F, 0.0F});
            ImGui::BeginChild("ExtensionDetailPane", {0.0F, paneHeight}, false,
                              ImGuiWindowFlags_AlwaysUseWindowPadding);
            DrawExtensionDetails(st, ctx);
            ImGui::EndChild();
            ImGui::PopStyleVar();
        }

        /** @brief Draws searchable registry results and their install actions. */
        void DrawExtensionMarketplace(SettingsState& st, const EditorGuiContext& ctx)
        {
            if (st.extensionMarketplace == nullptr)
            {
                ImGui::PushStyleColor(ImGuiCol_Text, Err());
                ImGui::TextWrapped("%s",
                    ctx.localization.Get(
                        "editor", "settings.extensions.marketplace.unavailable").c_str());
                ImGui::PopStyleColor();
                return;
            }

            st.extensionMarketplace->Update();
            Extensions::ExtensionMarketplaceSnapshot marketplace =
                st.extensionMarketplace->Snapshot();
            if (marketplace.status == Extensions::ExtensionMarketplaceStatus::Idle)
            {
                static_cast<void>(
                    st.extensionMarketplace->Search(st.marketplaceQuery));
                marketplace = st.extensionMarketplace->Snapshot();
            }

            constexpr float searchButtonWidth = 96.0F;
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2{10.0F, 7.0F});
            ImGui::SetNextItemWidth(
                std::max(120.0F, ImGui::GetContentRegionAvail().x -
                                     searchButtonWidth - 8.0F));
            st.marketplaceQuery.resize(
                std::min(st.marketplaceQuery.size(), std::size_t{127}));
            st.marketplaceQuery.resize(127, '\0');
            const std::string searchHint = ctx.localization.Get(
                "editor", "settings.extensions.marketplace.search");
            const bool submitted = ImGui::InputTextWithHint(
                "##marketplace-search", searchHint.c_str(),
                st.marketplaceQuery.data(), st.marketplaceQuery.size() + 1,
                ImGuiInputTextFlags_EnterReturnsTrue);
            st.marketplaceQuery.resize(st.marketplaceQuery.find('\0'));
            ImGui::SameLine(0.0F, 8.0F);
            const bool busy =
                marketplace.status == Extensions::ExtensionMarketplaceStatus::Searching ||
                marketplace.status == Extensions::ExtensionMarketplaceStatus::Installing;
            ImGui::BeginDisabled(busy);
            const bool searchClicked = ImGui::Button(
                ctx.localization.Get(
                    "editor", "settings.extensions.marketplace.search.action").c_str(),
                {searchButtonWidth, 0.0F});
            ImGui::EndDisabled();
            ImGui::PopStyleVar();
            if ((submitted || searchClicked) && !busy)
                static_cast<void>(
                    st.extensionMarketplace->Search(st.marketplaceQuery));

            ImGui::Dummy({0.0F, 12.0F});
            marketplace = st.extensionMarketplace->Snapshot();
            if (marketplace.status == Extensions::ExtensionMarketplaceStatus::Searching)
            {
                ImGui::PushStyleColor(ImGuiCol_Text, Muted());
                ImGui::TextUnformatted(
                    ctx.localization.Get(
                        "editor", "settings.extensions.marketplace.searching").c_str());
                ImGui::PopStyleColor();
                return;
            }
            if (!marketplace.message.empty())
            {
                ImGui::PushStyleColor(
                    ImGuiCol_Text,
                    marketplace.status == Extensions::ExtensionMarketplaceStatus::Error
                        ? Err()
                        : Ok());
                ImGui::TextWrapped("%s", marketplace.message.c_str());
                ImGui::PopStyleColor();
                ImGui::Dummy({0.0F, 8.0F});
            }
            if (marketplace.entries.empty() &&
                marketplace.status != Extensions::ExtensionMarketplaceStatus::Error)
            {
                ImGui::PushStyleColor(ImGuiCol_Text, Muted());
                ImGui::TextUnformatted(
                    ctx.localization.Get(
                        "editor", "settings.extensions.marketplace.empty").c_str());
                ImGui::PopStyleColor();
                return;
            }

            ImGui::BeginChild(
                "ExtensionMarketplaceResults", {0.0F, 0.0F}, false,
                ImGuiWindowFlags_AlwaysUseWindowPadding);
            for (const auto& entry : marketplace.entries)
            {
                ImGui::PushID(entry.packageId.c_str());
                const ImVec2 cardMin = ImGui::GetCursorScreenPos();
                const float cardWidth = ImGui::GetContentRegionAvail().x;
                constexpr float cardHeight = 104.0F;
                const ImVec2 cardMax{
                    cardMin.x + cardWidth, cardMin.y + cardHeight};
                auto* drawList = ImGui::GetWindowDrawList();
                drawList->AddRectFilled(
                    cardMin, cardMax, U32(Bg2()), Layout::Radius);
                drawList->AddRect(
                    cardMin, cardMax, U32(Border()), Layout::Radius);

                ImGui::SetCursorScreenPos(
                    {cardMin.x + 16.0F, cardMin.y + 13.0F});
                {
                    ScopedTextStyle title(
                        ctx.theme.fonts.sansEmphasis, 15.0F,
                        FontPx::SansEmphasis);
                    ImGui::TextUnformatted(entry.displayName.c_str());
                }
                ImGui::SameLine(0.0F, 8.0F);
                Badge(
                    {.label = ("v" + entry.version).c_str(),
                     .tone = BadgeTone::Accent},
                    ctx.theme.fonts);
                ImGui::SetCursorScreenPos(
                    {cardMin.x + 16.0F, cardMin.y + 42.0F});
                ImGui::PushStyleColor(ImGuiCol_Text, Muted());
                ImGui::PushTextWrapPos(
                    ImGui::GetCursorPosX() + cardWidth - 154.0F);
                ImGui::TextWrapped("%s", entry.description.c_str());
                ImGui::PopTextWrapPos();
                ImGui::PopStyleColor();
                ImGui::SetCursorScreenPos(
                    {cardMin.x + 16.0F, cardMax.y - 25.0F});
                ImGui::PushStyleColor(ImGuiCol_Text, Dim());
                ImGui::TextUnformatted(entry.author.c_str());
                ImGui::PopStyleColor();

                const bool installed = std::ranges::any_of(
                    st.extensionInventory->Entries(), [&entry](const auto& item)
                    {
                        return item.packageId == entry.packageId;
                    });
                const bool installing =
                    marketplace.status ==
                        Extensions::ExtensionMarketplaceStatus::Installing &&
                    marketplace.activePackageId == entry.packageId;
                ImGui::SetCursorScreenPos(
                    {cardMax.x - 116.0F, cardMin.y + 35.0F});
                ImGui::BeginDisabled(installed || busy);
                const std::string action = ctx.localization.Get(
                    "editor", installed
                                  ? "settings.extensions.marketplace.installed"
                                  : installing
                                  ? "settings.extensions.marketplace.installing"
                                  : "settings.plugins.action.install");
                if (ImGui::Button(action.c_str(), {100.0F, 32.0F}))
                {
                    const Result<void> started =
                        st.extensionMarketplace->Install(entry.packageId);
                    if (started.HasError())
                        st.modalFeedback = started.ErrorValue().message;
                }
                ImGui::EndDisabled();
                ImGui::SetCursorScreenPos(
                    {cardMin.x, cardMax.y + 8.0F});
                ImGui::PopID();
            }
            ImGui::EndChild();
        }

        void DrawExtensionManager(SettingsState& st, const EditorGuiContext& ctx)
        {
            if (st.extensionInventory == nullptr)
            {
                ImGui::PushStyleColor(ImGuiCol_Text, Err());
                ImGui::TextWrapped("%s",
                    ctx.localization.Get("editor", "settings.extensions.unavailable").c_str());
                ImGui::PopStyleColor();
                return;
            }

            DrawPluginSectionTabs(st, ctx);
            if (st.pluginSectionTab == 0)
                DrawExtensionCards(st, ctx);
            else
                DrawExtensionMarketplace(st, ctx);
        }

        void DrawContent(SettingsState& st, const EditorGuiContext& ctx)
        {
            switch (static_cast<SettingsTab>(st.activeTab))
            {
                using enum SettingsTab;
            case General:
                DrawGeneral(st, ctx);
                break;
            case Appearance:
                DrawAppearance(st, ctx);
                break;
            case Input:
                DrawInput(st, ctx);
                break;
            case Rendering:
                DrawRendering(st, ctx);
                break;
            case Audio:
                DrawAudio(st, ctx);
                break;
            case Network:
                DrawNetwork(st, ctx);
                break;
            case Diagnostics:
                DrawDiagnostics(st, ctx);
                break;
            case Plugins:
                DrawPlugins(st, ctx);
                break;
            default:
                break;
            }
        }

        [[nodiscard]] bool DrawFooterContent(
            SettingsState& st, EditorSettingsService& settings,
            const EditorGuiContext& ctx)
        {
            constexpr float actionH = 32.0F;
            constexpr float footerPaddingX = 22.0F;
            const float centeredActionY =
                (ImGui::GetWindowHeight() - actionH) * 0.5F;
            ImGui::SetCursorPos(
                {footerPaddingX,
                 (ImGui::GetWindowHeight() -
                  ImGui::GetTextLineHeight()) *
                     0.5F});
            if (st.dirty)
            {
                ScopedTextStyle badge(ctx.theme.fonts.sansCompact, 10.5F, FontPx::SansCompact);
                ImGui::PushStyleColor(ImGuiCol_Text, Warn());
                ImGui::TextUnformatted("unsaved");
                ImGui::PopStyleColor();
                ImGui::SameLine(0.0F, 8.0F);
            }
            {
                ScopedTextStyle hint(ctx.theme.fonts.sansCompact, 11.5F, FontPx::SansCompact);
                const bool hasFeedback = !st.modalFeedback.empty();
                ImGui::PushStyleColor(
                    ImGuiCol_Text,
                    hasFeedback ? Accent()
                                : (st.statusIsError ? Err() : Dim()));
                ImGui::TextUnformatted(
                    hasFeedback
                        ? st.modalFeedback.c_str()
                        : (st.statusMessage.empty()
                               ? "Apply writes user preferences to ~/.horo/editor_settings.json"
                               : st.statusMessage.c_str()));
                ImGui::PopStyleColor();
            }

            constexpr float restoreW = 124.0F;
            constexpr float cancelW = 78.0F;
            constexpr float applyW = 70.0F;
            constexpr float gap = 8.0F;
            const float actionsW = restoreW + cancelW + applyW + gap * 2.0F;
            ImGui::SetCursorPos(
                {ImGui::GetWindowWidth() - footerPaddingX - actionsW,
                 centeredActionY});
            bool requestClose = false;
            if (const std::string restoreDefaults = ctx.localization.Get("editor", "settings.restore_defaults");
                Button({
                    .label = restoreDefaults.c_str(),
                    .size = {restoreW, actionH},
                    .variant = ButtonVariant::Secondary,
                    .fontSize = 13.0F,
                    .font = ctx.theme.fonts.sansCompact,
                    .baseFontSize = FontPx::SansCompact
                }))
            {
                LOG_INFO("editor.settings", "Restore Defaults clicked — draft reset to factory defaults.");
                ApplySettingsToDraft(st, DefaultEditorSettings());
                st.statusMessage = "Defaults loaded into draft. Apply to persist.";
                st.statusIsError = false;
            }
            ImGui::SameLine(0.0F, gap);
            const std::string cancelLabel = ctx.localization.Get("editor", "settings.cancel") + "###settings_cancel";
            if (Button({
                .label = cancelLabel.c_str(),
                .size = {cancelW, actionH},
                .variant = ButtonVariant::Secondary,
                .fontSize = 13.0F,
                .font = ctx.theme.fonts.sansCompact,
                .baseFontSize = FontPx::SansCompact
            }))
            {
                LOG_INFO("editor.settings", "Settings cancelled by user (dirty=%s).", st.dirty ? "yes" : "no");
                requestClose = true;
            }
            ImGui::SameLine(0.0F, gap);
            const std::string applyLabel = ctx.localization.Get("editor", "settings.apply") + "###settings_apply";
            if (Button({
                .label = applyLabel.c_str(),
                .size = {applyW, actionH},
                .variant = ButtonVariant::Primary,
                .fontSize = 13.0F,
                .font = ctx.theme.fonts.sansCompact,
                .baseFontSize = FontPx::SansCompact
            }))
            {
                (void)ApplySettings(st, settings);
                LOG_INFO("editor.settings", "Settings applied via Apply button.");
            }

            return requestClose;
        }

        [[nodiscard]] ModalFrameResult DrawSettingsModalPresentationImpl(
            SettingsState& st, EditorSettingsService& settings,
            const EditorGuiContext& ctx, const ImTextureID logo)
        {
            if (st.appearance.pendingThemeIndex >= 0)
            {
                SelectThemeByIndex(st.appearance.pendingThemeIndex);
                st.appearance.pendingThemeIndex = -1;
            }

            st.modalFeedback.clear();

            const std::string title =
                ctx.localization.Get("editor", "settings.title");
            ScopedModalShell modal(
                {
                    .id = "Settings",
                    .title = title.c_str(),
                    .requestedSize = {Layout::ModalW, Layout::ModalH},
                    .viewportPadding = Layout::ViewportPad,
                    .headerHeight = Layout::HeaderH,
                    .footerHeight = Layout::FooterH,
                    .logo = logo,
                    .showBrandMark = true,
                    .titleFontSize = 13.0F,
                },
                ctx.theme.fonts);
            ModalSplitPane(
                {
                    .id = "SettingsBody",
                    .size = {0.0F, modal.BodyHeight()},
                    .leadingWidth = Layout::NavW,
                    .leadingPadding = {8.0F, 10.0F},
                    .contentPadding = {26.0F, 22.0F},
                    .leadingBackground = Bg0(),
                    .contentBackground = Bg1(),
                    .drawDivider = true,
                    .leadingScrollable = false,
                    .contentScrollable = true,
                },
                [&st, &ctx]() { DrawNavigationContent(st, ctx); },
                [&st, &ctx]() { DrawContent(st, ctx); });
            st.dirty = CollectDraftSettings(st) != st.committed;
            modal.BeginFooter({0.0F, 0.0F});
            const bool footerRequestedClose =
                DrawFooterContent(st, settings, ctx);
            modal.EndFooter();

            return (modal.CloseRequested() || footerRequestedClose)
                       ? ModalFrameResult::RequestClose(ModalCloseReason::Cancelled)
                       : ModalFrameResult::None();
        }
    } // namespace

    ModalFrameResult DrawSettingsModalPresentation(SettingsState& state, EditorSettingsService& settings,
                                                   const EditorGuiContext& ctx, const ImTextureID logo)
    {
        return DrawSettingsModalPresentationImpl(state, settings, ctx, logo);
    }

    ModalFrameResult SettingsModal::Draw()
    {
        return DrawSettingsModalPresentation(m_draft, m_settings, m_context, m_logo);
    }
} // namespace Horo::Editor
