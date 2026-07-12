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
            ImVec4 statusColour;
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
            ImVec4 signedColour;
            const char* restartBadge;
            const char* action1;
            const char* action2;
        };


        void DrawNavGroup(const char* label, const EditorGuiContext& ctx)
        {
            ImGui::Dummy({0.0F, 5.0F});
            ScopedTextStyle ts(ctx.theme.fonts.monoSemiBold, 12.0F, Theme::FontPx::MonoSemiBold);
            ImGui::PushStyleColor(ImGuiCol_Text, Theme::Dim());
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
                if (st.activeTab != static_cast<int>(item.tab)) {
                    LOG_DEBUG("editor.settings", "Settings tab changed to '%s'.", item.label);
                }
                st.activeTab = static_cast<int>(item.tab);
            }
            const bool hovered = ImGui::IsItemHovered();

            auto* dl = ImGui::GetWindowDrawList();
            if (active || hovered)
            {
                const auto accentGlow = ImVec4{Theme::Accent().x, Theme::Accent().y, Theme::Accent().z, 0.14F};
                dl->AddRectFilled(pos, {pos.x + rowW, pos.y + rowH}, Theme::U32(active ? accentGlow : Theme::Hover()),
                                  Layout::Radius);
            }
            if (active)
            {
                dl->AddRectFilled(pos, {pos.x + 2.0F, pos.y + rowH}, Theme::U32(Theme::Accent()), 1.0F);
            }

            ImGui::SetCursorScreenPos({pos.x + 12.0F, pos.y + 10.0F});
            {
                ScopedTextStyle ts(ctx.theme.fonts.monoSemiBold, 14.0F, Theme::FontPx::MonoSemiBold);
                ImGui::PushStyleColor(ImGuiCol_Text, active ? Theme::Accent() : Theme::Muted());
                ImGui::TextUnformatted(item.icon);
                ImGui::PopStyleColor();
            }
            ImGui::SameLine(0.0F, 10.0F);
            {
                ScopedTextStyle ts(ctx.theme.fonts.sans, 15.0F, Theme::FontPx::Sans);
                ImGui::PushStyleColor(ImGuiCol_Text, active ? Theme::Text() : Theme::Muted());
                ImGui::TextUnformatted(item.label);
                ImGui::PopStyleColor();
            }
            ImGui::SetCursorScreenPos({pos.x, pos.y + rowH + 1.0F});
            ImGui::PopID();
        }

        [[nodiscard]] bool DrawHeader(SettingsState&,
                                      const EditorGuiContext& ctx,
                                      const ::ImTextureID logo)
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
                ImGui::GetWindowDrawList()->AddRectFilled({mark.x + 4.0F, mark.y + 4.0F},
                                                          {mark.x + 14.0F, mark.y + 14.0F}, Theme::U32(Theme::Accent()),
                                                          2.0F);
                ImGui::Dummy({20.0F, 20.0F});
                ImGui::SameLine(0.0F, 10.0F);
            }
            {
                ScopedTextStyle ts(ctx.theme.fonts.monoSemiBold, 13.0F, Theme::FontPx::MonoSemiBold);
                ImGui::PushStyleColor(ImGuiCol_Text, Theme::Text());
                ImGui::TextUnformatted(ctx.localization.Get("editor", "settings.title").c_str());
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

        void DrawNavigation(SettingsState& st, const EditorGuiContext& ctx, const float bodyH)
        {
            using enum SettingsTab;
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{8.0F, 10.0F});
            ImGui::PushStyleColor(ImGuiCol_ChildBg, Theme::Bg0());
            ImGui::BeginChild("SettingsNav", {Layout::NavW, bodyH}, false, ImGuiWindowFlags_AlwaysUseWindowPadding);

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

            const ImVec2 p = ImGui::GetWindowPos();
            ImGui::GetWindowDrawList()->AddLine({p.x + Layout::NavW - 1.0F, p.y},
                                                {p.x + Layout::NavW - 1.0F, p.y + bodyH},
                                                Theme::U32(Theme::Border()),
                                                1.0F);
            ImGui::EndChild();
            ImGui::PopStyleColor();
            ImGui::PopStyleVar();
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
            const std::string startupDescription = ctx.localization.Get(
                "editor", "settings.general.startup_behavior.description");
            const std::string autosaveLabel = ctx.localization.Get("editor", "settings.general.autosave_interval");
            const std::string autosaveDescription = ctx.localization.Get(
                "editor", "settings.general.autosave_interval.description");
            const std::string confirmLabel = ctx.localization.Get("editor", "settings.general.confirm_exit");
            const std::string confirmDescription = ctx.localization.Get(
                "editor", "settings.general.confirm_exit.description");
            const std::string restoreLabel = ctx.localization.Get("editor", "settings.general.restore_workspace");
            const std::string restoreDescription = ctx.localization.Get(
                "editor", "settings.general.restore_workspace.description");
            const std::string defaultSceneLabel = ctx.localization.Get("editor", "settings.general.default_scene");
            const std::string defaultSceneDescription = ctx.localization.Get(
                "editor", "settings.general.default_scene.description");
            SettingRow(startupLabel.c_str(), startupDescription.c_str(), ctx.theme.fonts,
                       [&st, &ctx, kStartup]()
                       {
                           (void)ComboControl("##startup", &st.general.startupAction, kStartup.data(),
                                              static_cast<int>(kStartup.size()), ctx.theme.fonts);
                       });
            SettingRow(autosaveLabel.c_str(), autosaveDescription.c_str(), ctx.theme.fonts,
                       [&st, &ctx]()
                       {
                           SliderIntControl("##autosave", &st.general.autoSaveInterval, 0, 30,
                                            SliderValueFormat::Minutes, ctx.theme.fonts);
                       });
            SettingRow(confirmLabel.c_str(), confirmDescription.c_str(), ctx.theme.fonts,
                       [&st, &ctx]()
                       {
                           (void)ToggleControl("confirm-exit", &st.general.confirmExit, ctx.theme.fonts);
                       });
            const std::string sessionGroup = ctx.localization.Get("editor", "settings.general.session_group");
            SettingGroup(sessionGroup.c_str(), ctx.theme.fonts);
            SettingRow(restoreLabel.c_str(), restoreDescription.c_str(), ctx.theme.fonts,
                       [&st, &ctx]()
                       {
                           (void)ToggleControl("restore-workspace", &st.general.restoreWorkspace, ctx.theme.fonts);
                       });
            SettingRow(defaultSceneLabel.c_str(), defaultSceneDescription.c_str(), ctx.theme.fonts,
                       [&st, &ctx]()
                       {
                           (void)InputTextControl("##default-scene", st.general.defaultScene, 64, ctx.theme.fonts);
                       });
            const std::string english = ctx.localization.Get("editor", "settings.language.english");
            const std::string turkish = ctx.localization.Get("editor", "settings.language.turkish");
            const std::array<const char*, 2> kLanguages = {english.c_str(), turkish.c_str()};
            const std::string languageLabel = ctx.localization.Get("editor", "settings.language");
            const std::string languageDescription = ctx.localization.Get("editor", "settings.language.description");
            SettingRow(languageLabel.c_str(), languageDescription.c_str(), ctx.theme.fonts,
                       [&st, &ctx, kLanguages]()
                       {
                           int languageIndex = st.general.languageTag == "tr-TR" ? 1 : 0;
                           if (ComboControl("##language", &languageIndex, kLanguages.data(),
                                            static_cast<int>(kLanguages.size()), ctx.theme.fonts))
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
            const std::string colorThemeDescription = ctx.localization.Get(
                "editor", "settings.appearance.color_theme.description");
            const std::string customThemeLabel = ctx.localization.Get("editor", "settings.appearance.custom_theme");
            const std::string customThemeDescription = ctx.localization.Get(
                "editor", "settings.appearance.custom_theme.description");
            const std::string accentLabel = ctx.localization.Get("editor", "settings.appearance.accent_color");
            const std::string accentDescription = ctx.localization.Get(
                "editor", "settings.appearance.accent_color.description");
            const std::string scaleLabel = ctx.localization.Get("editor", "settings.appearance.ui_scale");
            const std::string scaleDescription = ctx.localization.Get(
                "editor", "settings.appearance.ui_scale.description");
            const std::string fontSizeLabel = ctx.localization.Get("editor", "settings.appearance.code_font_size");
            const std::string fontSizeDescription = ctx.localization.Get(
                "editor", "settings.appearance.code_font_size.description");
            SettingRow(colorThemeLabel.c_str(), colorThemeDescription.c_str(), ctx.theme.fonts, [&st, &ctx]()
            {
                const auto& themeList = Theme::GetThemeList();
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
            SettingRow(accentLabel.c_str(), accentDescription.c_str(), ctx.theme.fonts, [&st, &ctx]()
            {
                (void)ColorHexControl("accent-color", st.appearance.accentHex, 16, ctx.theme.fonts);
            });
            const std::string typoGroup = ctx.localization.Get("editor", "settings.appearance.typography_group");
            SettingGroup(typoGroup.c_str(), ctx.theme.fonts);
            SettingRow(scaleLabel.c_str(), scaleDescription.c_str(), ctx.theme.fonts,
                       [&st, &ctx]()
                       {
                           SliderIntControl("##ui-scale", &st.appearance.uiScale, 75, 200, SliderValueFormat::Percent,
                                            ctx.theme.fonts, 25);
                       });
            SettingRow(fontSizeLabel.c_str(), fontSizeDescription.c_str(), ctx.theme.fonts,
                       [&st, &ctx]()
                       {
                           (void)InputTextControl("##font-size", st.appearance.editorFontSize, 8, ctx.theme.fonts);
                       });
        }

        void ResolveShortcutConflicts(SettingsState::InputTab& input, const int editedIndex)
        {
            for (int index = 0; index < SettingsState::InputTab::kShortcutActionCount; ++index)
                input.shortcuts[index].conflict = false;
            if (input.shortcuts[editedIndex].keys.empty()) return;
            for (int index = 0; index < SettingsState::InputTab::kShortcutActionCount; ++index)
            {
                if (index == editedIndex || input.shortcuts[index].keys.empty()) continue;
                if (input.shortcuts[editedIndex].keys == input.shortcuts[index].keys)
                {
                    input.shortcuts[editedIndex].conflict = true;
                    input.shortcuts[index].conflict = true;
                }
            }
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
            const std::string actionHeader = ctx.localization.Get("editor", "settings.input.action");
            const std::string shortcutHeader = ctx.localization.Get("editor", "settings.input.shortcut");
            const std::string conflictText = ctx.localization.Get("editor", "settings.input.shortcut_conflict");
            SettingRow(orbitLabel.c_str(), orbitDescription.c_str(), ctx.theme.fonts,
                       [&st, &ctx]()
                       {
                           SliderIntControl("##orbit", &st.input.orbitSensitivity, 10, 300, SliderValueFormat::Integer,
                                            ctx.theme.fonts);
                       });
            SettingRow(panLabel.c_str(), panDescription.c_str(), ctx.theme.fonts,
                       [&st, &ctx]()
                       {
                           SliderIntControl("##pan", &st.input.panSensitivity, 10, 300, SliderValueFormat::Integer,
                                            ctx.theme.fonts);
                       });
            SettingRow(invertLabel.c_str(), invertDescription.c_str(), ctx.theme.fonts,
                       [&st, &ctx]() { (void)ToggleControl("invert-y", &st.input.invertOrbitY, ctx.theme.fonts); });

            const std::string shortcutsGroup = ctx.localization.Get("editor", "settings.input.shortcuts_group");
            SettingGroup(shortcutsGroup.c_str(), ctx.theme.fonts);

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
                ImGui::TableSetupColumn(actionHeader.c_str(), ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn(shortcutHeader.c_str(),
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
                    static constexpr std::array<const char*, 8> kShortcutLocKeys = {
                        "settings.input.shortcut.save_scene",
                        "settings.input.shortcut.undo",
                        "settings.input.shortcut.build",
                        "settings.input.shortcut.find",
                        "settings.input.shortcut.replace",
                        "settings.input.shortcut.duplicate",
                        "settings.input.shortcut.delete",
                        "settings.input.shortcut.select_all"
                    };
                    const std::string actionName = ctx.localization.Get("editor", kShortcutLocKeys[i]);
                    ImGui::TextUnformatted(actionName.empty()
                                               ? SettingsState::InputTab::kShortcutActions[i]
                                               : actionName.c_str());
                    ImGui::PopStyleColor();

                    // Key recorder
                    ImGui::TableSetColumnIndex(1);
                    const bool isListening = (st.input.listeningShortcut == i);

                    const std::string clickToRecord = ctx.localization.Get(
                        "editor", "settings.input.shortcut.click_to_record");
                    const std::string pressKeys = ctx.localization.Get("editor", "settings.input.shortcut.press_keys");

                    if (bool localListening = isListening; Ui::ShortcutRecorder("recorder",
                        st.input.shortcuts[i].keys.c_str(),
                        &localListening,
                        st.input.shortcuts[i].keys,
                        ctx.theme.fonts,
                        clickToRecord.empty() ? "Click to record" : clickToRecord.c_str(),
                        pressKeys.empty() ? "Press keys..." : pressKeys.c_str()))
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
                            ScopedTextStyle ts(ctx.theme.fonts.mono, 10.5F, Theme::FontPx::Mono);
                            ImGui::TextUnformatted(conflictText.c_str());
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

        void DrawRendering(SettingsState& st, const EditorGuiContext& ctx)
        {
            const std::array<std::string, 4> viewportText = {
                ctx.localization.Get("editor", "settings.rendering.shaded"),
                ctx.localization.Get("editor", "settings.rendering.wireframe"),
                ctx.localization.Get("editor", "settings.rendering.lit"),
                ctx.localization.Get("editor", "settings.rendering.unlit")
            };
            const std::array<const char*, 4> kViewport = {
                viewportText[0].c_str(), viewportText[1].c_str(), viewportText[2].c_str(), viewportText[3].c_str()
            };
            const std::array<std::string, 4> tierText = {
                ctx.localization.Get("editor", "settings.rendering.high_end"),
                ctx.localization.Get("editor", "settings.rendering.dx12_vulkan"),
                ctx.localization.Get("editor", "settings.rendering.dx11"),
                ctx.localization.Get("editor", "settings.rendering.es3")
            };
            const std::array<const char*, 4> kTier = {
                tierText[0].c_str(), tierText[1].c_str(), tierText[2].c_str(), tierText[3].c_str()
            };
            const std::string sectionTitle = ctx.localization.Get("editor", "settings.nav.rendering");
            SectionTitle(sectionTitle.c_str(), ctx.theme.fonts);
            const std::string viewportGroup = ctx.localization.Get("editor", "settings.rendering.viewport_group");
            SettingGroup(viewportGroup.c_str(), ctx.theme.fonts, true);
            const std::string viewportLabel = ctx.localization.Get("editor", "settings.rendering.viewport_mode");
            const std::string viewportDescription = ctx.localization.Get(
                "editor", "settings.rendering.viewport_mode.description");
            const std::string gridLabel = ctx.localization.Get("editor", "settings.rendering.grid_overlay");
            const std::string gridDescription = ctx.localization.Get(
                "editor", "settings.rendering.grid_overlay.description");
            const std::string tierLabel = ctx.localization.Get("editor", "settings.rendering.tier");
            const std::string tierDescription = ctx.localization.Get("editor", "settings.rendering.tier.description");
            const std::string budgetLabel = ctx.localization.Get("editor", "settings.rendering.texture_budget");
            const std::string budgetDescription = ctx.localization.Get(
                "editor", "settings.rendering.texture_budget.description");
            SettingRow(viewportLabel.c_str(), viewportDescription.c_str(), ctx.theme.fonts, [&st, &ctx, kViewport]()
            {
                (void)ComboControl("##viewport", &st.rendering.viewportMode, kViewport.data(),
                                   static_cast<int>(kViewport.size()), ctx.theme.fonts);
            });
            SettingRow(gridLabel.c_str(), gridDescription.c_str(), ctx.theme.fonts, [&st, &ctx]()
            {
                (void)ToggleControl("grid", &st.rendering.gridOverlay, ctx.theme.fonts);
            });
            const std::string qualityGroup = ctx.localization.Get("editor", "settings.rendering.quality_group");
            SettingGroup(qualityGroup.c_str(), ctx.theme.fonts);
            SettingRow(tierLabel.c_str(), tierDescription.c_str(), ctx.theme.fonts,
                       [&st, &ctx, kTier]()
                       {
                           (void)ComboControl("##tier", &st.rendering.renderingTier, kTier.data(),
                                              static_cast<int>(kTier.size()), ctx.theme.fonts);
                       });
            SettingRow(budgetLabel.c_str(), budgetDescription.c_str(), ctx.theme.fonts,
                       [&st, &ctx]()
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
            const std::string enabledDescription = ctx.localization.Get(
                "editor", "settings.audio.enable_in_editor.description");
            SettingRow(volumeLabel.c_str(), volumeDescription.c_str(), ctx.theme.fonts,
                       [&st, &ctx]()
                       {
                           SliderIntControl("##volume", &st.audio.masterVolume, 0, 100, SliderValueFormat::Integer,
                                            ctx.theme.fonts);
                       });
            SettingRow(deviceLabel.c_str(), deviceDescription.c_str(), ctx.theme.fonts,
                       [&st, &ctx, kDevices]()
                       {
                           (void)ComboControl("##audio-device", &st.audio.audioOutputDevice, kDevices.data(),
                                              static_cast<int>(kDevices.size()), ctx.theme.fonts);
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
            const std::string clientsDescription = ctx.localization.Get(
                "editor", "settings.network.max_preview_clients.description");
            const std::string latencyLabel = ctx.localization.Get("editor", "settings.network.simulate_latency");
            const std::string latencyDescription = ctx.localization.Get(
                "editor", "settings.network.simulate_latency.description");
            const std::string threadsLabel = ctx.localization.Get("editor", "settings.network.download_threads");
            const std::string threadsDescription = ctx.localization.Get(
                "editor", "settings.network.download_threads.description");
            SettingRow(clientsLabel.c_str(), clientsDescription.c_str(), ctx.theme.fonts,
                       [&st, &ctx]()
                       {
                           InputIntControl("##max-clients", &st.network.maxPreviewClients, ctx.theme.fonts);
                       });
            SettingRow(latencyLabel.c_str(), latencyDescription.c_str(), ctx.theme.fonts,
                       [&st, &ctx]()
                       {
                           SliderIntControl("##latency", &st.network.simulatedLatencyMs, 0, 500,
                                            SliderValueFormat::Milliseconds, ctx.theme.fonts);
                       });
            SettingRow(threadsLabel.c_str(), threadsDescription.c_str(), ctx.theme.fonts,
                       [&st, &ctx]()
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
                logLevelText[0].c_str(), logLevelText[1].c_str(), logLevelText[2].c_str(), logLevelText[3].c_str()
            };
            const std::string sectionTitle = ctx.localization.Get("editor", "settings.nav.diagnostics");
            SectionTitle(sectionTitle.c_str(), ctx.theme.fonts);
            const std::string loggingGroup = ctx.localization.Get("editor", "settings.diagnostics.logging_group");
            SettingGroup(loggingGroup.c_str(), ctx.theme.fonts, true);
            const std::string logLevelLabel = ctx.localization.Get("editor", "settings.diagnostics.log_level");
            const std::string logLevelDescription = ctx.localization.Get(
                "editor", "settings.diagnostics.log_level.description");
            const std::string writeLogLabel = ctx.localization.Get("editor", "settings.diagnostics.write_log");
            const std::string writeLogDescription = ctx.localization.Get(
                "editor", "settings.diagnostics.write_log.description");
            const std::string captureLabel = ctx.localization.Get("editor", "settings.diagnostics.auto_capture");
            const std::string captureDescription = ctx.localization.Get(
                "editor", "settings.diagnostics.auto_capture.description");
            const std::string thresholdLabel = ctx.localization.Get("editor", "settings.diagnostics.stutter_threshold");
            const std::string thresholdDescription = ctx.localization.Get(
                "editor", "settings.diagnostics.stutter_threshold.description");
            SettingRow(logLevelLabel.c_str(), logLevelDescription.c_str(), ctx.theme.fonts,
                       [&st, &ctx, kLogLevels]()
                       {
                           (void)ComboControl("##log-level", &st.diagnostics.consoleLogLevel, kLogLevels.data(),
                                              static_cast<int>(kLogLevels.size()), ctx.theme.fonts);
                       });
            SettingRow(writeLogLabel.c_str(), writeLogDescription.c_str(), ctx.theme.fonts,
                       [&st, &ctx]()
                       {
                           (void)ToggleControl("write-log", &st.diagnostics.writeLogToFile, ctx.theme.fonts);
                       });
            const std::string profilerGroup = ctx.localization.Get("editor", "settings.diagnostics.profiler_group");
            SettingGroup(profilerGroup.c_str(), ctx.theme.fonts);
            SettingRow(captureLabel.c_str(), captureDescription.c_str(), ctx.theme.fonts,
                       [&st, &ctx]()
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
            ImGui::PushStyleColor(ImGuiCol_Text, Theme::Muted());
            {
                ScopedTextStyle ts(ctx.theme.fonts.sans, 12.5F, Theme::FontPx::Sans);
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
                const std::string feedback = ctx.localization.Get(
                    "editor", "settings.plugins.feedback.install_not_implemented");
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
                ctx.localization.Get("editor", "settings.plugins.runtime")
            };
            const std::array<const char*, 2> kSectionTabs = {sectionTabsStr[0].c_str(), sectionTabsStr[1].c_str()};
            constexpr float pad = 4.0F;
            constexpr float tabH = 31.0F;
            const float containerW = ImGui::GetContentRegionAvail().x;
            const float installedW = 128.0F;
            const float runtimeW = std::min(186.0F, std::max(156.0F, containerW - installedW - pad * 4.0F));
            const float containerH = tabH + pad * 2.0F;
            const ImVec2 p = ImGui::GetCursorScreenPos();
            auto* dl = ImGui::GetWindowDrawList();

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
                                      active
                                          ? ImVec4{Theme::Accent().x, Theme::Accent().y, Theme::Accent().z, 0.12F}
                                          : ImVec4{0.0F, 0.0F, 0.0F, 0.0F});
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                                      active
                                          ? ImVec4{Theme::Accent().x, Theme::Accent().y, Theme::Accent().z, 0.18F}
                                          : Theme::Hover());
                ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                                      ImVec4{Theme::Accent().x, Theme::Accent().y, Theme::Accent().z, 0.22F});
                ImGui::PushStyleColor(ImGuiCol_Text, active ? Theme::Text() : Theme::Muted());
                {
                    ScopedTextStyle ts(ctx.theme.fonts.sans, 14.5F, Theme::FontPx::Sans);
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

        void DrawPlugins(SettingsState& st, const EditorGuiContext& ctx)
        {
            const std::string sectionTitle = ctx.localization.Get("editor", "settings.nav.plugins");
            SectionTitle(sectionTitle.c_str(), ctx.theme.fonts);
            DrawPluginsHeader(st, ctx);

            ImGui::Dummy({0.0F, 14.0F});
            DrawPluginSectionTabs(st, ctx);

            if (st.pluginSectionTab == 0)
                DrawInstalledPlugins(st, ctx);
            else
                DrawRuntimeDiscovery(st, ctx);
        }

        // ── Badge helper ──────────────────────────────────────────────
        /** @brief Draws a pill badge with coloured background and text. */
        void DrawBadge(const char* text, ImVec4 colour, const EditorGuiContext& ctx)
        {
            auto* dl = ImGui::GetWindowDrawList();
            const float padX = 7.0F;
            const float padY = 4.0F;
            const ImVec2 textSize = ImGui::CalcTextSize(text);
            const ImVec2 badgeMin = ImGui::GetCursorScreenPos();
            const ImVec2 badgeMax{
                badgeMin.x + textSize.x + padX * 2.0F,
                badgeMin.y + textSize.y + padY * 2.0F
            };

            dl->AddRectFilled(badgeMin, badgeMax,
                              ImColor{colour.x, colour.y, colour.z, 0.10F}, 999.0F);
            dl->AddRect(badgeMin, badgeMax,
                        ImColor{colour.x, colour.y, colour.z, 0.28F}, 999.0F);

            ImGui::SetCursorScreenPos({badgeMin.x + padX, badgeMin.y + padY});
            ImGui::PushStyleColor(ImGuiCol_Text, colour);
            {
                ScopedTextStyle ts(ctx.theme.fonts.mono, 10.5F, Theme::FontPx::Mono);
                ImGui::TextUnformatted(text);
            }
            ImGui::PopStyleColor();
            ImGui::SetCursorScreenPos({badgeMax.x + 6.0F, badgeMin.y});
        }

        struct PermissionRowSpec
        {
            const char* icon;
            const char* title;
            const char* desc;
            const char* badgeText;
            ImVec4 badgeColour;
        };

        struct DiagnosticMetricSpec
        {
            const char* label;
            const char* value;
            const char* hint;
            ImVec4 valueColour;
        };

        [[nodiscard]] float BadgeWidth(const char* text)
        {
            return ImGui::CalcTextSize(text).x + 18.0F;
        }

        template <typename DrawControl>
        void PluginSettingRow(const char* label,
                              const char* description,
                              const EditorGuiContext& ctx,
                              DrawControl drawControl)
        {
            const float rowW = ImGui::GetContentRegionAvail().x;
            const float startY = ImGui::GetCursorScreenPos().y;

            {
                ScopedTextStyle ts(ctx.theme.fonts.sans, 14.0F, Theme::FontPx::Sans);
                ImGui::PushStyleColor(ImGuiCol_Text, Theme::Text());
                ImGui::TextUnformatted(label);
                ImGui::PopStyleColor();
            }

            if (description != nullptr && description[0] != '\0')
            {
                ScopedTextStyle ts(ctx.theme.fonts.sans, 12.0F, Theme::FontPx::Sans);
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

        void DrawToggleState(const char* id, bool* value, const EditorGuiContext& ctx)
        {
            (void)ToggleControl(id, value, ctx.theme.fonts, false);
            ImGui::SameLine(0.0F, 8.0F);
            ScopedTextStyle ts(ctx.theme.fonts.sans, 12.5F, Theme::FontPx::Sans);
            ImGui::PushStyleColor(ImGuiCol_Text, *value ? Theme::Text() : Theme::Muted());
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
                const float badgeW = BadgeWidth(perm.badgeText);
                auto* dl = ImGui::GetWindowDrawList();

                dl->AddRectFilled(p, {p.x + cardW, p.y + cardH}, Theme::U32(Theme::Bg3()), Layout::Radius);
                dl->AddRect(p, {p.x + cardW, p.y + cardH}, Theme::U32(Theme::Border()), Layout::Radius);

                ImGui::SetCursorScreenPos({p.x + 13.0F, p.y + 19.0F});
                {
                    ScopedTextStyle ts(ctx.theme.fonts.monoSemiBold, 14.0F, Theme::FontPx::MonoSemiBold);
                    ImGui::PushStyleColor(ImGuiCol_Text, perm.badgeColour);
                    ImGui::TextUnformatted(perm.icon);
                    ImGui::PopStyleColor();
                }

                ImGui::SetCursorScreenPos({p.x + 40.0F, p.y + 11.0F});
                {
                    ScopedTextStyle ts(ctx.theme.fonts.sans, 12.5F, Theme::FontPx::Sans);
                    ImGui::PushStyleColor(ImGuiCol_Text, Theme::Text());
                    ImGui::TextUnformatted(perm.title);
                    ImGui::PopStyleColor();
                }

                ImGui::SetCursorScreenPos({p.x + 40.0F, p.y + 32.0F});
                {
                    ScopedTextStyle ts(ctx.theme.fonts.sans, 11.5F, Theme::FontPx::Sans);
                    ImGui::PushStyleColor(ImGuiCol_Text, Theme::Muted());
                    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + cardW - badgeW - 68.0F);
                    ImGui::TextWrapped("%s", perm.desc);
                    ImGui::PopTextWrapPos();
                    ImGui::PopStyleColor();
                }

                ImGui::SetCursorScreenPos({p.x + cardW - badgeW - 14.0F, p.y + 20.0F});
                DrawBadge(perm.badgeText, perm.badgeColour, ctx);

                ImGui::SetCursorScreenPos({p.x, p.y + cardH + 8.0F});
                ImGui::PopID();
            }
        }

        void DrawDiagnosticMetrics(const std::span<const DiagnosticMetricSpec> metrics, const EditorGuiContext& ctx)
        {
            constexpr float gap = 8.0F;
            constexpr float cardH = 68.0F;
            const float availW = ImGui::GetContentRegionAvail().x;
            const float cardW = (availW - gap * static_cast<float>(metrics.size() - 1)) /
                static_cast<float>(metrics.size());
            const ImVec2 start = ImGui::GetCursorScreenPos();
            auto* dl = ImGui::GetWindowDrawList();

            for (std::size_t i = 0; i < metrics.size(); ++i)
            {
                const ImVec2 p{start.x + static_cast<float>(i) * (cardW + gap), start.y};
                const auto& m = metrics[i];
                dl->AddRectFilled(p, {p.x + cardW, p.y + cardH}, Theme::U32(Theme::Bg3()), Layout::Radius);
                dl->AddRect(p, {p.x + cardW, p.y + cardH}, Theme::U32(Theme::Border()), Layout::Radius);

                ImGui::SetCursorScreenPos({p.x + 12.0F, p.y + 10.0F});
                {
                    ScopedTextStyle ts(ctx.theme.fonts.monoSemiBold, 9.5F, Theme::FontPx::MonoSemiBold);
                    ImGui::PushStyleColor(ImGuiCol_Text, Theme::Dim());
                    ImGui::TextUnformatted(m.label);
                    ImGui::PopStyleColor();
                }

                ImGui::SetCursorScreenPos({p.x + 12.0F, p.y + 28.0F});
                {
                    ScopedTextStyle ts(ctx.theme.fonts.monoSemiBold, 15.0F, Theme::FontPx::MonoSemiBold);
                    ImGui::PushStyleColor(ImGuiCol_Text, m.valueColour);
                    ImGui::TextUnformatted(m.value);
                    ImGui::PopStyleColor();
                }

                if (m.hint != nullptr && m.hint[0] != '\0')
                {
                    ImGui::SetCursorScreenPos({p.x + 12.0F, p.y + 49.0F});
                    ScopedTextStyle ts(ctx.theme.fonts.sans, 9.8F, Theme::FontPx::Sans);
                    ImGui::PushStyleColor(ImGuiCol_Text, Theme::Dim());
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
                dl->AddRectFilled(p, {p.x + rowW, p.y + rowH}, Theme::U32(i % 2 == 0 ? Theme::Bg3() : Theme::Bg2()),
                                  Layout::Radius);

                ImGui::SetCursorScreenPos({p.x + 10.0F, p.y + 7.0F});
                ScopedTextStyle ts(ctx.theme.fonts.mono, 10.5F, Theme::FontPx::Mono);
                ImGui::PushStyleColor(ImGuiCol_Text, Theme::Muted());
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
                ScopedTextStyle ts(ctx.theme.fonts.mono, 10.0F, Theme::FontPx::Mono);
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
                ScopedTextStyle ts(ctx.theme.fonts.mono, 11.0F, Theme::FontPx::Mono);
                ImGui::PushStyleColor(ImGuiCol_Text, Theme::Muted());
                ImGui::TextUnformatted(manifest);
                ImGui::PopStyleColor();
            }
            ImGui::EndChild();
            ImGui::PopStyleColor();
        }

        [[nodiscard]] bool ContainsCaseInsensitive(const char* text, const std::string& query)
        {
            if (query.empty()) return true;
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
                if (*needle == '\0') return true;
            }
            return false;
        }

        // ── Plugin list (left column) ─────────────────────────────────
        void DrawPluginList(SettingsState& st, const EditorGuiContext& ctx, float /*listW*/)
        {
            SettingGroup("INSTALLED PLUGINS", ctx.theme.fonts, true);

            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2{10.0F, 7.0F});
            ImGui::PushStyleColor(ImGuiCol_FrameBg, Theme::Bg3());
            ImGui::PushStyleColor(ImGuiCol_Text, Theme::Text());
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
                        "Horo MCP Bridge",
                        mcpDesc.c_str(),
                        "v0.4.0", mcpStatus.c_str(), Theme::Ok(), mcpCat.c_str(), 0,
                        &st.plugins.horoMcpBridge
                    },
                    {
                        "Vendor FMOD Integration",
                        fmodDesc.c_str(),
                        "v2.02.20", fmodStatus.c_str(), Theme::Ok(), fmodCat.c_str(), 1,
                        &st.plugins.fmodIntegration
                    },
                    {
                        "Steamworks SDK",
                        steamDesc.c_str(),
                        "v1.59", steamStatus.c_str(), Theme::Warn(), steamCat.c_str(), 2,
                        &st.plugins.steamworksSdk
                    },
                }
            };

            for (const auto& p : kPlugins)
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
                auto* dl = ImGui::GetWindowDrawList();

                if (active)
                {
                    dl->AddRectFilled(cardPos, {cardPos.x + cardW, cardPos.y + cardH},
                                      ImColor{Theme::Accent().x, Theme::Accent().y, Theme::Accent().z, 0.09F},
                                      Layout::Radius);
                    dl->AddRect(cardPos, {cardPos.x + cardW, cardPos.y + cardH}, Theme::U32(Theme::BorderStrong()),
                                Layout::Radius);
                    dl->AddRectFilled(cardPos, {cardPos.x + 3.0F, cardPos.y + cardH},
                                      Theme::U32(Theme::Accent()), 1.0F);
                }
                else if (ImGui::IsMouseHoveringRect(cardPos, {cardPos.x + cardW, cardPos.y + cardH}))
                {
                    dl->AddRectFilled(cardPos, {cardPos.x + cardW, cardPos.y + cardH},
                                      Theme::U32(Theme::Hover()), Layout::Radius);
                    dl->AddRect(cardPos, {cardPos.x + cardW, cardPos.y + cardH}, Theme::U32(Theme::Border()),
                                Layout::Radius);
                }
                else
                {
                    dl->AddRect(cardPos, {cardPos.x + cardW, cardPos.y + cardH}, Theme::U32(Theme::Border()),
                                Layout::Radius);
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
                    ScopedTextStyle ts(ctx.theme.fonts.sans, 14.0F, Theme::FontPx::Sans);
                    ImGui::PushStyleColor(ImGuiCol_Text, Theme::Text());
                    ImGui::TextUnformatted(p.name);
                    ImGui::PopStyleColor();
                }

                ImGui::SetCursorScreenPos({cardPos.x + cardW - 48.0F, cardPos.y + 9.0F});
                (void)ToggleControl("##tog", p.enabled, ctx.theme.fonts, false);

                ImGui::SetCursorScreenPos({innerX, cardPos.y + 33.0F});
                {
                    ScopedTextStyle ts(ctx.theme.fonts.sans, 11.8F, Theme::FontPx::Sans);
                    ImGui::PushStyleColor(ImGuiCol_Text, Theme::Muted());
                    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + cardW - (innerX - cardPos.x) - 56.0F);
                    ImGui::TextWrapped("%s", p.desc);
                    ImGui::PopTextWrapPos();
                    ImGui::PopStyleColor();
                }

                ImGui::SetCursorScreenPos({innerX, cardPos.y + cardH - 26.0F});
                DrawBadge(p.version, Theme::Accent(), ctx);
                DrawBadge(p.statusLabel, p.statusColour, ctx);
                {
                    ScopedTextStyle ts(ctx.theme.fonts.mono, 10.5F, Theme::FontPx::Mono);
                    ImGui::PushStyleColor(ImGuiCol_Text, Theme::Dim());
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
                ScopedTextStyle ts(ctx.theme.fonts.mono, 14.5F, Theme::FontPx::Mono);
                ImGui::PushStyleColor(ImGuiCol_Text, Theme::Text());
                ImGui::TextUnformatted(hdr.name);
                ImGui::PopStyleColor();
            }

            ImGui::SetCursorScreenPos({p.x + 20.0F, p.y + 40.0F});
            {
                ScopedTextStyle ts(ctx.theme.fonts.sans, 12.0F, Theme::FontPx::Sans);
                ImGui::PushStyleColor(ImGuiCol_Text, Theme::Muted());
                ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + headerW - 44.0F);
                ImGui::TextWrapped("%s", hdr.desc);
                ImGui::PopTextWrapPos();
                ImGui::PopStyleColor();
            }

            ImGui::SetCursorScreenPos({p.x + 20.0F, p.y + 94.0F});
            DrawBadge(hdr.scopeBadge, Theme::Accent(), ctx);
            DrawBadge(hdr.signedBadge, hdr.signedColour, ctx);
            {
                ScopedTextStyle ts(ctx.theme.fonts.mono, 10.5F, Theme::FontPx::Mono);
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
                    DrawPluginDetailPanelPrimaryAction(st);
                ImGui::PopStyleColor();
                ImGui::SameLine(0.0F, actionGap);
                const bool danger = (st.selectedPlugin == 0 || st.selectedPlugin == 1);
                if (danger) ImGui::PushStyleColor(ImGuiCol_Text, Theme::Err());
                if (ImGui::Button(hdr.action2, {actionW, 28.0F}))
                    DrawPluginDetailPanelSecondaryAction(st);
                if (danger) ImGui::PopStyleColor();
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
                if (i > 0) ImGui::SameLine(0.0F, tabGap);
                const bool active = activeTab == i;
                ImGui::PushID(i + 200);
                ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, Layout::Radius);
                ImGui::PushStyleColor(ImGuiCol_Button,
                                      active
                                          ? ImVec4{Theme::Accent().x, Theme::Accent().y, Theme::Accent().z, 0.09F}
                                          : Theme::Bg2());
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Theme::Hover());
                ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                                      ImVec4{Theme::Accent().x, Theme::Accent().y, Theme::Accent().z, 0.16F});
                ImGui::PushStyleColor(ImGuiCol_Text, active ? Theme::Accent() : Theme::Muted());
                {
                    ScopedTextStyle ts(ctx.theme.fonts.sans, 13.5F, Theme::FontPx::Sans);
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
                        "Horo MCP Bridge",
                        mcpDDesc.c_str(),
                        mcpScope.c_str(), mcpSigned.c_str(), Theme::Ok(), mcpRestart.c_str(),
                        openLogs.c_str(), disableStr.c_str()
                    },
                    {
                        "Vendor FMOD Integration",
                        fmodDDesc.c_str(),
                        fmodScope.c_str(), fmodSigned.c_str(), Theme::Ok(), fmodRestart.c_str(),
                        validateStr.c_str(), disableStr.c_str()
                    },
                    {
                        "Steamworks SDK",
                        steamDDesc.c_str(),
                        steamScope.c_str(), steamSigned.c_str(), Theme::Warn(), steamRestart.c_str(),
                        enableStr.c_str(), openDocs.c_str()
                    },
                }
            };
            const auto& hdr = kDetailHeaders[st.selectedPlugin];
            int& activeTab = st.pluginDetailTab[st.selectedPlugin];
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
        void DrawMcpDetailContent(SettingsState& st, const EditorGuiContext& ctx, int activeTab)
        {
            switch (activeTab)
            {
            case 0:
                SettingGroup("CONNECTION", ctx.theme.fonts, true);
                PluginSettingRow("Transport Mode",
                                 "Use stdio for local tools; HTTP is useful for explicit local integrations.", ctx,
                                 [&st, &ctx]()
                                 {
                                     static constexpr std::array kModes = {"Local HTTP", "stdio", "Named Pipe"};
                                     (void)ComboControl("##transport", &st.mcp.transportMode, kModes.data(), 3,
                                                        ctx.theme.fonts);
                                 });
                PluginSettingRow("MCP Port", "Bound to localhost unless remote access is enabled.", ctx, [&st, &ctx]()
                {
                    InputIntControl("##mcp-port", &st.mcp.port, ctx.theme.fonts);
                });
                PluginSettingRow("Require Session Token",
                                 "Reject tool calls unless they include the generated editor session token.", ctx,
                                 [&st, &ctx]() { DrawToggleState("##token", &st.mcp.requireToken, ctx); });
                PluginSettingRow("Allow Remote Connections", "Off by default to avoid accidental LAN exposure.", ctx,
                                 [&st, &ctx]() { DrawToggleState("##remote", &st.mcp.allowRemote, ctx); });
                SettingGroup("TOOL SCOPE", ctx.theme.fonts);
                PluginSettingRow("Allowed Tool Groups", "Restrict what external tools can invoke.", ctx, [&st, &ctx]()
                {
                    static constexpr std::array kScopes = {
                        "Read + Safe Mutations", "Read Only", "Full Project Access", "Custom Policy..."
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
                                "Read project name, scene list, package graph, and editor state.",
                                "Allowed", Theme::Ok()
                            },
                            {
                                "✓", "Write generated assets",
                                "Create files only under Assets/Generated unless policy is elevated.",
                                "Scoped", Theme::Ok()
                            },
                            {
                                "!", "Execute build commands",
                                "Requires interactive confirmation before running build or release tasks.",
                                "Confirm", Theme::Warn()
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
                            {"STATUS", "Running", "sandboxed", Theme::Ok()},
                            {"LAST CALL", "2m ago", "tool request", Theme::Text()},
                            {"ERRORS", "0", "last 24h", Theme::Ok()},
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
                DrawManifestBlock("plugins/mcp-bridge/plugin.yaml", "id: horo.mcp.bridge\n"
                                  "version: 0.4.0\n"
                                  "entry: plugins/mcp-bridge/bin/horo-mcp\n"
                                  "scope: editor\n"
                                  "permissions:\n"
                                  "  - project.read\n"
                                  "  - assets.write.scoped\n"
                                  "  - commands.run.confirmed", ctx);
                break;
            default:
                break;
            }
        }

        // ── FMOD detail content ───────────────────────────────────────
        void DrawFmodDetailContent(SettingsState& st, const EditorGuiContext& ctx, int activeTab)
        {
            switch (activeTab)
            {
            case 0:
                SettingGroup("AUTHORING", ctx.theme.fonts, true);
                PluginSettingRow("FMOD Studio Path", "Used to open projects and compile banks from the editor.", ctx,
                                 [&st, &ctx]()
                                 {
                                     (void)InputTextControl("##fmod-path", st.fmod.studioPath, 128, ctx.theme.fonts);
                                 });
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
                                 "Prevents shipping builds with unresolved audio events.", ctx, [&st, &ctx]()
                                 {
                                     DrawToggleState("##fmod-fail", &st.fmod.failOnMissing, ctx);
                                 });
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
                                "Limited to configured FMOD project and bank output paths.",
                                "Scoped", Theme::Ok()
                            },
                            {
                                "!", "Launch external FMOD Studio process",
                                "Requires a configured executable path and user initiated action.",
                                "User action", Theme::Warn()
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
                            {"BANKS", "14", "loaded", Theme::Text()},
                            {"UNRESOLVED", "2", "events", Theme::Warn()},
                            {"LIVE UPDATE", "On", "connected", Theme::Ok()},
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
                DrawManifestBlock("plugins/fmod/plugin.yaml", "id: vendor.fmod\n"
                                  "version: 2.02.20\n"
                                  "entry: plugins/fmod/horo-fmod.plugin\n"
                                  "scope: project\n"
                                  "permissions:\n"
                                  "  - audio.bank.readwrite\n"
                                  "  - process.launch.user_action\n"
                                  "  - build.validation", ctx);
                break;
            default:
                break;
            }
        }

        // ── Steamworks detail content ─────────────────────────────────
        void DrawSteamDetailContent(SettingsState& st, const EditorGuiContext& ctx, int activeTab)
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
                PluginSettingRow("Initialize On", "Controls when Steam API is started during editor workflows.", ctx,
                                 [&st, &ctx]()
                                 {
                                     static constexpr std::array kModes = {
                                         "Play Mode Only", "Editor Launch", "Build Runtime Only"
                                     };
                                     (void)ComboControl("##steam-init", &st.steam.initMode, kModes.data(), 3,
                                                        ctx.theme.fonts);
                                 });
                SettingGroup("FEATURES", ctx.theme.fonts);
                PluginSettingRow("Overlay", "Enable Steam overlay while testing from Play Mode.", ctx, [&st, &ctx]()
                {
                    DrawToggleState("##steam-overlay", &st.steam.overlay, ctx);
                });
                PluginSettingRow("Achievements", "Expose achievement authoring and validation panels.", ctx,
                                 [&st, &ctx]() { DrawToggleState("##steam-ach", &st.steam.achievements, ctx); });
                PluginSettingRow("Networking Sockets", "Enable Steam networking transport for multiplayer preview.",
                                 ctx, [&st, &ctx]() { DrawToggleState("##steam-net", &st.steam.networking, ctx); });
                break;

            case 1:
                {
                    static const std::array<PermissionRowSpec, 2> kPerms = {
                        {
                            {
                                "✓", "Read platform config",
                                "Reads App ID, achievements config, and build target metadata.",
                                "Allowed", Theme::Ok()
                            },
                            {
                                "!", "Network access",
                                "Only enabled when Steam networking transport is selected.",
                                "Conditional", Theme::Warn()
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
                            {"STATUS", "Disabled", "not loaded", Theme::Dim()},
                            {"SDK", "Missing", "path required", Theme::Warn()},
                            {"OVERLAY", "Ready", "waiting", Theme::Ok()},
                        }
                    };
                    DrawDiagnosticMetrics(kMetrics, ctx);
                    const std::array kActivity = {
                        "12:45  skipped init because Steamworks SDK is disabled",
                        "12:44  overlay check passed",
                        "12:42  missing SDK path warning emitted"
                    };
                    DrawDiagnosticActivity(kActivity, ctx);
                }
                break;

            case 3:
                DrawManifestBlock("plugins/steamworks/plugin.yaml", "id: vendor.steamworks\n"
                                  "version: 1.59\n"
                                  "entry: plugins/steamworks/horo-steam.plugin\n"
                                  "scope: project\n"
                                  "permissions:\n"
                                  "  - platform.config.read\n"
                                  "  - network.conditional\n"
                                  "  - achievements.write", ctx);
                break;
            default:
                break;
            }
        }

        // ── Installed Plugins (split pane) ────────────────────────────
        void DrawInstalledPlugins(SettingsState& st, const EditorGuiContext& ctx)
        {
            {
                ScopedTextStyle ts(ctx.theme.fonts.sans, 12.5F, Theme::FontPx::Sans);
                ImGui::PushStyleColor(ImGuiCol_Text, Theme::Muted());
                ImGui::TextWrapped(
                    "Select a plugin to edit its settings, permissions, diagnostics, and manifest details. The selected plugin appears below in the same workspace so the layout stays simpler and easier to scan.");
                ImGui::PopStyleColor();
            }
            ImGui::Dummy({0.0F, 10.0F});

            DrawPluginList(st, ctx, ImGui::GetContentRegionAvail().x);

            ImGui::Dummy({0.0F, 10.0F});
            const ImVec2 sep = ImGui::GetCursorScreenPos();
            ImGui::GetWindowDrawList()->AddLine({sep.x, sep.y}, {sep.x + ImGui::GetContentRegionAvail().x, sep.y},
                                                Theme::U32(Theme::Border()), 1.0F);
            ImGui::Dummy({0.0F, 14.0F});

            DrawPluginDetailPanel(st, ctx, ImGui::GetContentRegionAvail().x, true);
        }

        // ── Runtime & Discovery ───────────────────────────────────────
        void DrawRuntimeDiscovery(SettingsState& st, const EditorGuiContext& ctx)
        {
            SettingGroup("RUNTIME OVERVIEW", ctx.theme.fonts, true);

            static const std::array<DiagnosticMetricSpec, 3> kRuntimeCards = {
                {
                    {"ISOLATION", "Sandboxed", "processes", Theme::Text()},
                    {"DISCOVERY", "Project + editor", "paths", Theme::Text()},
                    {"UPDATES", "Signed only", "registries", Theme::Ok()},
                }
            };
            DrawDiagnosticMetrics(kRuntimeCards, ctx);

            SettingGroup("DISCOVERY", ctx.theme.fonts);
            PluginSettingRow("Plugin Discovery Paths",
                             "Semicolon-separated paths. Project plugins override editor plugins only when trusted.",
                             ctx, [&st, &ctx]()
                             {
                                 (void)InputTextControl("##disc-path", st.runtime.discoveryPaths, 128, ctx.theme.fonts);
                             });
            PluginSettingRow("Load Order Policy",
                             "Defines how editor, project, vendor, and local-development plugins are resolved.",
                             ctx, [&st, &ctx]()
                             {
                                 static constexpr std::array kOrders = {
                                     "Project overrides editor if trusted", "Editor plugins first",
                                     "Project plugins first", "Locked by project manifest"
                                 };
                                 (void)ComboControl("##order", &st.runtime.loadOrder, kOrders.data(), 4,
                                                    ctx.theme.fonts);
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
                             ctx, [&st, &ctx]()
                             {
                                 static constexpr std::array kPolicies = {
                                     "Block by default",
                                     "Allow after warning",
                                     "Allow local development only"
                                 };
                                 (void)ComboControl("##unsigned", &st.runtime.unsignedPolicy, kPolicies.data(), 3,
                                                    ctx.theme.fonts);
                             });
            PluginSettingRow("Network Access Policy",
                             "Default network behavior for plugins unless a plugin-specific permission overrides it.",
                             ctx, [&st, &ctx]()
                             {
                                 static constexpr std::array kNets = {
                                     "Deny by default", "Localhost only",
                                     "Prompt per plugin",
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
                             "How strictly plugin API versions are validated when opening a project.", ctx,
                             [&st, &ctx]()
                             {
                                 static constexpr std::array kModes = {
                                     "Strict semantic versioning",
                                     "Allow compatible minors",
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
                                  ImColor{Theme::Accent().x, Theme::Accent().y, Theme::Accent().z, 0.06F},
                                  Layout::Radius);
                dl->AddRect(p, {p.x + noteW, p.y + noteH}, Theme::U32(Theme::Border()), Layout::Radius);

                ImGui::SetCursorScreenPos({p.x + 12.0F, p.y + 10.0F});
                ScopedTextStyle ts(ctx.theme.fonts.sans, 11.5F, Theme::FontPx::Sans);
                ImGui::PushStyleColor(ImGuiCol_Text, Theme::Muted());
                ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + noteW - 26.0F);
                ImGui::TextWrapped(
                    "Runtime settings are editor-wide defaults. Individual plugin settings live inside each plugin detail panel and can override these defaults only when the permission model allows it.");
                ImGui::PopTextWrapPos();
                ImGui::PopStyleColor();
                ImGui::SetCursorScreenPos({p.x, p.y + noteH + 4.0F});
            }
        }

        void DrawContent(SettingsState& st, const EditorGuiContext& ctx, const float bodyH)
        {
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{26.0F, 22.0F});
            ImGui::PushStyleColor(ImGuiCol_ChildBg, Theme::Bg1());
            ImGui::BeginChild("SettingsContent", {0.0F, bodyH}, false,
                              ImGuiWindowFlags_AlwaysUseWindowPadding |
                              ImGuiWindowFlags_NoScrollbar |
                              ImGuiWindowFlags_NoScrollWithMouse);

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

            st.targetBodyH = ImGui::GetCursorPosY() + 22.0F;

            ImGui::EndChild();
            ImGui::PopStyleColor();
            ImGui::PopStyleVar();
        }

        [[nodiscard]] bool DrawFooter(SettingsState& st,
                                      EditorSettingsService& settings,
                                      const EditorGuiContext& ctx)
        {
            ImGui::PushStyleColor(ImGuiCol_ChildBg, Theme::Bg0());
            ImGui::BeginChild("SettingsFooter", {0.0F, Layout::FooterH}, false,
                              ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
            const ImVec2 p = ImGui::GetWindowPos();
            ImGui::GetWindowDrawList()->AddLine(p, {p.x + ImGui::GetWindowWidth(), p.y}, Theme::U32(Theme::Border()),
                                                1.0F);

            ImGui::SetCursorPos({22.0F, 22.0F});
            if (st.dirty)
            {
                ScopedTextStyle badge(ctx.theme.fonts.mono, 10.5F, Theme::FontPx::Mono);
                ImGui::PushStyleColor(ImGuiCol_Text, Theme::Warn());
                ImGui::TextUnformatted("unsaved");
                ImGui::PopStyleColor();
                ImGui::SameLine(0.0F, 8.0F);
            }
            {
                ScopedTextStyle hint(ctx.theme.fonts.mono, 11.5F, Theme::FontPx::Mono);
                ImGui::PushStyleColor(ImGuiCol_Text, st.statusIsError ? Theme::Err() : Theme::Dim());
                ImGui::TextUnformatted(st.statusMessage.empty()
                                           ? "Apply writes user preferences to ~/.horo/editor_settings.json"
                                           : st.statusMessage.c_str());
                ImGui::PopStyleColor();
            }

            // ── Feedback line (one-shot modal notifications) ──────────
            if (!st.modalFeedback.empty())
            {
                ImGui::SetCursorPosX(22.0F);
                ImGui::PushStyleColor(ImGuiCol_Text, Theme::Accent());
                {
                    ScopedTextStyle ts(ctx.theme.fonts.mono, 11.0F, Theme::FontPx::Mono);
                    ImGui::TextUnformatted(st.modalFeedback.c_str());
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
            if (const std::string restoreDefaults = ctx.localization.Get("editor", "settings.restore_defaults");
                Button({
                    .label = restoreDefaults.c_str(), .size = {restoreW, 34.0F},
                    .variant = Ui::ButtonVariant::Secondary, .fontSize = 13.0F, .font = ctx.theme.fonts.mono,
                    .baseFontSize = Theme::FontPx::Mono
                }))
            {
                LOG_INFO("editor.settings", "Restore Defaults clicked — draft reset to factory defaults.");
                ApplySettingsToDraft(st, DefaultEditorSettings());
                st.statusMessage = "Defaults loaded into draft. Apply to persist.";
                st.statusIsError = false;
            }
            ImGui::SameLine(0.0F, gap);
            if (Button({
                .label = ctx.localization.Get("editor", "settings.cancel").c_str(), .size = {cancelW, 34.0F},
                .variant = Ui::ButtonVariant::Secondary, .fontSize = 13.0F, .font = ctx.theme.fonts.mono,
                .baseFontSize = Theme::FontPx::Mono
            }))
            {
                LOG_INFO("editor.settings", "Settings cancelled by user (dirty=%s).",
                         st.dirty ? "yes" : "no");
                requestClose = true;
            }
            ImGui::SameLine(0.0F, gap);
            if (Button({
                .label = ctx.localization.Get("editor", "settings.apply").c_str(), .size = {applyW, 34.0F},
                .variant = Ui::ButtonVariant::Primary, .fontSize = 13.0F, .font = ctx.theme.fonts.mono,
                .baseFontSize = Theme::FontPx::Mono
            }))
            {
                (void)ApplySettings(st, settings);
                LOG_INFO("editor.settings", "Settings applied via Apply button.");
            }

            ImGui::EndChild();
            ImGui::PopStyleColor();
            return requestClose;
        }

        [[nodiscard]] ModalFrameResult DrawSettingsModalPresentationImpl(SettingsState& st,
                                                                         EditorSettingsService& settings,
                                                                         const EditorGuiContext& ctx,
                                                                         const ::ImTextureID logo)
        {
            if (st.appearance.pendingThemeIndex >= 0)
            {
                Theme::SelectThemeByIndex(st.appearance.pendingThemeIndex);
                st.appearance.pendingThemeIndex = -1;
            }

            st.modalFeedback.clear();

            const ImGuiViewport* vp = ImGui::GetMainViewport();
            const float modalW = std::min(Layout::ModalW, std::max(360.0F, vp->WorkSize.x - Layout::ViewportPad));
            float desiredModalH = Layout::ModalH;
            if (st.targetBodyH > 0.0F)
            {
                const float exactContentH = st.targetBodyH + Layout::HeaderH + Layout::FooterH;
                if (st.activeTab == static_cast<int>(SettingsTab::Plugins))
                {
                    desiredModalH = exactContentH;
                }
                else
                {
                    desiredModalH = std::max(Layout::ModalH, exactContentH);
                }
            }
            const float modalH = std::min(desiredModalH, std::max(360.0F, vp->WorkSize.y - Layout::ViewportPad));
            const ImVec2 modalPos{
                vp->WorkPos.x + (vp->WorkSize.x - modalW) * 0.5F,
                vp->WorkPos.y + (vp->WorkSize.y - modalH) * 0.5F
            };

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
            const bool headerRequestedClose = DrawHeader(st, ctx, logo);
            const float bodyH = ImGui::GetWindowHeight() - Layout::HeaderH - Layout::FooterH;
            DrawNavigation(st, ctx, bodyH);
            ImGui::SameLine(0.0F, 0.0F);
            DrawContent(st, ctx, bodyH);
            st.dirty = CollectDraftSettings(st) != st.committed;
            const bool footerRequestedClose = DrawFooter(st, settings, ctx);
            ImGui::End();
            ImGui::PopStyleColor(2);
            ImGui::PopStyleVar(3);

            return (headerRequestedClose || footerRequestedClose)
                       ? ModalFrameResult::RequestClose(ModalCloseReason::Cancelled)
                       : ModalFrameResult::None();
        }
    } // namespace

    ModalFrameResult DrawSettingsModalPresentation(SettingsState& state,
                                                   EditorSettingsService& settings,
                                                   const EditorGuiContext& ctx,
                                                   const ::ImTextureID logo)
    {
        return DrawSettingsModalPresentationImpl(state, settings, ctx, logo);
    }

    ModalFrameResult SettingsModal::Draw()
    {
        return DrawSettingsModalPresentation(m_draft, m_settings, m_context, static_cast<::ImTextureID>(m_logo));
    }
} // namespace Horo::Editor
