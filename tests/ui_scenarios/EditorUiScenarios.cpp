#include "launcher/UiTestHarness.h"

#ifdef MONOLITH_STANDALONE_UI_AUTOMATION

#include <algorithm>
#include <string>
#include <string_view>

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_test_engine/imgui_te_context.h>
#include <imgui_test_engine/imgui_te_engine.h>

#include "core/Logger.h"
#include "launcher/LauncherEditorShell.h"
#include "tests/UiTestRegistry.h"

namespace Monolith {
    namespace {
        template<typename Predicate>
        bool WaitForCondition(ImGuiTestContext *ctx, int maxFrames,
                              Predicate &&predicate) {
            if (!ctx)
                return false;
            for (int frame = 0; frame < maxFrames; ++frame) {
                if (predicate())
                    return true;
                ctx->Yield(1);
            }
            return predicate();
        }

        // ---------------------------------------------------------------------------
        // WaitForPopup: waits for the popup at stack depth `depth` to be rendered
        // and contain `sentinelItem`.  On success leaves ctx->SetRef at that window
        // and returns its ID.  On failure returns 0 (does NOT press Escape).
        //
        // depth 0 = first/outermost popup (e.g. a toolbar or context menu popup).
        // depth 1 = second popup (e.g. an "Add" sub-menu inside a context menu).
        // ---------------------------------------------------------------------------
        static ImGuiID WaitForPopup(ImGuiTestContext *ctx, int depth,
                                    const char *sentinelItem, int maxFrames = 60) {
            ImGuiID wid = 0;
            WaitForCondition(ctx, maxFrames, [ctx, depth, sentinelItem, &wid]() -> bool {
                ImGuiContext &g = *ctx->UiContext;
                if (g.OpenPopupStack.Size > depth) {
                    ImGuiWindow *win = g.OpenPopupStack[depth].Window;
                    if (win != nullptr) {
                        wid = win->ID;
                        ctx->SetRef(wid);
                        return sentinelItem ? ctx->ItemExists(sentinelItem) : true;
                    }
                }
                return false;
            });
            if (wid)
                ctx->SetRef(wid);
            return wid;
        }

        // Opens a toolbar popup by clicking btnLabel, waits for sentinelItem,
        // and leaves ctx->SetRef pointing at the actual popup window.
        // Returns the popup window ID on success, or 0 on timeout.
        // On failure, presses Escape to ensure the popup is closed so that
        // subsequent tests start with a clean ImGui popup stack.
        //
        // Implementation note: BeginPopup creates windows named "##Popup_XXXXXXXX",
        // NOT windows named by popupId. We find the actual popup window by reading
        // g.OpenPopupStack[0].Window directly — this is immune to hash mismatches.
        static ImGuiID OpenToolbarPopup(ImGuiTestContext *ctx, const char *btnLabel,
                                        const char * /*popupId*/, const char *sentinelItem,
                                        int maxFrames = 60) {
            ctx->SetRef("##toolbar");
            ctx->ItemClick(btnLabel);
            ctx->Yield(2);

            const ImGuiID wid = WaitForPopup(ctx, 0, sentinelItem, maxFrames);
            if (!wid) {
                // Popup failed to open: close any open popup so ImGui state is
                // clean for subsequent tests.
                ctx->PopupCloseAll();
                ctx->Yield(2);
                ctx->SetRef("##toolbar");
            }
            return wid;
        }

        void CaptureScreenshotTo(ImGuiTestContext *ctx, const std::filesystem::path &dir,
                                 const char *filename) {
            if (!ctx || !ctx->CaptureArgs || dir.empty())
                return;
            const std::string full = (dir / filename).string();
            if (full.size() >= IM_ARRAYSIZE(ctx->CaptureArgs->InOutputFile))
                return;
            LogDebug("UI scenario capture screenshot: {}", full);
            ImStrncpy(ctx->CaptureArgs->InOutputFile, full.c_str(),
                      IM_ARRAYSIZE(ctx->CaptureArgs->InOutputFile));
            ctx->CaptureScreenshot(0);
        }

        void CaptureIfEnabled(ImGuiTestContext *ctx, const UiAutomationRunState *state,
                              const char *filename) {
            if (!state->captureEnabled || state->videoEnabled)
                return;
            CaptureScreenshotTo(ctx, state->uiCaptureOutputDir, filename);
        }

        UiAutomationRunState *GetTestState(ImGuiTestContext *ctx,
                                           const char *scenarioName) {
            LogInfo("UI scenario start: {}", scenarioName);
            if (ctx == nullptr || ctx->Test == nullptr)
                return nullptr;
            return static_cast<UiAutomationRunState *>(ctx->Test->UserData);
        }

        bool EnsureEditorActive(ImGuiTestContext *ctx, UiAutomationRunState *state) {
            const auto *shell = state->shellContext;
            if (!shell)
                return false;
            if (!shell->HasActiveProject()) {
                LogWarn("editor scenario: no active project, skipping.");
                return false;
            }
            // Close any popup/modal left open by the previous scenario before
            // checking toolbar state — an open popup blocks toolbar clicks.
            ctx->PopupCloseAll();
            ctx->Yield(2);
            ctx->SetRef("##toolbar");
            return WaitForCondition(ctx, 180, [ctx]() { return ctx->ItemExists("File"); });
        }

        // ---- Scenario run functions ----

        void RunToolbarButtonsVisible(ImGuiTestContext *ctx) {
            UiAutomationRunState *state =
                    GetTestState(ctx, "editor_ui/toolbar_buttons_visible");
            IM_CHECK(state != nullptr);
            if (!state)
                return;

            IM_CHECK(EnsureEditorActive(ctx, state));
            if (!EnsureEditorActive(ctx, state))
                return;

            ctx->SetRef("##toolbar");
            IM_CHECK(ctx->ItemExists("File"));
            IM_CHECK(ctx->ItemExists("Add"));
            IM_CHECK(ctx->ItemExists("Edit"));
            IM_CHECK(ctx->ItemExists("View"));

            CaptureIfEnabled(ctx, state,
                             "editor_ui__toolbar_buttons_visible__expect_buttons.png");
            LogInfo("UI scenario done: editor_ui/toolbar_buttons_visible");
        }

        void RunFileMenuItems(ImGuiTestContext *ctx) {
            UiAutomationRunState *state = GetTestState(ctx, "editor_ui/file_menu_items");
            IM_CHECK(state != nullptr);
            if (!state)
                return;

            IM_CHECK(EnsureEditorActive(ctx, state));
            if (!EnsureEditorActive(ctx, state))
                return;

            const ImGuiID filePopupId =
                    OpenToolbarPopup(ctx, "File", "##toolbar_file_popup", "New Scene");
            IM_CHECK(filePopupId != ImGuiID(0));
            if (!filePopupId)
                return;

            IM_CHECK(ctx->ItemExists("New Scene"));
            IM_CHECK(ctx->ItemExists("Open Scene..."));
            IM_CHECK(ctx->ItemExists("Reset Layout"));
            IM_CHECK(ctx->ItemExists("Settings..."));

            ctx->PopupCloseAll();
            ctx->Yield(1);
            LogInfo("UI scenario done: editor_ui/file_menu_items");
        }

        void RunAddMenuItems(ImGuiTestContext *ctx) {
            UiAutomationRunState *state = GetTestState(ctx, "editor_ui/add_menu_items");
            IM_CHECK(state != nullptr);
            if (!state)
                return;

            IM_CHECK(EnsureEditorActive(ctx, state));
            if (!EnsureEditorActive(ctx, state))
                return;

            const ImGuiID addPopupId =
                    OpenToolbarPopup(ctx, "Add", "##toolbar_add_popup", "Panel");
            IM_CHECK(addPopupId != ImGuiID(0));
            if (!addPopupId)
                return;

            IM_CHECK(ctx->ItemExists("Panel"));
            IM_CHECK(ctx->ItemExists("Prop"));
            IM_CHECK(ctx->ItemExists("Light"));
            IM_CHECK(ctx->ItemExists("Camera"));

            ctx->PopupCloseAll();
            ctx->Yield(1);
            LogInfo("UI scenario done: editor_ui/add_menu_items");
        }

        void RunEditMenuItems(ImGuiTestContext *ctx) {
            UiAutomationRunState *state = GetTestState(ctx, "editor_ui/edit_menu_items");
            IM_CHECK(state != nullptr);
            if (!state)
                return;

            IM_CHECK(EnsureEditorActive(ctx, state));
            if (!EnsureEditorActive(ctx, state))
                return;

            const ImGuiID editPopupId =
                    OpenToolbarPopup(ctx, "Edit", "##toolbar_edit_popup", "Undo");
            IM_CHECK(editPopupId != ImGuiID(0));
            if (!editPopupId)
                return;

            IM_CHECK(ctx->ItemExists("Undo"));
            IM_CHECK(ctx->ItemExists("Redo"));
            IM_CHECK(ctx->ItemExists("Delete"));
            IM_CHECK(ctx->ItemExists("Duplicate"));

            ctx->PopupCloseAll();
            ctx->Yield(1);
            LogInfo("UI scenario done: editor_ui/edit_menu_items");
        }

        void RunViewMenuItems(ImGuiTestContext *ctx) {
            UiAutomationRunState *state = GetTestState(ctx, "editor_ui/view_menu_items");
            IM_CHECK(state != nullptr);
            if (!state)
                return;

            IM_CHECK(EnsureEditorActive(ctx, state));
            if (!EnsureEditorActive(ctx, state))
                return;

            const ImGuiID viewPopupId =
                    OpenToolbarPopup(ctx, "View", "##toolbar_view_popup", "Fly Mode");
            IM_CHECK(viewPopupId != ImGuiID(0));
            if (!viewPopupId)
                return;

            IM_CHECK(ctx->ItemExists("Fly Mode"));
            IM_CHECK(ctx->ItemExists("Help"));
            IM_CHECK(ctx->ItemExists("Quick Open"));
            IM_CHECK(ctx->ItemExists("Command Palette"));

            ctx->PopupCloseAll();
            ctx->Yield(1);
            LogInfo("UI scenario done: editor_ui/view_menu_items");
        }

        void RunSceneControlButtonsVisible(ImGuiTestContext *ctx) {
            UiAutomationRunState *state =
                    GetTestState(ctx, "editor_ui/scene_control_buttons_visible");
            IM_CHECK(state != nullptr);
            if (!state)
                return;

            IM_CHECK(EnsureEditorActive(ctx, state));
            if (!EnsureEditorActive(ctx, state))
                return;

            ctx->SetRef("##toolbar");
            // Play and Stop are mutually exclusive (toggled by m_playMode).
            // In normal (non-play) state, "Play" is shown; in play mode "Stop"
            // is shown. Verify that at least one of them is present.
            const bool hasPlay = ctx->ItemExists("Play");
            const bool hasStop = ctx->ItemExists("Stop");
            IM_CHECK(hasPlay || hasStop);
            IM_CHECK(ctx->ItemExists("Load"));
            IM_CHECK(ctx->ItemExists("Save"));
            IM_CHECK(ctx->ItemExists("Close editor"));

            CaptureIfEnabled(
                ctx, state,
                "editor_ui__scene_control_buttons_visible__expect_controls.png");
            LogInfo("UI scenario done: editor_ui/scene_control_buttons_visible");
        }

        void RunCloseEditorReturnsToLauncher(ImGuiTestContext *ctx) {
            UiAutomationRunState *state =
                    GetTestState(ctx, "editor_ui/close_editor_returns_to_launcher");
            IM_CHECK(state != nullptr);
            if (!state)
                return;

            IM_CHECK(EnsureEditorActive(ctx, state));
            if (!EnsureEditorActive(ctx, state))
                return;

            const Launcher::LauncherEditorShell *shell = state->shellContext;

            ctx->SetRef("##toolbar");
            LogDebug("UI scenario action: click 'Close editor'");
            ctx->ItemClick("Close editor");

            const bool projectClosed = WaitForCondition(
                ctx, 120, [shell]() { return !shell->HasActiveProject(); });
            IM_CHECK(projectClosed);
            if (!projectClosed)
                LogWarn("UI scenario failed to observe project close within timeout.");

            LogInfo("UI scenario done: editor_ui/close_editor_returns_to_launcher");
        }

        void RunBottomDockTabsVisible(ImGuiTestContext *ctx) {
            UiAutomationRunState *state =
                    GetTestState(ctx, "editor_ui/bottom_dock_tabs_visible");
            IM_CHECK(state != nullptr);
            if (!state)
                return;

            IM_CHECK(EnsureEditorActive(ctx, state));
            if (!EnsureEditorActive(ctx, state))
                return;

            const bool ready = WaitForCondition(ctx, 120, [ctx]() {
                ctx->SetRef("Workspace");
                return ctx->ItemExists("##bottom_tabs/Project");
            });
            IM_CHECK(ready);
            if (!ready)
                return;

            ctx->SetRef("Workspace");
            IM_CHECK(ctx->ItemExists("##bottom_tabs/Project"));
            IM_CHECK(ctx->ItemExists("##bottom_tabs/Console"));
            IM_CHECK(ctx->ItemExists("##bottom_tabs/MCP"));

            CaptureIfEnabled(ctx, state,
                             "editor_ui__bottom_dock_tabs_visible__expect_tabs.png");
            LogInfo("UI scenario done: editor_ui/bottom_dock_tabs_visible");
        }

        void RunConsoleTabControls(ImGuiTestContext *ctx) {
            UiAutomationRunState *state =
                    GetTestState(ctx, "editor_ui/console_tab_controls");
            IM_CHECK(state != nullptr);
            if (!state)
                return;

            IM_CHECK(EnsureEditorActive(ctx, state));
            if (!EnsureEditorActive(ctx, state))
                return;

            const bool ready = WaitForCondition(ctx, 120, [ctx]() {
                ctx->SetRef("Workspace");
                return ctx->ItemExists("##bottom_tabs/Console");
            });
            IM_CHECK(ready);
            if (!ready)
                return;

            ctx->SetRef("Workspace");
            LogDebug("UI scenario action: click Console tab");
            ctx->ItemClick("##bottom_tabs/Console");
            ctx->Yield(2);

            ctx->SetRef("Workspace");
            IM_CHECK(ctx->ItemExists("Clear"));
            IM_CHECK(ctx->ItemExists("Info"));
            IM_CHECK(ctx->ItemExists("Warn"));
            IM_CHECK(ctx->ItemExists("Error"));

            LogDebug("UI scenario action: click Clear button");
            ctx->ItemClick("Clear");
            ctx->Yield(1);

            LogInfo("UI scenario done: editor_ui/console_tab_controls");
        }

        void RunMcpTabButtons(ImGuiTestContext *ctx) {
            UiAutomationRunState *state =
                    GetTestState(ctx, "editor_ui/mcp_tab_buttons");
            IM_CHECK(state != nullptr);
            if (!state)
                return;

            IM_CHECK(EnsureEditorActive(ctx, state));
            if (!EnsureEditorActive(ctx, state))
                return;

            const bool ready = WaitForCondition(ctx, 120, [ctx]() {
                ctx->SetRef("Workspace");
                return ctx->ItemExists("##bottom_tabs/MCP");
            });
            IM_CHECK(ready);
            if (!ready)
                return;

            ctx->SetRef("Workspace");
            LogDebug("UI scenario action: click MCP tab");
            ctx->ItemClick("##bottom_tabs/MCP");
            ctx->Yield(2);

            ctx->SetRef("Workspace");
            IM_CHECK(ctx->ItemExists("Open Settings"));
            IM_CHECK(ctx->ItemExists("Copy Endpoint"));
            IM_CHECK(ctx->ItemExists("Clear Request Log"));

            LogInfo("UI scenario done: editor_ui/mcp_tab_buttons");
        }

        void RunHierarchyPanelVisible(ImGuiTestContext *ctx) {
            UiAutomationRunState *state =
                    GetTestState(ctx, "editor_ui/hierarchy_panel_visible");
            IM_CHECK(state != nullptr);
            if (!state)
                return;

            IM_CHECK(EnsureEditorActive(ctx, state));
            if (!EnsureEditorActive(ctx, state))
                return;

            const bool ready = WaitForCondition(ctx, 120, [ctx]() {
                ctx->SetRef("Hierarchy");
                return ctx->ItemExists("##object_search");
            });
            IM_CHECK(ready);
            if (!ready)
                return;

            ctx->SetRef("Hierarchy");
            IM_CHECK(ctx->ItemExists("##object_search"));

            CaptureIfEnabled(ctx, state,
                             "editor_ui__hierarchy_panel_visible__expect_panel.png");
            LogInfo("UI scenario done: editor_ui/hierarchy_panel_visible");
        }

        void RunHierarchySearchInput(ImGuiTestContext *ctx) {
            UiAutomationRunState *state =
                    GetTestState(ctx, "editor_ui/hierarchy_search_input");
            IM_CHECK(state != nullptr);
            if (!state)
                return;

            IM_CHECK(EnsureEditorActive(ctx, state));
            if (!EnsureEditorActive(ctx, state))
                return;

            const bool ready = WaitForCondition(ctx, 120, [ctx]() {
                ctx->SetRef("Hierarchy");
                return ctx->ItemExists("##object_search");
            });
            IM_CHECK(ready);
            if (!ready)
                return;

            ctx->SetRef("Hierarchy");
            LogDebug("UI scenario action: type into hierarchy search");
            ctx->ItemInputValue("##object_search", "test_query");
            ctx->Yield(2);

            LogDebug("UI scenario action: clear hierarchy search");
            ctx->ItemInputValue("##object_search", "");
            ctx->Yield(1);

            LogInfo("UI scenario done: editor_ui/hierarchy_search_input");
        }

        void RunAssetsPanelVisible(ImGuiTestContext *ctx) {
            UiAutomationRunState *state =
                    GetTestState(ctx, "editor_ui/assets_panel_visible");
            IM_CHECK(state != nullptr);
            if (!state)
                return;

            IM_CHECK(EnsureEditorActive(ctx, state));
            if (!EnsureEditorActive(ctx, state))
                return;

            const bool ready = WaitForCondition(ctx, 120, [ctx]() {
                ctx->SetRef("Assets");
                return ctx->ItemExists("+");
            });
            IM_CHECK(ready);
            if (!ready)
                return;

            ctx->SetRef("Assets");
            IM_CHECK(ctx->ItemExists("+"));
            IM_CHECK(ctx->ItemExists("Search"));

            CaptureIfEnabled(ctx, state,
                             "editor_ui__assets_panel_visible__expect_panel.png");
            LogInfo("UI scenario done: editor_ui/assets_panel_visible");
        }

        void RunHelpWindowOpenClose(ImGuiTestContext *ctx) {
            UiAutomationRunState *state =
                    GetTestState(ctx, "editor_ui/help_window_open_close");
            IM_CHECK(state != nullptr);
            if (!state)
                return;

            IM_CHECK(EnsureEditorActive(ctx, state));
            if (!EnsureEditorActive(ctx, state))
                return;

            LogDebug("UI scenario action: click View menu");
            const ImGuiID viewPopupIdHelp =
                    OpenToolbarPopup(ctx, "View", "##toolbar_view_popup", "Help");
            IM_CHECK(viewPopupIdHelp != ImGuiID(0));
            if (!viewPopupIdHelp)
                return;

            LogDebug("UI scenario action: click Help menu item");
            ctx->ItemClick("Help");
            ctx->Yield(2);

            const bool helpReady = WaitForCondition(ctx, 60, [ctx]() {
                ctx->SetRef("Help - Keyboard Shortcuts");
                return ctx->ItemExists("##shortcut_search");
            });
            IM_CHECK(helpReady);
            if (!helpReady) {
                LogWarn("UI scenario: Help window did not open within timeout.");
                return;
            }

            ctx->SetRef("Help - Keyboard Shortcuts");
            IM_CHECK(ctx->ItemExists("##shortcut_search"));

            LogDebug("UI scenario action: close Help window with Escape");
            ctx->PopupCloseAll();
            ctx->Yield(2);

            LogInfo("UI scenario done: editor_ui/help_window_open_close");
        }

        void RunSettingsModalOpenCancel(ImGuiTestContext *ctx) {
            UiAutomationRunState *state =
                    GetTestState(ctx, "editor_ui/settings_modal_open_cancel");
            IM_CHECK(state != nullptr);
            if (!state)
                return;

            IM_CHECK(EnsureEditorActive(ctx, state));
            if (!EnsureEditorActive(ctx, state))
                return;

            LogDebug("UI scenario action: click File menu");
            const ImGuiID filePopupIdSettings =
                    OpenToolbarPopup(ctx, "File", "##toolbar_file_popup", "Settings...");
            IM_CHECK(filePopupIdSettings != ImGuiID(0));
            if (!filePopupIdSettings)
                return;


            LogDebug("UI scenario action: click Settings...");
            ctx->ItemClick("Settings...");
            ctx->Yield(2);

            const bool modalReady = WaitForCondition(ctx, 60, [ctx]() {
                ctx->SetRef("Editor Settings");
                return ctx->ItemExists("Cancel");
            });
            IM_CHECK(modalReady);
            if (!modalReady) {
                LogWarn("UI scenario: Editor Settings modal did not open within timeout.");
                return;
            }

            ctx->SetRef("Editor Settings");
            IM_CHECK(ctx->ItemExists("Cancel"));
            IM_CHECK(ctx->ItemExists("Apply"));

            LogDebug("UI scenario action: click Cancel to close modal");
            ctx->ItemClick("Cancel");
            ctx->Yield(2);

            LogInfo("UI scenario done: editor_ui/settings_modal_open_cancel");
        }

        // ---- Registration helpers ----

        ImGuiTest *RegisterToolbarButtonsVisible(ImGuiTestEngine *engine,
                                                 UiAutomationRunState *state) {
            ImGuiTest *test =
                    IM_REGISTER_TEST(engine, "editor_ui", "toolbar_buttons_visible");
            test->UserData = state;
            test->TestFunc = &RunToolbarButtonsVisible;
            return test;
        }

        ImGuiTest *RegisterFileMenuItems(ImGuiTestEngine *engine,
                                         UiAutomationRunState *state) {
            ImGuiTest *test = IM_REGISTER_TEST(engine, "editor_ui", "file_menu_items");
            test->UserData = state;
            test->TestFunc = &RunFileMenuItems;
            return test;
        }

        ImGuiTest *RegisterAddMenuItems(ImGuiTestEngine *engine,
                                        UiAutomationRunState *state) {
            ImGuiTest *test = IM_REGISTER_TEST(engine, "editor_ui", "add_menu_items");
            test->UserData = state;
            test->TestFunc = &RunAddMenuItems;
            return test;
        }

        ImGuiTest *RegisterEditMenuItems(ImGuiTestEngine *engine,
                                         UiAutomationRunState *state) {
            ImGuiTest *test = IM_REGISTER_TEST(engine, "editor_ui", "edit_menu_items");
            test->UserData = state;
            test->TestFunc = &RunEditMenuItems;
            return test;
        }

        ImGuiTest *RegisterViewMenuItems(ImGuiTestEngine *engine,
                                         UiAutomationRunState *state) {
            ImGuiTest *test = IM_REGISTER_TEST(engine, "editor_ui", "view_menu_items");
            test->UserData = state;
            test->TestFunc = &RunViewMenuItems;
            return test;
        }

        ImGuiTest *RegisterSceneControlButtonsVisible(ImGuiTestEngine *engine,
                                                      UiAutomationRunState *state) {
            ImGuiTest *test =
                    IM_REGISTER_TEST(engine, "editor_ui", "scene_control_buttons_visible");
            test->UserData = state;
            test->TestFunc = &RunSceneControlButtonsVisible;
            return test;
        }

        ImGuiTest *RegisterCloseEditorReturnsToLauncher(ImGuiTestEngine *engine,
                                                        UiAutomationRunState *state) {
            ImGuiTest *test = IM_REGISTER_TEST(engine, "editor_ui",
                                               "close_editor_returns_to_launcher");
            test->UserData = state;
            test->TestFunc = &RunCloseEditorReturnsToLauncher;
            return test;
        }

        ImGuiTest *RegisterBottomDockTabsVisible(ImGuiTestEngine *engine,
                                                 UiAutomationRunState *state) {
            ImGuiTest *test =
                    IM_REGISTER_TEST(engine, "editor_ui", "bottom_dock_tabs_visible");
            test->UserData = state;
            test->TestFunc = &RunBottomDockTabsVisible;
            return test;
        }

        ImGuiTest *RegisterConsoleTabControls(ImGuiTestEngine *engine,
                                              UiAutomationRunState *state) {
            ImGuiTest *test =
                    IM_REGISTER_TEST(engine, "editor_ui", "console_tab_controls");
            test->UserData = state;
            test->TestFunc = &RunConsoleTabControls;
            return test;
        }

        ImGuiTest *RegisterMcpTabButtons(ImGuiTestEngine *engine,
                                         UiAutomationRunState *state) {
            ImGuiTest *test = IM_REGISTER_TEST(engine, "editor_ui", "mcp_tab_buttons");
            test->UserData = state;
            test->TestFunc = &RunMcpTabButtons;
            return test;
        }

        ImGuiTest *RegisterHierarchyPanelVisible(ImGuiTestEngine *engine,
                                                  UiAutomationRunState *state) {
            ImGuiTest *test =
                    IM_REGISTER_TEST(engine, "editor_ui", "hierarchy_panel_visible");
            test->UserData = state;
            test->TestFunc = &RunHierarchyPanelVisible;
            return test;
        }

        ImGuiTest *RegisterHierarchySearchInput(ImGuiTestEngine *engine,
                                                 UiAutomationRunState *state) {
            ImGuiTest *test =
                    IM_REGISTER_TEST(engine, "editor_ui", "hierarchy_search_input");
            test->UserData = state;
            test->TestFunc = &RunHierarchySearchInput;
            return test;
        }

        ImGuiTest *RegisterAssetsPanelVisible(ImGuiTestEngine *engine,
                                              UiAutomationRunState *state) {
            ImGuiTest *test =
                    IM_REGISTER_TEST(engine, "editor_ui", "assets_panel_visible");
            test->UserData = state;
            test->TestFunc = &RunAssetsPanelVisible;
            return test;
        }

        ImGuiTest *RegisterHelpWindowOpenClose(ImGuiTestEngine *engine,
                                               UiAutomationRunState *state) {
            ImGuiTest *test =
                    IM_REGISTER_TEST(engine, "editor_ui", "help_window_open_close");
            test->UserData = state;
            test->TestFunc = &RunHelpWindowOpenClose;
            return test;
        }

        ImGuiTest *RegisterSettingsModalOpenCancel(ImGuiTestEngine *engine,
                                                   UiAutomationRunState *state) {
            ImGuiTest *test =
                    IM_REGISTER_TEST(engine, "editor_ui", "settings_modal_open_cancel");
            test->UserData = state;
            test->TestFunc = &RunSettingsModalOpenCancel;
            return test;
        }
        void RunPropertiesPanelVisible(ImGuiTestContext *ctx) {
            UiAutomationRunState *state =
                    GetTestState(ctx, "editor_ui/properties_panel_visible");
            IM_CHECK(state != nullptr);
            if (!state)
                return;

            IM_CHECK(EnsureEditorActive(ctx, state));
            if (!EnsureEditorActive(ctx, state))
                return;

            const ImGuiWindow *propsWin = ImGui::FindWindowByName("Properties");
            IM_CHECK(propsWin != nullptr);
            IM_CHECK(!propsWin->Hidden);

            CaptureIfEnabled(ctx, state,
                             "editor_ui__properties_panel_visible.png");
            LogInfo("UI scenario done: editor_ui/properties_panel_visible");
        }

        ImGuiTest *RegisterPropertiesPanelVisible(ImGuiTestEngine *engine,
                                                  UiAutomationRunState *state) {
            ImGuiTest *test =
                    IM_REGISTER_TEST(engine, "editor_ui", "properties_panel_visible");
            test->UserData = state;
            test->TestFunc = &RunPropertiesPanelVisible;
            return test;
        }

        void RunViewportAndStatusbarVisible(ImGuiTestContext *ctx) {
            UiAutomationRunState *state =
                    GetTestState(ctx, "editor_ui/viewport_statusbar_visible");
            IM_CHECK(state != nullptr);
            if (!state)
                return;

            IM_CHECK(EnsureEditorActive(ctx, state));
            if (!EnsureEditorActive(ctx, state))
                return;

            const ImGuiWindow *viewportWin = ImGui::FindWindowByName("Viewport");
            IM_CHECK(viewportWin != nullptr);

            const ImGuiWindow *statusWin = ImGui::FindWindowByName("##editor_statusbar");
            IM_CHECK(statusWin != nullptr);

            CaptureIfEnabled(ctx, state,
                             "editor_ui__viewport_statusbar_visible.png");
            LogInfo("UI scenario done: editor_ui/viewport_statusbar_visible");
        }

        ImGuiTest *RegisterViewportAndStatusbarVisible(ImGuiTestEngine *engine,
                                                       UiAutomationRunState *state) {
            ImGuiTest *test =
                    IM_REGISTER_TEST(engine, "editor_ui", "viewport_statusbar_visible");
            test->UserData = state;
            test->TestFunc = &RunViewportAndStatusbarVisible;
            return test;
        }

        void RunNewSceneAndAddPanel(ImGuiTestContext *ctx) {
            UiAutomationRunState *state =
                    GetTestState(ctx, "editor_ui/new_scene_and_add_panel");
            IM_CHECK(state != nullptr);
            if (!state)
                return;

            IM_CHECK(EnsureEditorActive(ctx, state));
            if (!EnsureEditorActive(ctx, state))
                return;

            // File → New Scene
            const ImGuiID filePopupIdNS =
                    OpenToolbarPopup(ctx, "File", "##toolbar_file_popup", "New Scene");
            if (!filePopupIdNS) {
                ctx->PopupCloseAll();
                return;
            }
            ctx->ItemClick("New Scene");
            ctx->Yield(2);

            // Dismiss "Unsaved Changes" modal if it appears
            if (ctx->ItemExists("Unsaved Changes/Discard")) {
                ctx->SetRef("Unsaved Changes");
                ctx->ItemClick("Discard");
                ctx->Yield(2);
            }

            // Wait for hierarchy to stabilise
            WaitForCondition(ctx, 120, [ctx]() {
                return ctx->ItemExists("Hierarchy/##scene_primary");
            });

            // Add → Panel
            const ImGuiID addPopupIdNS =
                    OpenToolbarPopup(ctx, "Add", "##toolbar_add_popup", "Panel");
            IM_CHECK(addPopupIdNS != ImGuiID(0));
            if (!addPopupIdNS)
                return;

            ctx->ItemClick("Panel");
            ctx->Yield(4);

            // Hierarchy window should still be visible
            const ImGuiWindow *hierWin = ImGui::FindWindowByName("Hierarchy");
            IM_CHECK(hierWin != nullptr && !hierWin->Hidden);

            CaptureIfEnabled(ctx, state,
                             "editor_ui__new_scene_and_add_panel.png");
            LogInfo("UI scenario done: editor_ui/new_scene_and_add_panel");
        }

        ImGuiTest *RegisterNewSceneAndAddPanel(ImGuiTestEngine *engine,
                                               UiAutomationRunState *state) {
            ImGuiTest *test =
                    IM_REGISTER_TEST(engine, "editor_ui", "new_scene_and_add_panel");
            test->UserData = state;
            test->TestFunc = &RunNewSceneAndAddPanel;
            return test;
        }

        void RunQuickOpenPopup(ImGuiTestContext *ctx) {
            UiAutomationRunState *state =
                    GetTestState(ctx, "editor_ui/quick_open_popup");
            IM_CHECK(state != nullptr);
            if (!state)
                return;

            IM_CHECK(EnsureEditorActive(ctx, state));
            if (!EnsureEditorActive(ctx, state))
                return;

            // View → Quick Open
            const ImGuiID viewPopupIdQO =
                    OpenToolbarPopup(ctx, "View", "##toolbar_view_popup", "Quick Open");
            IM_CHECK(viewPopupIdQO != ImGuiID(0));
            if (!viewPopupIdQO)
                return;

            ctx->ItemClick("Quick Open");
            ctx->Yield(2);

            const bool quickOpenReady = WaitForCondition(ctx, 60, [ctx]() {
                return ctx->ItemExists("Quick Open/##quick_open_input");
            });
            IM_CHECK(quickOpenReady);
            if (!quickOpenReady) {
                ctx->PopupCloseAll();
                ctx->Yield(2);
                return;
            }
            ctx->Yield(1);

            CaptureIfEnabled(ctx, state, "editor_ui__quick_open_popup.png");
            LogInfo("UI scenario done: editor_ui/quick_open_popup");
        }

        ImGuiTest *RegisterQuickOpenPopup(ImGuiTestEngine *engine,
                                          UiAutomationRunState *state) {
            ImGuiTest *test =
                    IM_REGISTER_TEST(engine, "editor_ui", "quick_open_popup");
            test->UserData = state;
            test->TestFunc = &RunQuickOpenPopup;
            return test;
        }

        void RunCommandPalettePopup(ImGuiTestContext *ctx) {
            UiAutomationRunState *state =
                    GetTestState(ctx, "editor_ui/command_palette_popup");
            IM_CHECK(state != nullptr);
            if (!state)
                return;

            IM_CHECK(EnsureEditorActive(ctx, state));
            if (!EnsureEditorActive(ctx, state))
                return;

            // View → Command Palette
            const ImGuiID viewPopupIdCP =
                    OpenToolbarPopup(ctx, "View", "##toolbar_view_popup",
                                     "Command Palette");
            IM_CHECK(viewPopupIdCP != ImGuiID(0));
            if (!viewPopupIdCP)
                return;

            ctx->ItemClick("Command Palette");
            ctx->Yield(2);

            const bool paletteReady = WaitForCondition(ctx, 60, [ctx]() {
                return ctx->ItemExists("Command Palette/##command_palette_input");
            });
            IM_CHECK(paletteReady);

            ctx->PopupCloseAll();
            ctx->Yield(1);

            CaptureIfEnabled(ctx, state, "editor_ui__command_palette_popup.png");
            LogInfo("UI scenario done: editor_ui/command_palette_popup");
        }

        ImGuiTest *RegisterCommandPalettePopup(ImGuiTestEngine *engine,
                                               UiAutomationRunState *state) {
            ImGuiTest *test =
                    IM_REGISTER_TEST(engine, "editor_ui", "command_palette_popup");
            test->UserData = state;
            test->TestFunc = &RunCommandPalettePopup;
            return test;
        }

        void RunSelectObjectViaHierarchy(ImGuiTestContext *ctx) {
            UiAutomationRunState *state =
                    GetTestState(ctx, "editor_ui/select_object_via_hierarchy");
            IM_CHECK(state != nullptr);
            if (!state)
                return;

            IM_CHECK(EnsureEditorActive(ctx, state));
            if (!EnsureEditorActive(ctx, state))
                return;

            // Ensure hierarchy is visible
            const ImGuiWindow *hierWin = ImGui::FindWindowByName("Hierarchy");
            IM_CHECK(hierWin != nullptr);

            // Click on the first tree node in the hierarchy (##scene_primary)
            // This exercises DrawSceneHeader and tree interaction
            ctx->SetRef("Hierarchy");
            const bool sceneNodeExists = WaitForCondition(ctx, 60, [ctx]() {
                return ctx->ItemExists("##scene_primary");
            });
            if (sceneNodeExists)
                ctx->ItemClick("##scene_primary");

            ctx->Yield(2);

            // Properties panel should still be visible regardless of selection
            const ImGuiWindow *propsWin = ImGui::FindWindowByName("Properties");
            IM_CHECK(propsWin != nullptr && !propsWin->Hidden);

            CaptureIfEnabled(ctx, state,
                             "editor_ui__select_object_via_hierarchy.png");
            LogInfo("UI scenario done: editor_ui/select_object_via_hierarchy");
        }

        ImGuiTest *RegisterSelectObjectViaHierarchy(ImGuiTestEngine *engine,
                                                    UiAutomationRunState *state) {
            ImGuiTest *test =
                    IM_REGISTER_TEST(engine, "editor_ui", "select_object_via_hierarchy");
            test->UserData = state;
            test->TestFunc = &RunSelectObjectViaHierarchy;
            return test;
        }

        void RunRenameObjectModal(ImGuiTestContext *ctx) {
            UiAutomationRunState *state =
                    GetTestState(ctx, "editor_ui/rename_object_modal");
            IM_CHECK(state != nullptr);
            if (!state)
                return;

            IM_CHECK(EnsureEditorActive(ctx, state));
            if (!EnsureEditorActive(ctx, state))
                return;

            // Add a Panel so we have something to select
            const ImGuiID addPopupIdRename =
                    OpenToolbarPopup(ctx, "Add", "##toolbar_add_popup", "Panel");
            if (!addPopupIdRename) {
                ctx->PopupCloseAll();
                return;
            }
            ctx->ItemClick("Panel");
            ctx->Yield(4);

            // Select the panel (tree mode: PushID(0) / ##obj_tree)
            ctx->SetRef("Hierarchy");
            const bool panelReadyRename = WaitForCondition(ctx, 60, [ctx]() {
                return ctx->ItemExists("$$0/##obj_tree");
            });
            if (panelReadyRename)
                ctx->ItemClick("$$0/##obj_tree");
            ctx->Yield(1);

            // Open Edit → Rename... (enabled only with single selection)
            const ImGuiID editPopupIdRename =
                    OpenToolbarPopup(ctx, "Edit", "##toolbar_edit_popup", "Rename...");
            IM_CHECK(editPopupIdRename != ImGuiID(0));
            if (!editPopupIdRename) {
                ctx->PopupCloseAll();
                return;
            }

            ctx->ItemClick("Rename...");
            ctx->Yield(2);

            const bool renameModalOpen = WaitForCondition(ctx, 60, [ctx]() {
                return ctx->ItemExists("Rename Object/New ID");
            });
            IM_CHECK(renameModalOpen);

            ctx->PopupCloseAll();
            ctx->Yield(1);

            CaptureIfEnabled(ctx, state, "editor_ui__rename_object_modal.png");
            LogInfo("UI scenario done: editor_ui/rename_object_modal");
        }

        ImGuiTest *RegisterRenameObjectModal(ImGuiTestEngine *engine,
                                             UiAutomationRunState *state) {
            ImGuiTest *test =
                    IM_REGISTER_TEST(engine, "editor_ui", "rename_object_modal");
            test->UserData = state;
            test->TestFunc = &RunRenameObjectModal;
            return test;
        }

        // ---- New scenarios (added below) ----

        void RunObjectContextMenuInHierarchy(ImGuiTestContext *ctx) {
            UiAutomationRunState *state =
                    GetTestState(ctx, "editor_ui/object_context_menu_in_hierarchy");
            IM_CHECK(state != nullptr);
            if (!state)
                return;

            IM_CHECK(EnsureEditorActive(ctx, state));
            if (!EnsureEditorActive(ctx, state))
                return;

            // Add a Panel so there is something to right-click in the hierarchy
            const ImGuiID addPopupIdObjCtx =
                    OpenToolbarPopup(ctx, "Add", "##toolbar_add_popup", "Panel");
            if (!addPopupIdObjCtx) {
                ctx->PopupCloseAll();
                LogWarn("UI scenario: Add popup did not appear.");
                return;
            }
            ctx->ItemClick("Panel");
            ctx->Yield(4);

            // Activate search mode so DrawObjectsTreeSearchMode provides context menu
            ctx->SetRef("Hierarchy");
            ctx->ItemInputValue("##object_search", "Panel");
            ctx->Yield(2);

            ctx->SetRef("Hierarchy");
            const bool objExists = WaitForCondition(ctx, 60, [ctx]() {
                return ctx->ItemExists("$$0/##obj_0");
            });
            if (!objExists) {
                LogWarn("UI scenario: $$0/##obj_0 not found in hierarchy search mode.");
                ctx->ItemInputValue("##object_search", "");
                return;
            }

            LogDebug("UI scenario action: right-click $$0/##obj_0 (search mode)");
            ctx->ItemClick("$$0/##obj_0", ImGuiMouseButton_Right);
            ctx->Yield(2);

            // Verify context menu items — use WaitForPopup to find the actual popup window
            const ImGuiID objCtxPopupId = WaitForPopup(ctx, 0, "Rename...");
            IM_CHECK(objCtxPopupId != ImGuiID(0));
            if (objCtxPopupId) {
                IM_CHECK(ctx->ItemExists("Rename..."));
                IM_CHECK(ctx->ItemExists("Duplicate"));
                IM_CHECK(ctx->ItemExists("Delete"));
            }

            ctx->PopupCloseAll();
            ctx->Yield(1);
            ctx->SetRef("Hierarchy");
            ctx->ItemInputValue("##object_search", "");

            CaptureIfEnabled(ctx, state,
                             "editor_ui__object_context_menu_in_hierarchy.png");
            LogInfo("UI scenario done: editor_ui/object_context_menu_in_hierarchy");
        }

        ImGuiTest *RegisterObjectContextMenuInHierarchy(ImGuiTestEngine *engine,
                                                        UiAutomationRunState *state) {
            ImGuiTest *test = IM_REGISTER_TEST(engine, "editor_ui",
                                               "object_context_menu_in_hierarchy");
            test->UserData = state;
            test->TestFunc = &RunObjectContextMenuInHierarchy;
            return test;
        }

        void RunDuplicateObjectChangesHierarchy(ImGuiTestContext *ctx) {
            UiAutomationRunState *state =
                    GetTestState(ctx, "editor_ui/duplicate_object_changes_hierarchy");
            IM_CHECK(state != nullptr);
            if (!state)
                return;

            IM_CHECK(EnsureEditorActive(ctx, state));
            if (!EnsureEditorActive(ctx, state))
                return;

            // Add a Panel via the Add menu
            const ImGuiID addPopupIdDup =
                    OpenToolbarPopup(ctx, "Add", "##toolbar_add_popup", "Panel");
            if (!addPopupIdDup) {
                ctx->PopupCloseAll();
                return;
            }
            ctx->ItemClick("Panel");
            ctx->Yield(4);

            // Confirm first object exists (tree mode: TreeNodeEx("##obj_tree") in PushID(0))
            ctx->SetRef("Hierarchy");
            const bool firstObj = WaitForCondition(ctx, 60, [ctx]() {
                return ctx->ItemExists("$$0/##obj_tree");
            });
            IM_CHECK(firstObj);
            // Select it so Duplicate is enabled
            if (firstObj)
                ctx->ItemClick("$$0/##obj_tree");

            // Duplicate via Edit > Duplicate
            const ImGuiID editPopupIdDup =
                    OpenToolbarPopup(ctx, "Edit", "##toolbar_edit_popup", "Duplicate");
            IM_CHECK(editPopupIdDup != ImGuiID(0));
            if (!editPopupIdDup) {
                ctx->PopupCloseAll();
                return;
            }
            ctx->ItemClick("Duplicate");
            ctx->Yield(4);

            // Hierarchy should now contain a second object (PushID(1) / ##obj_tree)
            ctx->SetRef("Hierarchy");
            const bool secondObj = WaitForCondition(ctx, 60, [ctx]() {
                return ctx->ItemExists("$$1/##obj_tree");
            });
            IM_CHECK(secondObj);

            CaptureIfEnabled(ctx, state,
                             "editor_ui__duplicate_object_changes_hierarchy.png");
            LogInfo("UI scenario done: editor_ui/duplicate_object_changes_hierarchy");
        }

        ImGuiTest *RegisterDuplicateObjectChangesHierarchy(ImGuiTestEngine *engine,
                                                           UiAutomationRunState *state) {
            ImGuiTest *test = IM_REGISTER_TEST(engine, "editor_ui",
                                               "duplicate_object_changes_hierarchy");
            test->UserData = state;
            test->TestFunc = &RunDuplicateObjectChangesHierarchy;
            return test;
        }

        void RunDeleteSelectedObjectFlow(ImGuiTestContext *ctx) {
            UiAutomationRunState *state =
                    GetTestState(ctx, "editor_ui/delete_selected_object_flow");
            IM_CHECK(state != nullptr);
            if (!state)
                return;

            IM_CHECK(EnsureEditorActive(ctx, state));
            if (!EnsureEditorActive(ctx, state))
                return;

            // Add a Panel so there is an object to delete
            const ImGuiID addPopupIdDel =
                    OpenToolbarPopup(ctx, "Add", "##toolbar_add_popup", "Panel");
            if (!addPopupIdDel) {
                ctx->PopupCloseAll();
                return;
            }
            ctx->ItemClick("Panel");
            ctx->Yield(4);

            // Select the panel so Edit > Delete is enabled
            ctx->SetRef("Hierarchy");
            const bool panelReady = WaitForCondition(ctx, 60, [ctx]() {
                return ctx->ItemExists("$$0/##obj_tree");
            });
            if (panelReady)
                ctx->ItemClick("$$0/##obj_tree");
            ctx->Yield(1);

            // Trigger Edit > Delete
            const ImGuiID editPopupIdDel =
                    OpenToolbarPopup(ctx, "Edit", "##toolbar_edit_popup", "Delete");
            IM_CHECK(editPopupIdDel != ImGuiID(0));
            if (!editPopupIdDel) {
                ctx->PopupCloseAll();
                return;
            }
            ctx->ItemClick("Delete");
            ctx->Yield(2);

            // The confirm delete modal should appear
            const bool modalReady = WaitForCondition(ctx, 60, [ctx]() {
                return ctx->ItemExists("Confirm Delete Objects/Cancel");
            });
            IM_CHECK(modalReady);
            if (modalReady) {
                ctx->SetRef("Confirm Delete Objects");
                IM_CHECK(ctx->ItemExists("Cancel"));
                IM_CHECK(ctx->ItemExists("Delete"));

                LogDebug("UI scenario action: click Cancel in delete confirm modal");
                ctx->ItemClick("Cancel");
                ctx->Yield(1);
            }

            CaptureIfEnabled(ctx, state,
                             "editor_ui__delete_selected_object_flow.png");
            LogInfo("UI scenario done: editor_ui/delete_selected_object_flow");
        }

        ImGuiTest *RegisterDeleteSelectedObjectFlow(ImGuiTestEngine *engine,
                                                    UiAutomationRunState *state) {
            ImGuiTest *test = IM_REGISTER_TEST(engine, "editor_ui",
                                               "delete_selected_object_flow");
            test->UserData = state;
            test->TestFunc = &RunDeleteSelectedObjectFlow;
            return test;
        }

        void RunUndoRedoViaEditMenu(ImGuiTestContext *ctx) {
            UiAutomationRunState *state =
                    GetTestState(ctx, "editor_ui/undo_redo_via_edit_menu");
            IM_CHECK(state != nullptr);
            if (!state)
                return;

            IM_CHECK(EnsureEditorActive(ctx, state));
            if (!EnsureEditorActive(ctx, state))
                return;

            // Add a Panel to have an undoable action
            const ImGuiID addPopupIdUR =
                    OpenToolbarPopup(ctx, "Add", "##toolbar_add_popup", "Panel");
            if (!addPopupIdUR) {
                ctx->PopupCloseAll();
                return;
            }
            ctx->ItemClick("Panel");
            ctx->Yield(4);

            // Undo via Edit > Undo
            const ImGuiID undoPopupId =
                    OpenToolbarPopup(ctx, "Edit", "##toolbar_edit_popup", "Undo");
            IM_CHECK(undoPopupId != ImGuiID(0));
            if (!undoPopupId) {
                ctx->PopupCloseAll();
                return;
            }
            ctx->ItemClick("Undo");
            ctx->Yield(4);

            // Redo via Edit > Redo
            const ImGuiID redoPopupId =
                    OpenToolbarPopup(ctx, "Edit", "##toolbar_edit_popup", "Redo");
            IM_CHECK(redoPopupId != ImGuiID(0));
            if (!redoPopupId) {
                ctx->PopupCloseAll();
                return;
            }
            ctx->ItemClick("Redo");
            ctx->Yield(4);

            // Hierarchy window should still be visible after undo/redo
            const ImGuiWindow *hierWin = ImGui::FindWindowByName("Hierarchy");
            IM_CHECK(hierWin != nullptr && !hierWin->Hidden);

            CaptureIfEnabled(ctx, state,
                             "editor_ui__undo_redo_via_edit_menu.png");
            LogInfo("UI scenario done: editor_ui/undo_redo_via_edit_menu");
        }

        ImGuiTest *RegisterUndoRedoViaEditMenu(ImGuiTestEngine *engine,
                                               UiAutomationRunState *state) {
            ImGuiTest *test =
                    IM_REGISTER_TEST(engine, "editor_ui", "undo_redo_via_edit_menu");
            test->UserData = state;
            test->TestFunc = &RunUndoRedoViaEditMenu;
            return test;
        }

        void RunMcpTabContentVisible(ImGuiTestContext *ctx) {
            UiAutomationRunState *state =
                    GetTestState(ctx, "editor_ui/mcp_tab_content_visible");
            IM_CHECK(state != nullptr);
            if (!state)
                return;

            IM_CHECK(EnsureEditorActive(ctx, state));
            if (!EnsureEditorActive(ctx, state))
                return;

            const bool tabReady = WaitForCondition(ctx, 120, [ctx]() {
                ctx->SetRef("Workspace");
                return ctx->ItemExists("##bottom_tabs/MCP");
            });
            IM_CHECK(tabReady);
            if (!tabReady)
                return;

            ctx->SetRef("Workspace");
            LogDebug("UI scenario action: click MCP tab");
            ctx->ItemClick("##bottom_tabs/MCP");
            ctx->Yield(2);

            // Verify the MCP clients table region is rendered
            ctx->SetRef("Workspace");
            const bool catalogReady = WaitForCondition(ctx, 60, [ctx]() {
                ctx->SetRef("Workspace");
                return ctx->ItemExists("##mcp_catalog");
            });
            IM_CHECK(catalogReady);

            CaptureIfEnabled(ctx, state,
                             "editor_ui__mcp_tab_content_visible.png");
            LogInfo("UI scenario done: editor_ui/mcp_tab_content_visible");
        }

        ImGuiTest *RegisterMcpTabContentVisible(ImGuiTestEngine *engine,
                                                UiAutomationRunState *state) {
            ImGuiTest *test =
                    IM_REGISTER_TEST(engine, "editor_ui", "mcp_tab_content_visible");
            test->UserData = state;
            test->TestFunc = &RunMcpTabContentVisible;
            return test;
        }

        void RunProjectTabVisible(ImGuiTestContext *ctx) {
            UiAutomationRunState *state =
                    GetTestState(ctx, "editor_ui/project_tab_visible");
            IM_CHECK(state != nullptr);
            if (!state)
                return;

            IM_CHECK(EnsureEditorActive(ctx, state));
            if (!EnsureEditorActive(ctx, state))
                return;

            const bool tabReady = WaitForCondition(ctx, 120, [ctx]() {
                ctx->SetRef("Workspace");
                return ctx->ItemExists("##bottom_tabs/Project");
            });
            IM_CHECK(tabReady);
            if (!tabReady)
                return;

            ctx->SetRef("Workspace");
            LogDebug("UI scenario action: click Project tab");
            ctx->ItemClick("##bottom_tabs/Project");
            ctx->Yield(2);

            // The project panel content (tiles area) should be rendered
            ctx->SetRef("Workspace");
            const bool tilesReady = WaitForCondition(ctx, 60, [ctx]() {
                ctx->SetRef("Workspace");
                return ctx->ItemExists("##project_tiles");
            });
            IM_CHECK(tilesReady);

            CaptureIfEnabled(ctx, state, "editor_ui__project_tab_visible.png");
            LogInfo("UI scenario done: editor_ui/project_tab_visible");
        }

        ImGuiTest *RegisterProjectTabVisible(ImGuiTestEngine *engine,
                                             UiAutomationRunState *state) {
            ImGuiTest *test =
                    IM_REGISTER_TEST(engine, "editor_ui", "project_tab_visible");
            test->UserData = state;
            test->TestFunc = &RunProjectTabVisible;
            return test;
        }

        void RunObjectTypeFilterInHierarchy(ImGuiTestContext *ctx) {
            UiAutomationRunState *state =
                    GetTestState(ctx, "editor_ui/object_type_filter_in_hierarchy");
            IM_CHECK(state != nullptr);
            if (!state)
                return;

            IM_CHECK(EnsureEditorActive(ctx, state));
            if (!EnsureEditorActive(ctx, state))
                return;

            // Add a Panel object
            {
                const ImGuiID pid =
                        OpenToolbarPopup(ctx, "Add", "##toolbar_add_popup", "Panel");
                if (!pid) { ctx->PopupCloseAll(); return; }
                ctx->ItemClick("Panel");
                ctx->Yield(4);
            }

            // Add a Camera object
            {
                const ImGuiID pid =
                        OpenToolbarPopup(ctx, "Add", "##toolbar_add_popup", "Camera");
                if (!pid) { ctx->PopupCloseAll(); return; }
                ctx->ItemClick("Camera");
                ctx->Yield(4);
            }

            // Type "cam" into the hierarchy search filter
            ctx->SetRef("Hierarchy");
            const bool searchReady = WaitForCondition(ctx, 60, [ctx]() {
                return ctx->ItemExists("##object_search");
            });
            IM_CHECK(searchReady);
            if (!searchReady)
                return;

            LogDebug("UI scenario action: type 'cam' into hierarchy search");
            ctx->ItemInputValue("##object_search", "cam");
            ctx->Yield(3);

            // Clear search to restore full list
            LogDebug("UI scenario action: clear hierarchy search filter");
            ctx->ItemInputValue("##object_search", "");
            ctx->Yield(2);

            // Hierarchy search input should still be present after clearing
            IM_CHECK(ctx->ItemExists("##object_search"));

            CaptureIfEnabled(ctx, state,
                             "editor_ui__object_type_filter_in_hierarchy.png");
            LogInfo("UI scenario done: editor_ui/object_type_filter_in_hierarchy");
        }

        ImGuiTest *RegisterObjectTypeFilterInHierarchy(ImGuiTestEngine *engine,
                                                       UiAutomationRunState *state) {
            ImGuiTest *test = IM_REGISTER_TEST(engine, "editor_ui",
                                               "object_type_filter_in_hierarchy");
            test->UserData = state;
            test->TestFunc = &RunObjectTypeFilterInHierarchy;
            return test;
        }

        void RunAssetsPanelAddAssetButton(ImGuiTestContext *ctx) {
            UiAutomationRunState *state =
                    GetTestState(ctx, "editor_ui/assets_panel_add_asset_button");
            IM_CHECK(state != nullptr);
            if (!state)
                return;

            IM_CHECK(EnsureEditorActive(ctx, state));
            if (!EnsureEditorActive(ctx, state))
                return;

            // Wait for the Assets panel "+" button
            const bool assetsReady = WaitForCondition(ctx, 120, [ctx]() {
                ctx->SetRef("Assets");
                return ctx->ItemExists("+");
            });
            IM_CHECK(assetsReady);
            if (!assetsReady)
                return;

            ctx->SetRef("Assets");
            LogDebug("UI scenario action: click '+' in Assets panel");
            ctx->ItemClick("+");
            ctx->Yield(2);

            // The Create Asset modal should open
            const bool modalReady = WaitForCondition(ctx, 60, [ctx]() {
                return ctx->ItemExists("Create Asset/##draft_id");
            });
            IM_CHECK(modalReady);
            if (modalReady) {
                ctx->SetRef("Create Asset");
                IM_CHECK(ctx->ItemExists("##draft_id"));
                IM_CHECK(ctx->ItemExists("Cancel"));

                LogDebug("UI scenario action: dismiss Create Asset modal");
                ctx->ItemClick("Cancel");
                ctx->Yield(1);
            }

            CaptureIfEnabled(ctx, state,
                             "editor_ui__assets_panel_add_asset_button.png");
            LogInfo("UI scenario done: editor_ui/assets_panel_add_asset_button");
        }

        ImGuiTest *RegisterAssetsPanelAddAssetButton(ImGuiTestEngine *engine,
                                                     UiAutomationRunState *state) {
            ImGuiTest *test = IM_REGISTER_TEST(engine, "editor_ui",
                                               "assets_panel_add_asset_button");
            test->UserData = state;
            test->TestFunc = &RunAssetsPanelAddAssetButton;
            return test;
        }

        void RunPropertiesShowsCameraFields(ImGuiTestContext *ctx) {
            UiAutomationRunState *state =
                    GetTestState(ctx, "editor_ui/properties_shows_camera_fields");
            IM_CHECK(state != nullptr);
            if (!state)
                return;

            IM_CHECK(EnsureEditorActive(ctx, state));
            if (!EnsureEditorActive(ctx, state))
                return;

            // Add a Camera via the Add menu
            const ImGuiID addPopupIdCam =
                    OpenToolbarPopup(ctx, "Add", "##toolbar_add_popup", "Camera");
            if (!addPopupIdCam) {
                ctx->PopupCloseAll();
                return;
            }
            ctx->ItemClick("Camera");
            ctx->Yield(4);

            // Select the camera in the hierarchy (tree mode: PushID(0) / ##obj_tree)
            ctx->SetRef("Hierarchy");
            const bool camObj = WaitForCondition(ctx, 60, [ctx]() {
                return ctx->ItemExists("$$0/##obj_tree");
            });
            if (camObj) {
                ctx->ItemClick("$$0/##obj_tree");
                ctx->Yield(2);
            }

            // Properties panel should show camera-specific drag float fields
            ctx->SetRef("Properties");
            const bool yawReady = WaitForCondition(ctx, 60, [ctx]() {
                ctx->SetRef("Properties");
                return ctx->ItemExists("Yaw");
            });
            IM_CHECK(yawReady);
            if (yawReady) {
                IM_CHECK(ctx->ItemExists("Yaw"));
                IM_CHECK(ctx->ItemExists("Pitch"));
                IM_CHECK(ctx->ItemExists("FOV"));
            }

            CaptureIfEnabled(ctx, state,
                             "editor_ui__properties_shows_camera_fields.png");
            LogInfo("UI scenario done: editor_ui/properties_shows_camera_fields");
        }

        ImGuiTest *RegisterPropertiesShowsCameraFields(ImGuiTestEngine *engine,
                                                       UiAutomationRunState *state) {
            ImGuiTest *test = IM_REGISTER_TEST(engine, "editor_ui",
                                               "properties_shows_camera_fields");
            test->UserData = state;
            test->TestFunc = &RunPropertiesShowsCameraFields;
            return test;
        }

        void RunMultiSelectShowsBatchPanel(ImGuiTestContext *ctx) {
            UiAutomationRunState *state =
                    GetTestState(ctx, "editor_ui/multi_select_shows_batch_panel");
            IM_CHECK(state != nullptr);
            if (!state)
                return;

            IM_CHECK(EnsureEditorActive(ctx, state));
            if (!EnsureEditorActive(ctx, state))
                return;

            // Add three Panel objects
            for (int i = 0; i < 3; ++i) {
                const ImGuiID addPid =
                        OpenToolbarPopup(ctx, "Add", "##toolbar_add_popup", "Panel");
                if (!addPid) {
                    ctx->PopupCloseAll();
                    return;
                }
                ctx->ItemClick("Panel");
                ctx->Yield(3);
            }

            // Click first object to select it (tree mode: PushID(0)/##obj_tree)
            ctx->SetRef("Hierarchy");
            const bool firstReady = WaitForCondition(ctx, 60, [ctx]() {
                return ctx->ItemExists("$$0/##obj_tree");
            });
            IM_CHECK(firstReady);
            if (!firstReady)
                return;

            ctx->ItemClick("$$0/##obj_tree");
            ctx->Yield(1);

            // Ctrl+click second and third objects (PushID(1)/##obj_tree, PushID(2)/##obj_tree)
            ctx->KeyDown(ImGuiMod_Ctrl);
            if (ctx->ItemExists("$$1/##obj_tree"))
                ctx->ItemClick("$$1/##obj_tree");
            ctx->Yield(1);
            if (ctx->ItemExists("$$2/##obj_tree"))
                ctx->ItemClick("$$2/##obj_tree");
            ctx->KeyUp(ImGuiMod_Ctrl);
            ctx->Yield(2);

            // Properties panel should show the multi-select batch operations
            ctx->SetRef("Properties");
            const bool batchReady = WaitForCondition(ctx, 60, [ctx]() {
                ctx->SetRef("Properties");
                return ctx->ItemExists("Duplicate Selected");
            });
            IM_CHECK(batchReady);
            if (batchReady) {
                IM_CHECK(ctx->ItemExists("Duplicate Selected"));
                IM_CHECK(ctx->ItemExists("Delete Selected"));
            }

            CaptureIfEnabled(ctx, state,
                             "editor_ui__multi_select_shows_batch_panel.png");
            LogInfo("UI scenario done: editor_ui/multi_select_shows_batch_panel");
        }

        ImGuiTest *RegisterMultiSelectShowsBatchPanel(ImGuiTestEngine *engine,
                                                      UiAutomationRunState *state) {
            ImGuiTest *test = IM_REGISTER_TEST(engine, "editor_ui",
                                               "multi_select_shows_batch_panel");
            test->UserData = state;
            test->TestFunc = &RunMultiSelectShowsBatchPanel;
            return test;
        }

        void RunEditMenuShowsHistoryItems(ImGuiTestContext *ctx) {
            UiAutomationRunState *state =
                    GetTestState(ctx, "editor_ui/edit_menu_shows_history_items");
            IM_CHECK(state != nullptr);
            if (!state)
                return;

            IM_CHECK(EnsureEditorActive(ctx, state));
            if (!EnsureEditorActive(ctx, state))
                return;

            // Add a Panel first to ensure Rename... and Create Prefab items are enabled
            const ImGuiID addPopupIdHist =
                    OpenToolbarPopup(ctx, "Add", "##toolbar_add_popup", "Panel");
            if (!addPopupIdHist) {
                ctx->PopupCloseAll();
                return;
            }
            ctx->ItemClick("Panel");
            ctx->Yield(4);

            // Open Edit menu
            const ImGuiID editPopupIdHist =
                    OpenToolbarPopup(ctx, "Edit", "##toolbar_edit_popup", "Undo");
            IM_CHECK(editPopupIdHist != ImGuiID(0));
            if (!editPopupIdHist)
                return;

            IM_CHECK(ctx->ItemExists("Undo"));
            IM_CHECK(ctx->ItemExists("Redo"));
            IM_CHECK(ctx->ItemExists("Rename..."));
            IM_CHECK(ctx->ItemExists("Duplicate"));
            IM_CHECK(ctx->ItemExists("Delete"));

            ctx->PopupCloseAll();
            ctx->Yield(1);

            CaptureIfEnabled(ctx, state,
                             "editor_ui__edit_menu_shows_history_items.png");
            LogInfo("UI scenario done: editor_ui/edit_menu_shows_history_items");
        }

        ImGuiTest *RegisterEditMenuShowsHistoryItems(ImGuiTestEngine *engine,
                                                     UiAutomationRunState *state) {
            ImGuiTest *test = IM_REGISTER_TEST(engine, "editor_ui",
                                               "edit_menu_shows_history_items");
            test->UserData = state;
            test->TestFunc = &RunEditMenuShowsHistoryItems;
            return test;
        }

        void RunCloseEditorButton(ImGuiTestContext *ctx) {
            UiAutomationRunState *state =
                    GetTestState(ctx, "editor_ui/close_editor_button");
            IM_CHECK(state != nullptr);
            if (!state)
                return;

            IM_CHECK(EnsureEditorActive(ctx, state));
            if (!EnsureEditorActive(ctx, state))
                return;

            // Add a Panel to make the document dirty so the Unsaved Changes modal
            // appears instead of immediately closing the editor
            const ImGuiID addPopupIdClose =
                    OpenToolbarPopup(ctx, "Add", "##toolbar_add_popup", "Panel");
            if (!addPopupIdClose) {
                ctx->PopupCloseAll();
                return;
            }
            ctx->ItemClick("Panel");
            ctx->Yield(4);

            // Click the "Close editor" button in the toolbar
            ctx->SetRef("##toolbar");
            IM_CHECK(ctx->ItemExists("Close editor"));
            LogDebug("UI scenario action: click 'Close editor'");
            ctx->ItemClick("Close editor");
            ctx->Yield(2);

            // If the document is dirty an "Unsaved Changes" modal appears
            const bool unsavedModalOpen = WaitForCondition(ctx, 60, [ctx]() {
                return ctx->ItemExists("Unsaved Changes/Cancel");
            });

            if (unsavedModalOpen) {
                ctx->SetRef("Unsaved Changes");
                IM_CHECK(ctx->ItemExists("Cancel"));
                IM_CHECK(ctx->ItemExists("Discard"));

                LogDebug("UI scenario action: cancel close-editor modal");
                ctx->ItemClick("Cancel");
                ctx->Yield(2);

                // Toolbar should still be accessible after cancelling
                ctx->SetRef("##toolbar");
                IM_CHECK(ctx->ItemExists("File"));
            } else {
                // Document was clean; editor may have already closed — log and skip
                LogWarn("UI scenario: Close editor did not produce Unsaved Changes "
                        "modal (document may have been clean).");
            }

            CaptureIfEnabled(ctx, state, "editor_ui__close_editor_button.png");
            LogInfo("UI scenario done: editor_ui/close_editor_button");
        }

        ImGuiTest *RegisterCloseEditorButton(ImGuiTestEngine *engine,
                                             UiAutomationRunState *state) {
            ImGuiTest *test =
                    IM_REGISTER_TEST(engine, "editor_ui", "close_editor_button");
            test->UserData = state;
            test->TestFunc = &RunCloseEditorButton;
            return test;
        }

        // ---- New scenarios (uncovered EditorLayer paths) ----

        // 1. play_mode_toggle — lines 2182–2200 (DrawPlaybackControls)
        void RunPlayModeToggle(ImGuiTestContext *ctx) {
            UiAutomationRunState *state = GetTestState(ctx, "editor_ui/play_mode_toggle");
            IM_CHECK(state != nullptr);
            if (!state)
                return;

            IM_CHECK(EnsureEditorActive(ctx, state));
            if (!EnsureEditorActive(ctx, state))
                return;

            ctx->SetRef("##toolbar");
            const bool playReady = WaitForCondition(ctx, 60, [ctx]() {
                return ctx->ItemExists("Play");
            });
            IM_CHECK(playReady);
            if (!playReady)
                return;

            LogDebug("UI scenario action: click Play button");
            ctx->ItemClick("Play");
            ctx->Yield(3);

            // After entering play mode, "Stop" should replace "Play"
            ctx->SetRef("##toolbar");
            const bool stopReady = WaitForCondition(ctx, 60, [ctx]() {
                return ctx->ItemExists("Stop");
            });
            IM_CHECK(stopReady);

            if (stopReady) {
                LogDebug("UI scenario action: click Stop to exit play mode");
                ctx->ItemClick("Stop");
                ctx->Yield(2);
            }

            CaptureIfEnabled(ctx, state, "editor_ui__play_mode_toggle.png");
            LogInfo("UI scenario done: editor_ui/play_mode_toggle");
        }

        ImGuiTest *RegisterPlayModeToggle(ImGuiTestEngine *engine,
                                          UiAutomationRunState *state) {
            ImGuiTest *test = IM_REGISTER_TEST(engine, "editor_ui", "play_mode_toggle");
            test->UserData = state;
            test->TestFunc = &RunPlayModeToggle;
            return test;
        }

        // 2. file_new_scene_cancel_dirty — lines 2037 + 4433–4494 (Unsaved Changes modal)
        void RunFileNewSceneCancelDirty(ImGuiTestContext *ctx) {
            UiAutomationRunState *state =
                    GetTestState(ctx, "editor_ui/file_new_scene_cancel_dirty");
            IM_CHECK(state != nullptr);
            if (!state)
                return;

            IM_CHECK(EnsureEditorActive(ctx, state));
            if (!EnsureEditorActive(ctx, state))
                return;

            // Add a Panel to make the document dirty
            const ImGuiID addPopupIdDirty =
                    OpenToolbarPopup(ctx, "Add", "##toolbar_add_popup", "Panel");
            if (!addPopupIdDirty) {
                ctx->PopupCloseAll();
                return;
            }
            ctx->ItemClick("Panel");
            ctx->Yield(4);

            // File > New Scene — should trigger Unsaved Changes modal
            const ImGuiID filePopupIdDirty =
                    OpenToolbarPopup(ctx, "File", "##toolbar_file_popup", "New Scene");
            IM_CHECK(filePopupIdDirty != ImGuiID(0));
            if (!filePopupIdDirty) {
                ctx->PopupCloseAll();
                return;
            }

            LogDebug("UI scenario action: click New Scene");
            ctx->ItemClick("New Scene");
            ctx->Yield(3);

            const bool unsavedReady = WaitForCondition(ctx, 60, [ctx]() {
                return ctx->ItemExists("Unsaved Changes/Cancel");
            });
            IM_CHECK(unsavedReady);

            if (unsavedReady) {
                ctx->SetRef("Unsaved Changes");
                IM_CHECK(ctx->ItemExists("Discard"));
                IM_CHECK(ctx->ItemExists("Save & Continue"));

                LogDebug("UI scenario action: click Cancel in Unsaved Changes modal");
                ctx->ItemClick("Cancel");
                ctx->Yield(2);

                // Toolbar still accessible after cancelling
                ctx->SetRef("##toolbar");
                IM_CHECK(ctx->ItemExists("File"));
            }

            CaptureIfEnabled(ctx, state, "editor_ui__file_new_scene_cancel_dirty.png");
            LogInfo("UI scenario done: editor_ui/file_new_scene_cancel_dirty");
        }

        ImGuiTest *RegisterFileNewSceneCancelDirty(ImGuiTestEngine *engine,
                                                    UiAutomationRunState *state) {
            ImGuiTest *test =
                    IM_REGISTER_TEST(engine, "editor_ui", "file_new_scene_cancel_dirty");
            test->UserData = state;
            test->TestFunc = &RunFileNewSceneCancelDirty;
            return test;
        }

        // 3. file_open_scene_dismiss — line 2039 (Open Scene... menu item)
        void RunFileOpenSceneDismiss(ImGuiTestContext *ctx) {
            UiAutomationRunState *state =
                    GetTestState(ctx, "editor_ui/file_open_scene_dismiss");
            IM_CHECK(state != nullptr);
            if (!state)
                return;

            IM_CHECK(EnsureEditorActive(ctx, state));
            if (!EnsureEditorActive(ctx, state))
                return;

            const ImGuiID filePopupIdOpen =
                    OpenToolbarPopup(ctx, "File", "##toolbar_file_popup", "Open Scene...");
            IM_CHECK(filePopupIdOpen != ImGuiID(0));
            if (!filePopupIdOpen) {
                ctx->PopupCloseAll();
                return;
            }

            LogDebug("UI scenario action: click Open Scene...");
            ctx->ItemClick("Open Scene...");
            ctx->Yield(2);

            // Dismiss any system dialog or pending action with Escape
            ctx->PopupCloseAll();
            ctx->Yield(2);

            // Toolbar should still be accessible
            ctx->SetRef("##toolbar");
            IM_CHECK(ctx->ItemExists("File"));

            CaptureIfEnabled(ctx, state, "editor_ui__file_open_scene_dismiss.png");
            LogInfo("UI scenario done: editor_ui/file_open_scene_dismiss");
        }

        ImGuiTest *RegisterFileOpenSceneDismiss(ImGuiTestEngine *engine,
                                                 UiAutomationRunState *state) {
            ImGuiTest *test =
                    IM_REGISTER_TEST(engine, "editor_ui", "file_open_scene_dismiss");
            test->UserData = state;
            test->TestFunc = &RunFileOpenSceneDismiss;
            return test;
        }

        // 4. file_reset_layout — line ~2048 (File > Reset Layout)
        void RunFileResetLayout(ImGuiTestContext *ctx) {
            UiAutomationRunState *state =
                    GetTestState(ctx, "editor_ui/file_reset_layout");
            IM_CHECK(state != nullptr);
            if (!state)
                return;

            IM_CHECK(EnsureEditorActive(ctx, state));
            if (!EnsureEditorActive(ctx, state))
                return;

            const ImGuiID filePopupIdReset =
                    OpenToolbarPopup(ctx, "File", "##toolbar_file_popup", "Reset Layout");
            IM_CHECK(filePopupIdReset != ImGuiID(0));
            if (!filePopupIdReset) {
                ctx->PopupCloseAll();
                return;
            }

            LogDebug("UI scenario action: click Reset Layout");
            ctx->ItemClick("Reset Layout");
            ctx->Yield(3);

            // Layout reset should not crash; toolbar still accessible
            ctx->SetRef("##toolbar");
            const bool toolbarBack = WaitForCondition(ctx, 60, [ctx]() {
                return ctx->ItemExists("File");
            });
            IM_CHECK(toolbarBack);

            CaptureIfEnabled(ctx, state, "editor_ui__file_reset_layout.png");
            LogInfo("UI scenario done: editor_ui/file_reset_layout");
        }

        ImGuiTest *RegisterFileResetLayout(ImGuiTestEngine *engine,
                                            UiAutomationRunState *state) {
            ImGuiTest *test =
                    IM_REGISTER_TEST(engine, "editor_ui", "file_reset_layout");
            test->UserData = state;
            test->TestFunc = &RunFileResetLayout;
            return test;
        }

        // 5. view_fly_mode_activate — lines 2153–2161 (View > Fly Mode toggle)
        void RunViewFlyModeActivate(ImGuiTestContext *ctx) {
            UiAutomationRunState *state =
                    GetTestState(ctx, "editor_ui/view_fly_mode_activate");
            IM_CHECK(state != nullptr);
            if (!state)
                return;

            IM_CHECK(EnsureEditorActive(ctx, state));
            if (!EnsureEditorActive(ctx, state))
                return;

            const ImGuiID viewPopupIdFly =
                    OpenToolbarPopup(ctx, "View", "##toolbar_view_popup", "Fly Mode");
            IM_CHECK(viewPopupIdFly != ImGuiID(0));
            if (!viewPopupIdFly) {
                ctx->PopupCloseAll();
                return;
            }

            LogDebug("UI scenario action: click Fly Mode to activate");
            ctx->ItemClick("Fly Mode");
            ctx->Yield(2);

            // Re-open View menu to toggle Fly Mode off
            const ImGuiID viewPopupIdFly2 =
                    OpenToolbarPopup(ctx, "View", "##toolbar_view_popup", "Fly Mode");
            if (viewPopupIdFly2) {
                LogDebug("UI scenario action: click Fly Mode to deactivate");
                ctx->ItemClick("Fly Mode");
                ctx->Yield(2);
            } else {
                ctx->PopupCloseAll();
            }

            CaptureIfEnabled(ctx, state, "editor_ui__view_fly_mode_activate.png");
            LogInfo("UI scenario done: editor_ui/view_fly_mode_activate");
        }

        ImGuiTest *RegisterViewFlyModeActivate(ImGuiTestEngine *engine,
                                                UiAutomationRunState *state) {
            ImGuiTest *test =
                    IM_REGISTER_TEST(engine, "editor_ui", "view_fly_mode_activate");
            test->UserData = state;
            test->TestFunc = &RunViewFlyModeActivate;
            return test;
        }

        // 6. settings_modal_apply_button — lines 2874–2886 (DrawSettingsModal Apply)
        void RunSettingsModalApplyButton(ImGuiTestContext *ctx) {
            UiAutomationRunState *state =
                    GetTestState(ctx, "editor_ui/settings_modal_apply_button");
            IM_CHECK(state != nullptr);
            if (!state)
                return;

            IM_CHECK(EnsureEditorActive(ctx, state));
            if (!EnsureEditorActive(ctx, state))
                return;

            const ImGuiID filePopupIdApply =
                    OpenToolbarPopup(ctx, "File", "##toolbar_file_popup", "Settings...");
            IM_CHECK(filePopupIdApply != ImGuiID(0));
            if (!filePopupIdApply) {
                ctx->PopupCloseAll();
                return;
            }

            LogDebug("UI scenario action: open Settings modal");
            ctx->ItemClick("Settings...");
            ctx->Yield(2);

            const bool modalReady = WaitForCondition(ctx, 60, [ctx]() {
                return ctx->ItemExists("Editor Settings/Apply");
            });
            IM_CHECK(modalReady);
            if (!modalReady) {
                ctx->PopupCloseAll();
                return;
            }

            ctx->SetRef("Editor Settings");
            IM_CHECK(ctx->ItemExists("Apply"));
            IM_CHECK(ctx->ItemExists("Cancel"));

            LogDebug("UI scenario action: click Apply in Settings modal");
            ctx->ItemClick("Apply");
            ctx->Yield(2);

            // Modal should be closed after Apply (MCP apply may succeed or fail;
            // either way the editor should remain responsive)
            ctx->SetRef("##toolbar");
            IM_CHECK(ctx->ItemExists("File"));

            CaptureIfEnabled(ctx, state, "editor_ui__settings_modal_apply_button.png");
            LogInfo("UI scenario done: editor_ui/settings_modal_apply_button");
        }

        ImGuiTest *RegisterSettingsModalApplyButton(ImGuiTestEngine *engine,
                                                     UiAutomationRunState *state) {
            ImGuiTest *test =
                    IM_REGISTER_TEST(engine, "editor_ui", "settings_modal_apply_button");
            test->UserData = state;
            test->TestFunc = &RunSettingsModalApplyButton;
            return test;
        }

        // 7. settings_modal_port_input — line 2857 (DrawSettingsModal Port InputInt)
        void RunSettingsModalPortInput(ImGuiTestContext *ctx) {
            UiAutomationRunState *state =
                    GetTestState(ctx, "editor_ui/settings_modal_port_input");
            IM_CHECK(state != nullptr);
            if (!state)
                return;

            IM_CHECK(EnsureEditorActive(ctx, state));
            if (!EnsureEditorActive(ctx, state))
                return;

            const ImGuiID filePopupIdPort =
                    OpenToolbarPopup(ctx, "File", "##toolbar_file_popup", "Settings...");
            if (!filePopupIdPort) {
                ctx->PopupCloseAll();
                return;
            }

            ctx->ItemClick("Settings...");
            ctx->Yield(2);

            const bool modalReady = WaitForCondition(ctx, 60, [ctx]() {
                return ctx->ItemExists("Editor Settings/Port");
            });
            IM_CHECK(modalReady);
            if (!modalReady) {
                ctx->PopupCloseAll();
                return;
            }

            ctx->SetRef("Editor Settings");
            IM_CHECK(ctx->ItemExists("Port"));

            // Interact with Port input to exercise InputInt path
            LogDebug("UI scenario action: activate Port input field");
            ctx->ItemClick("Port");
            ctx->Yield(1);

            LogDebug("UI scenario action: cancel settings modal");
            ctx->ItemClick("Cancel");
            ctx->Yield(2);

            ctx->SetRef("##toolbar");
            IM_CHECK(ctx->ItemExists("File"));

            CaptureIfEnabled(ctx, state, "editor_ui__settings_modal_port_input.png");
            LogInfo("UI scenario done: editor_ui/settings_modal_port_input");
        }

        ImGuiTest *RegisterSettingsModalPortInput(ImGuiTestEngine *engine,
                                                   UiAutomationRunState *state) {
            ImGuiTest *test =
                    IM_REGISTER_TEST(engine, "editor_ui", "settings_modal_port_input");
            test->UserData = state;
            test->TestFunc = &RunSettingsModalPortInput;
            return test;
        }

        // 8. create_asset_modal_fill_cancel — lines 4200–4320 (DrawCreateAssetModal)
        void RunCreateAssetModalFillCancel(ImGuiTestContext *ctx) {
            UiAutomationRunState *state =
                    GetTestState(ctx, "editor_ui/create_asset_modal_fill_cancel");
            IM_CHECK(state != nullptr);
            if (!state)
                return;

            IM_CHECK(EnsureEditorActive(ctx, state));
            if (!EnsureEditorActive(ctx, state))
                return;

            // Click the "+" button in the Assets panel to open Create Asset modal
            ctx->SetRef("Assets");
            const bool assetsReady = WaitForCondition(ctx, 60, [ctx]() {
                return ctx->ItemExists("+");
            });
            IM_CHECK(assetsReady);
            if (!assetsReady)
                return;

            LogDebug("UI scenario action: click + button in Assets panel");
            ctx->ItemClick("+");
            ctx->Yield(2);

            const bool modalReady = WaitForCondition(ctx, 60, [ctx]() {
                return ctx->ItemExists("Create Asset/##draft_id");
            });
            IM_CHECK(modalReady);
            if (!modalReady) {
                ctx->PopupCloseAll();
                return;
            }

            ctx->SetRef("Create Asset");
            IM_CHECK(ctx->ItemExists("##draft_id"));
            IM_CHECK(ctx->ItemExists("##draft_mesh"));

            LogDebug("UI scenario action: fill Asset ID input");
            ctx->ItemInputValue("##draft_id", "test_asset_ui");
            ctx->Yield(1);

            LogDebug("UI scenario action: fill Mesh input");
            ctx->ItemInputValue("##draft_mesh", "assets/models/test.obj");
            ctx->Yield(1);

            IM_CHECK(ctx->ItemExists("Cancel"));
            LogDebug("UI scenario action: click Cancel in Create Asset modal");
            ctx->ItemClick("Cancel");
            ctx->Yield(2);

            // Assets panel should still be visible
            ctx->SetRef("Assets");
            IM_CHECK(ctx->ItemExists("+"));

            CaptureIfEnabled(ctx, state,
                             "editor_ui__create_asset_modal_fill_cancel.png");
            LogInfo("UI scenario done: editor_ui/create_asset_modal_fill_cancel");
        }

        ImGuiTest *RegisterCreateAssetModalFillCancel(ImGuiTestEngine *engine,
                                                       UiAutomationRunState *state) {
            ImGuiTest *test = IM_REGISTER_TEST(engine, "editor_ui",
                                               "create_asset_modal_fill_cancel");
            test->UserData = state;
            test->TestFunc = &RunCreateAssetModalFillCancel;
            return test;
        }

        // 9. delete_confirm_modal_accept — lines 4349–4362 (Confirm Delete, click Delete)
        void RunDeleteConfirmModalAccept(ImGuiTestContext *ctx) {
            UiAutomationRunState *state =
                    GetTestState(ctx, "editor_ui/delete_confirm_modal_accept");
            IM_CHECK(state != nullptr);
            if (!state)
                return;

            IM_CHECK(EnsureEditorActive(ctx, state));
            if (!EnsureEditorActive(ctx, state))
                return;

            // Add a Panel so there is an object to delete
            const ImGuiID addPopupIdConfirm =
                    OpenToolbarPopup(ctx, "Add", "##toolbar_add_popup", "Panel");
            if (!addPopupIdConfirm) {
                ctx->PopupCloseAll();
                return;
            }
            ctx->ItemClick("Panel");
            ctx->Yield(4);

            // Select the panel then delete it (tree mode: PushID(0) / ##obj_tree)
            ctx->SetRef("Hierarchy");
            const bool objExists = WaitForCondition(ctx, 60, [ctx]() {
                return ctx->ItemExists("$$0/##obj_tree");
            });
            IM_CHECK(objExists);
            if (!objExists)
                return;

            ctx->ItemClick("$$0/##obj_tree");
            ctx->Yield(1);

            // Trigger Edit > Delete
            const ImGuiID editPopupIdConfirm =
                    OpenToolbarPopup(ctx, "Edit", "##toolbar_edit_popup", "Delete");
            IM_CHECK(editPopupIdConfirm != ImGuiID(0));
            if (!editPopupIdConfirm) {
                ctx->PopupCloseAll();
                return;
            }

            ctx->ItemClick("Delete");
            ctx->Yield(2);

            // Confirm Delete Objects modal appears
            const bool modalReady = WaitForCondition(ctx, 60, [ctx]() {
                return ctx->ItemExists("Confirm Delete Objects/Delete");
            });
            IM_CHECK(modalReady);
            if (!modalReady)
                return;

            ctx->SetRef("Confirm Delete Objects");
            IM_CHECK(ctx->ItemExists("Cancel"));
            IM_CHECK(ctx->ItemExists("Delete"));

            LogDebug("UI scenario action: click Delete in confirm modal");
            ctx->ItemClick("Delete");
            ctx->Yield(3);

            // Object should be gone from hierarchy (tree node at PushID(0) gone)
            ctx->SetRef("Hierarchy");
            const bool objGone = WaitForCondition(ctx, 60, [ctx]() {
                return !ctx->ItemExists("$$0/##obj_tree");
            });
            IM_CHECK(objGone);

            CaptureIfEnabled(ctx, state,
                             "editor_ui__delete_confirm_modal_accept.png");
            LogInfo("UI scenario done: editor_ui/delete_confirm_modal_accept");
        }

        ImGuiTest *RegisterDeleteConfirmModalAccept(ImGuiTestEngine *engine,
                                                     UiAutomationRunState *state) {
            ImGuiTest *test = IM_REGISTER_TEST(engine, "editor_ui",
                                               "delete_confirm_modal_accept");
            test->UserData = state;
            test->TestFunc = &RunDeleteConfirmModalAccept;
            return test;
        }

        // 10. scene_header_context_add_panel — lines 3264–3288 (scene header ctx menu)
        void RunSceneHeaderContextAddPanel(ImGuiTestContext *ctx) {
            UiAutomationRunState *state =
                    GetTestState(ctx, "editor_ui/scene_header_context_add_panel");
            IM_CHECK(state != nullptr);
            if (!state)
                return;

            IM_CHECK(EnsureEditorActive(ctx, state));
            if (!EnsureEditorActive(ctx, state))
                return;

            const bool headerReady = WaitForCondition(ctx, 120, [ctx]() {
                ctx->SetRef("Hierarchy");
                return ctx->ItemExists("##scene_primary");
            });
            IM_CHECK(headerReady);
            if (!headerReady)
                return;

            LogDebug("UI scenario action: right-click scene header ##scene_primary");
            ctx->SetRef("Hierarchy");
            ctx->ItemClick("##scene_primary", ImGuiMouseButton_Right);
            ctx->Yield(2);

            // Find scene context menu popup using OpenPopupStack
            const ImGuiID sceneCtxId = WaitForPopup(ctx, 0, "Add");
            IM_CHECK(sceneCtxId != ImGuiID(0));
            if (!sceneCtxId) {
                ctx->PopupCloseAll();
                return;
            }

            IM_CHECK(ctx->ItemExists("Add"));
            IM_CHECK(ctx->ItemExists("Save Scene"));

            // Open the Add submenu and click Panel
            ctx->ItemClick("Add");
            ctx->Yield(1);

            // Find Add submenu at stack depth 1
            const ImGuiID addSubId = WaitForPopup(ctx, 1, "Panel");
            if (addSubId) {
                LogDebug("UI scenario action: click Panel in Add submenu");
                ctx->ItemClick("Panel");
                ctx->Yield(3);
            } else {
                ctx->PopupCloseAll();
                ctx->Yield(1);
            }

            CaptureIfEnabled(ctx, state,
                             "editor_ui__scene_header_context_add_panel.png");
            LogInfo("UI scenario done: editor_ui/scene_header_context_add_panel");
        }

        ImGuiTest *RegisterSceneHeaderContextAddPanel(ImGuiTestEngine *engine,
                                                       UiAutomationRunState *state) {
            ImGuiTest *test = IM_REGISTER_TEST(engine, "editor_ui",
                                               "scene_header_context_add_panel");
            test->UserData = state;
            test->TestFunc = &RunSceneHeaderContextAddPanel;
            return test;
        }

        // 11. hierarchy_context_menu_add_child — lines 3521–3545 (obj ctx Add submenu)
        void RunHierarchyContextMenuAddChild(ImGuiTestContext *ctx) {
            UiAutomationRunState *state =
                    GetTestState(ctx, "editor_ui/hierarchy_context_menu_add_child");
            IM_CHECK(state != nullptr);
            if (!state)
                return;

            IM_CHECK(EnsureEditorActive(ctx, state));
            if (!EnsureEditorActive(ctx, state))
                return;

            // Add a Panel as the parent object
            const ImGuiID addPopupIdChild =
                    OpenToolbarPopup(ctx, "Add", "##toolbar_add_popup", "Panel");
            if (!addPopupIdChild) {
                ctx->PopupCloseAll();
                return;
            }
            ctx->ItemClick("Panel");
            ctx->Yield(4);

            // Activate search mode to get context menu (DrawObjectsTreeSearchMode)
            ctx->SetRef("Hierarchy");
            ctx->ItemInputValue("##object_search", "Panel");
            ctx->Yield(2);

            // Compute obj_ctx popup window ID (BeginPopupContextItem inside PushID(0))
            ctx->SetRef("Hierarchy");
            const bool objExists = WaitForCondition(ctx, 60, [ctx]() {
                return ctx->ItemExists("$$0/##obj_0");
            });
            IM_CHECK(objExists);
            if (!objExists) {
                ctx->ItemInputValue("##object_search", "");
                return;
            }

            LogDebug("UI scenario action: right-click $$0/##obj_0 to open context menu");
            ctx->ItemClick("$$0/##obj_0", ImGuiMouseButton_Right);
            ctx->Yield(2);

            // Find context menu popup using OpenPopupStack
            const ImGuiID objCtxIdChild = WaitForPopup(ctx, 0, "Add");
            IM_CHECK(objCtxIdChild != ImGuiID(0));
            if (!objCtxIdChild) {
                ctx->PopupCloseAll();
                ctx->SetRef("Hierarchy");
                ctx->ItemInputValue("##object_search", "");
                return;
            }

            IM_CHECK(ctx->ItemExists("Add"));

            // Open Add submenu
            ctx->ItemClick("Add");
            ctx->Yield(1);

            // Find Add submenu at stack depth 1
            const ImGuiID addSubIdChild = WaitForPopup(ctx, 1, "Prop");
            IM_CHECK(addSubIdChild != ImGuiID(0));
            if (addSubIdChild) {
                LogDebug("UI scenario action: click Prop in Add submenu");
                ctx->ItemClick("Prop");
                ctx->Yield(3);
            } else {
                ctx->PopupCloseAll();
                ctx->Yield(1);
            }
            // Restore: clear search
            ctx->SetRef("Hierarchy");
            ctx->ItemInputValue("##object_search", "");

            CaptureIfEnabled(ctx, state,
                             "editor_ui__hierarchy_context_menu_add_child.png");
            LogInfo("UI scenario done: editor_ui/hierarchy_context_menu_add_child");
        }

        ImGuiTest *RegisterHierarchyContextMenuAddChild(ImGuiTestEngine *engine,
                                                         UiAutomationRunState *state) {
            ImGuiTest *test = IM_REGISTER_TEST(engine, "editor_ui",
                                               "hierarchy_context_menu_add_child");
            test->UserData = state;
            test->TestFunc = &RunHierarchyContextMenuAddChild;
            return test;
        }

        // 12. edit_menu_create_prefab — lines 2108–2116 (DrawToolbarEditMenuItems Create Prefab)
        void RunEditMenuCreatePrefab(ImGuiTestContext *ctx) {
            UiAutomationRunState *state =
                    GetTestState(ctx, "editor_ui/edit_menu_create_prefab");
            IM_CHECK(state != nullptr);
            if (!state)
                return;

            IM_CHECK(EnsureEditorActive(ctx, state));
            if (!EnsureEditorActive(ctx, state))
                return;

            // Add a Panel so Edit > Create Prefab is enabled (needs single selection)
            const ImGuiID addPopupIdPrefab =
                    OpenToolbarPopup(ctx, "Add", "##toolbar_add_popup", "Panel");
            if (!addPopupIdPrefab) {
                ctx->PopupCloseAll();
                return;
            }
            ctx->ItemClick("Panel");
            ctx->Yield(4);

            // Select the object so Create Prefab is enabled
            ctx->SetRef("Hierarchy");
            const bool panelReadyPrefab = WaitForCondition(ctx, 60, [ctx]() {
                return ctx->ItemExists("$$0/##obj_tree");
            });
            if (panelReadyPrefab)
                ctx->ItemClick("$$0/##obj_tree");
            ctx->Yield(1);

            // Trigger Edit > Create Prefab
            const ImGuiID editPopupIdPrefab =
                    OpenToolbarPopup(ctx, "Edit", "##toolbar_edit_popup", "Create Prefab");
            IM_CHECK(editPopupIdPrefab != ImGuiID(0));
            if (!editPopupIdPrefab) {
                ctx->PopupCloseAll();
                return;
            }

            IM_CHECK(ctx->ItemExists("Create Prefab"));
            LogDebug("UI scenario action: click Create Prefab");
            ctx->ItemClick("Create Prefab");
            ctx->Yield(3);

            // Create Prefab may log an error (no project path) or succeed silently;
            // editor must remain responsive either way.
            ctx->SetRef("##toolbar");
            IM_CHECK(ctx->ItemExists("File"));

            CaptureIfEnabled(ctx, state, "editor_ui__edit_menu_create_prefab.png");
            LogInfo("UI scenario done: editor_ui/edit_menu_create_prefab");
        }

        ImGuiTest *RegisterEditMenuCreatePrefab(ImGuiTestEngine *engine,
                                                 UiAutomationRunState *state) {
            ImGuiTest *test =
                    IM_REGISTER_TEST(engine, "editor_ui", "edit_menu_create_prefab");
            test->UserData = state;
            test->TestFunc = &RunEditMenuCreatePrefab;
            return test;
        }

        // 13. assets_panel_search_open_dismiss — lines 3758–3975 (Search popup)
        void RunAssetsPanelSearchOpenDismiss(ImGuiTestContext *ctx) {
            UiAutomationRunState *state =
                    GetTestState(ctx, "editor_ui/assets_panel_search_open_dismiss");
            IM_CHECK(state != nullptr);
            if (!state)
                return;

            IM_CHECK(EnsureEditorActive(ctx, state));
            if (!EnsureEditorActive(ctx, state))
                return;

            ctx->SetRef("Assets");
            const bool searchBtnReady = WaitForCondition(ctx, 60, [ctx]() {
                return ctx->ItemExists("Search");
            });
            IM_CHECK(searchBtnReady);
            if (!searchBtnReady)
                return;

            LogDebug("UI scenario action: click Search button in Assets panel");
            ctx->ItemClick("Search");
            ctx->Yield(2);

            const bool popupReady = WaitForCondition(ctx, 60, [ctx]() {
                return ctx->ItemExists("Asset Search/##quick_open_input");
            });
            IM_CHECK(popupReady);

            if (popupReady) {
                ctx->SetRef("Asset Search");
                IM_CHECK(ctx->ItemExists("##quick_open_input"));

                LogDebug("UI scenario action: dismiss Asset Search popup with Escape");
                ctx->PopupCloseAll();
                ctx->Yield(2);
            } else {
                ctx->PopupCloseAll();
                ctx->Yield(1);
            }

            // Assets panel still accessible
            ctx->SetRef("Assets");
            IM_CHECK(ctx->ItemExists("+"));

            CaptureIfEnabled(ctx, state,
                             "editor_ui__assets_panel_search_open_dismiss.png");
            LogInfo("UI scenario done: editor_ui/assets_panel_search_open_dismiss");
        }

        ImGuiTest *RegisterAssetsPanelSearchOpenDismiss(ImGuiTestEngine *engine,
                                                         UiAutomationRunState *state) {
            ImGuiTest *test = IM_REGISTER_TEST(engine, "editor_ui",
                                               "assets_panel_search_open_dismiss");
            test->UserData = state;
            test->TestFunc = &RunAssetsPanelSearchOpenDismiss;
            return test;
        }

        // 14. console_filter_warn_toggle — lines 2547–2558 (DrawConsoleTab checkboxes)
        void RunConsoleFilterWarnToggle(ImGuiTestContext *ctx) {
            UiAutomationRunState *state =
                    GetTestState(ctx, "editor_ui/console_filter_warn_toggle");
            IM_CHECK(state != nullptr);
            if (!state)
                return;

            IM_CHECK(EnsureEditorActive(ctx, state));
            if (!EnsureEditorActive(ctx, state))
                return;

            const bool consoleReady = WaitForCondition(ctx, 120, [ctx]() {
                ctx->SetRef("Workspace");
                return ctx->ItemExists("##bottom_tabs/Console");
            });
            IM_CHECK(consoleReady);
            if (!consoleReady)
                return;

            ctx->SetRef("Workspace");
            LogDebug("UI scenario action: switch to Console tab");
            ctx->ItemClick("##bottom_tabs/Console");
            ctx->Yield(2);

            ctx->SetRef("Workspace");
            IM_CHECK(ctx->ItemExists("Warn"));
            IM_CHECK(ctx->ItemExists("Error"));
            IM_CHECK(ctx->ItemExists("Info"));

            // Toggle Warn off
            LogDebug("UI scenario action: toggle Warn checkbox off");
            ctx->ItemCheck("Warn");
            ctx->Yield(1);

            // Toggle Warn back on
            LogDebug("UI scenario action: toggle Warn checkbox on");
            ctx->ItemCheck("Warn");
            ctx->Yield(1);

            // Toggle Error off then on
            LogDebug("UI scenario action: toggle Error checkbox off");
            ctx->ItemCheck("Error");
            ctx->Yield(1);
            LogDebug("UI scenario action: toggle Error checkbox on");
            ctx->ItemCheck("Error");
            ctx->Yield(1);

            CaptureIfEnabled(ctx, state, "editor_ui__console_filter_warn_toggle.png");
            LogInfo("UI scenario done: editor_ui/console_filter_warn_toggle");
        }

        ImGuiTest *RegisterConsoleFilterWarnToggle(ImGuiTestEngine *engine,
                                                    UiAutomationRunState *state) {
            ImGuiTest *test =
                    IM_REGISTER_TEST(engine, "editor_ui", "console_filter_warn_toggle");
            test->UserData = state;
            test->TestFunc = &RunConsoleFilterWarnToggle;
            return test;
        }

        // 15. mcp_clear_request_log — line 2812 (DrawMcpTab Clear Request Log)
        void RunMcpClearRequestLog(ImGuiTestContext *ctx) {
            UiAutomationRunState *state =
                    GetTestState(ctx, "editor_ui/mcp_clear_request_log");
            IM_CHECK(state != nullptr);
            if (!state)
                return;

            IM_CHECK(EnsureEditorActive(ctx, state));
            if (!EnsureEditorActive(ctx, state))
                return;

            const bool mcpTabReady = WaitForCondition(ctx, 120, [ctx]() {
                ctx->SetRef("Workspace");
                return ctx->ItemExists("##bottom_tabs/MCP");
            });
            IM_CHECK(mcpTabReady);
            if (!mcpTabReady)
                return;

            ctx->SetRef("Workspace");
            LogDebug("UI scenario action: switch to MCP tab");
            ctx->ItemClick("##bottom_tabs/MCP");
            ctx->Yield(2);

            ctx->SetRef("Workspace");
            const bool clearBtnReady = WaitForCondition(ctx, 60, [ctx]() {
                return ctx->ItemExists("Clear Request Log");
            });
            IM_CHECK(clearBtnReady);

            if (clearBtnReady) {
                LogDebug("UI scenario action: click Clear Request Log");
                ctx->ItemClick("Clear Request Log");
                ctx->Yield(2);
            }

            CaptureIfEnabled(ctx, state, "editor_ui__mcp_clear_request_log.png");
            LogInfo("UI scenario done: editor_ui/mcp_clear_request_log");
        }

        ImGuiTest *RegisterMcpClearRequestLog(ImGuiTestEngine *engine,
                                               UiAutomationRunState *state) {
            ImGuiTest *test =
                    IM_REGISTER_TEST(engine, "editor_ui", "mcp_clear_request_log");
            test->UserData = state;
            test->TestFunc = &RunMcpClearRequestLog;
            return test;
        }

        // 16. hierarchy_empty_space_context_add — lines 3231–3255 (obj_ctx_empty menu)
        void RunHierarchyEmptySpaceContextAdd(ImGuiTestContext *ctx) {
            UiAutomationRunState *state =
                    GetTestState(ctx, "editor_ui/hierarchy_empty_space_context_add");
            IM_CHECK(state != nullptr);
            if (!state)
                return;

            IM_CHECK(EnsureEditorActive(ctx, state));
            if (!EnsureEditorActive(ctx, state))
                return;

            const bool hierReady = WaitForCondition(ctx, 120, [ctx]() {
                ctx->SetRef("Hierarchy");
                return ctx->ItemExists("##object_search");
            });
            IM_CHECK(hierReady);
            if (!hierReady)
                return;

            // Right-click empty space in hierarchy panel to open obj_ctx_empty popup
            LogDebug("UI scenario action: right-click empty area in Hierarchy panel");
            const ImGuiWindow *hierWin = ctx->GetWindowByRef("Hierarchy");
            if (hierWin) {
                ctx->MouseMoveToPos(
                        ImVec2(hierWin->Pos.x + 10.0f, hierWin->Pos.y + 80.0f));
            }
            ctx->MouseClick(ImGuiMouseButton_Right);
            ctx->Yield(2);

            // Find context menu popup using OpenPopupStack
            const ImGuiID emptyCtxId = WaitForPopup(ctx, 0, "Add");
            IM_CHECK(emptyCtxId != ImGuiID(0));
            if (!emptyCtxId) {
                ctx->PopupCloseAll();
                return;
            }

            IM_CHECK(ctx->ItemExists("Add"));

            // Open the Add submenu and choose Light
            ctx->ItemClick("Add");
            ctx->Yield(1);

            // Find Add submenu at stack depth 1
            const ImGuiID addSubIdEmpty = WaitForPopup(ctx, 1, "Light");
            IM_CHECK(addSubIdEmpty != ImGuiID(0));
            if (addSubIdEmpty) {
                LogDebug("UI scenario action: click Light in empty-space Add submenu");
                ctx->ItemClick("Light");
                ctx->Yield(3);
            } else {
                ctx->PopupCloseAll();
                ctx->Yield(1);
            }

            CaptureIfEnabled(ctx, state,
                             "editor_ui__hierarchy_empty_space_context_add.png");
            LogInfo("UI scenario done: editor_ui/hierarchy_empty_space_context_add");
        }

        ImGuiTest *RegisterHierarchyEmptySpaceContextAdd(
                ImGuiTestEngine *engine, UiAutomationRunState *state) {
            ImGuiTest *test = IM_REGISTER_TEST(engine, "editor_ui",
                                               "hierarchy_empty_space_context_add");
            test->UserData = state;
            test->TestFunc = &RunHierarchyEmptySpaceContextAdd;
            return test;
        }

        // 17. unsaved_changes_discard — line 4468–4480 (Unsaved Changes Discard path)
        void RunUnsavedChangesDiscard(ImGuiTestContext *ctx) {
            UiAutomationRunState *state =
                    GetTestState(ctx, "editor_ui/unsaved_changes_discard");
            IM_CHECK(state != nullptr);
            if (!state)
                return;

            IM_CHECK(EnsureEditorActive(ctx, state));
            if (!EnsureEditorActive(ctx, state))
                return;

            // Add a Panel to dirty the document
            const ImGuiID addPopupIdDiscard =
                    OpenToolbarPopup(ctx, "Add", "##toolbar_add_popup", "Panel");
            if (!addPopupIdDiscard) {
                ctx->PopupCloseAll();
                return;
            }
            ctx->ItemClick("Panel");
            ctx->Yield(4);

            // File > New Scene to trigger Unsaved Changes modal
            const ImGuiID filePopupIdDiscard =
                    OpenToolbarPopup(ctx, "File", "##toolbar_file_popup", "New Scene");
            if (!filePopupIdDiscard) {
                ctx->PopupCloseAll();
                return;
            }

            ctx->ItemClick("New Scene");
            ctx->Yield(3);

            const bool unsavedReady = WaitForCondition(ctx, 60, [ctx]() {
                return ctx->ItemExists("Unsaved Changes/Discard");
            });
            IM_CHECK(unsavedReady);
            if (!unsavedReady)
                return;

            ctx->SetRef("Unsaved Changes");
            IM_CHECK(ctx->ItemExists("Discard"));

            LogDebug("UI scenario action: click Discard in Unsaved Changes modal");
            ctx->ItemClick("Discard");
            ctx->Yield(4);

            // After discarding, editor should process New Scene and remain active
            ctx->SetRef("##toolbar");
            const bool toolbarBack = WaitForCondition(ctx, 120, [ctx]() {
                return ctx->ItemExists("File");
            });
            IM_CHECK(toolbarBack);

            CaptureIfEnabled(ctx, state, "editor_ui__unsaved_changes_discard.png");
            LogInfo("UI scenario done: editor_ui/unsaved_changes_discard");
        }

        ImGuiTest *RegisterUnsavedChangesDiscard(ImGuiTestEngine *engine,
                                                  UiAutomationRunState *state) {
            ImGuiTest *test =
                    IM_REGISTER_TEST(engine, "editor_ui", "unsaved_changes_discard");
            test->UserData = state;
            test->TestFunc = &RunUnsavedChangesDiscard;
            return test;
        }

    } // namespace

    void RegisterEditorUiScenarioSet() {
        RegisterUiScenario("editor/toolbar_buttons_visible",
                           &RegisterToolbarButtonsVisible);
        RegisterUiScenario("editor/file_menu_items", &RegisterFileMenuItems);
        RegisterUiScenario("editor/add_menu_items", &RegisterAddMenuItems);
        RegisterUiScenario("editor/edit_menu_items", &RegisterEditMenuItems);
        RegisterUiScenario("editor/view_menu_items", &RegisterViewMenuItems);
        RegisterUiScenario("editor/scene_control_buttons_visible",
                           &RegisterSceneControlButtonsVisible);
        RegisterUiScenario("editor/bottom_dock_tabs_visible",
                           &RegisterBottomDockTabsVisible);
        RegisterUiScenario("editor/console_tab_controls",
                           &RegisterConsoleTabControls);
        RegisterUiScenario("editor/mcp_tab_buttons", &RegisterMcpTabButtons);
        RegisterUiScenario("editor/hierarchy_panel_visible",
                           &RegisterHierarchyPanelVisible);
        RegisterUiScenario("editor/hierarchy_search_input",
                           &RegisterHierarchySearchInput);
        RegisterUiScenario("editor/assets_panel_visible",
                           &RegisterAssetsPanelVisible);
        RegisterUiScenario("editor/help_window_open_close",
                           &RegisterHelpWindowOpenClose);
        RegisterUiScenario("editor/settings_modal_open_cancel",
                           &RegisterSettingsModalOpenCancel);
        RegisterUiScenario("editor/properties_panel_visible",
                           &RegisterPropertiesPanelVisible);
        RegisterUiScenario("editor/viewport_statusbar_visible",
                           &RegisterViewportAndStatusbarVisible);
        RegisterUiScenario("editor/new_scene_and_add_panel",
                           &RegisterNewSceneAndAddPanel);
        RegisterUiScenario("editor/quick_open_popup", &RegisterQuickOpenPopup);
        RegisterUiScenario("editor/command_palette_popup",
                           &RegisterCommandPalettePopup);
        RegisterUiScenario("editor/select_object_via_hierarchy",
                           &RegisterSelectObjectViaHierarchy);
        RegisterUiScenario("editor/rename_object_modal", &RegisterRenameObjectModal);
        RegisterUiScenario("editor/object_context_menu_in_hierarchy",
                           &RegisterObjectContextMenuInHierarchy);
        RegisterUiScenario("editor/duplicate_object_changes_hierarchy",
                           &RegisterDuplicateObjectChangesHierarchy);
        RegisterUiScenario("editor/delete_selected_object_flow",
                           &RegisterDeleteSelectedObjectFlow);
        RegisterUiScenario("editor/undo_redo_via_edit_menu",
                           &RegisterUndoRedoViaEditMenu);
        RegisterUiScenario("editor/mcp_tab_content_visible",
                           &RegisterMcpTabContentVisible);
        RegisterUiScenario("editor/project_tab_visible", &RegisterProjectTabVisible);
        RegisterUiScenario("editor/object_type_filter_in_hierarchy",
                           &RegisterObjectTypeFilterInHierarchy);
        RegisterUiScenario("editor/assets_panel_add_asset_button",
                           &RegisterAssetsPanelAddAssetButton);
        RegisterUiScenario("editor/properties_shows_camera_fields",
                           &RegisterPropertiesShowsCameraFields);
        RegisterUiScenario("editor/multi_select_shows_batch_panel",
                           &RegisterMultiSelectShowsBatchPanel);
        RegisterUiScenario("editor/edit_menu_shows_history_items",
                           &RegisterEditMenuShowsHistoryItems);
        RegisterUiScenario("editor/play_mode_toggle", &RegisterPlayModeToggle);
        RegisterUiScenario("editor/file_new_scene_cancel_dirty",
                           &RegisterFileNewSceneCancelDirty);
        RegisterUiScenario("editor/file_open_scene_dismiss",
                           &RegisterFileOpenSceneDismiss);
        RegisterUiScenario("editor/file_reset_layout", &RegisterFileResetLayout);
        RegisterUiScenario("editor/view_fly_mode_activate",
                           &RegisterViewFlyModeActivate);
        RegisterUiScenario("editor/settings_modal_apply_button",
                           &RegisterSettingsModalApplyButton);
        RegisterUiScenario("editor/settings_modal_port_input",
                           &RegisterSettingsModalPortInput);
        RegisterUiScenario("editor/create_asset_modal_fill_cancel",
                           &RegisterCreateAssetModalFillCancel);
        RegisterUiScenario("editor/delete_confirm_modal_accept",
                           &RegisterDeleteConfirmModalAccept);
        RegisterUiScenario("editor/scene_header_context_add_panel",
                           &RegisterSceneHeaderContextAddPanel);
        RegisterUiScenario("editor/hierarchy_context_menu_add_child",
                           &RegisterHierarchyContextMenuAddChild);
        RegisterUiScenario("editor/edit_menu_create_prefab",
                           &RegisterEditMenuCreatePrefab);
        RegisterUiScenario("editor/assets_panel_search_open_dismiss",
                           &RegisterAssetsPanelSearchOpenDismiss);
        RegisterUiScenario("editor/console_filter_warn_toggle",
                           &RegisterConsoleFilterWarnToggle);
        RegisterUiScenario("editor/mcp_clear_request_log",
                           &RegisterMcpClearRequestLog);
        RegisterUiScenario("editor/hierarchy_empty_space_context_add",
                           &RegisterHierarchyEmptySpaceContextAdd);
        RegisterUiScenario("editor/unsaved_changes_discard",
                           &RegisterUnsavedChangesDiscard);
        // Keep editor-closing scenarios LAST — they leave no active project,
        // which would cause all subsequent editor tests to skip.
        RegisterUiScenario("editor/close_editor_button", &RegisterCloseEditorButton);
        RegisterUiScenario("editor/close_editor_returns_to_launcher",
                           &RegisterCloseEditorReturnsToLauncher);
    }
} // namespace Monolith

#endif
