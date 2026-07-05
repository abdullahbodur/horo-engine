/** @file UiAutomationConfig.cpp
 *  @brief Implements UI automation configuration parsing and suite selection helpers. */
#include "ui/launcher/UiAutomationConfig.h"

#include <array>
#include <charconv>

namespace Horo {
    namespace {
        /** @brief Declares a named suite and its scenario filter expression. */
        struct UiAutomationTestSuiteDefinition {
            std::string_view name;
            std::string_view filter;
        };

        /** @brief Built-in UI automation suite definitions keyed by public suite name. */
        constexpr std::array kUiAutomationTestSuites{
            UiAutomationTestSuiteDefinition{
                "launcher-basic",
                "launcher/*,editor/toolbar_buttons_visible,editor/file_menu_items,"
                "editor/add_menu_items,editor/edit_menu_items,editor/view_menu_items,"
                "editor/scene_control_buttons_visible,editor/bottom_dock_tabs_visible,"
                "editor/console_tab_controls,editor/mcp_tab_buttons,"
                "editor/hierarchy_panel_visible,editor/hierarchy_search_input,"
                "editor/assets_panel_visible,editor/help_window_open_close,"
                "editor/settings_modal_open_cancel,editor/properties_panel_visible"},
            UiAutomationTestSuiteDefinition{
                "properties-workflows",
                "launcher/create_project_from_launcher,"
                "editor/properties_panel_light_controls_workflow,"
                "editor/properties_panel_mixed_selection_workflow,"
                "editor/properties_panel_rename_delete_undo_workflow,"
                "editor/properties_panel_scene_save_reload_workflow,"
                "editor/viewport_statusbar_visible,editor/new_scene_and_add_panel,"
                "editor/quick_open_popup,editor/command_palette_popup,"
                "editor/select_object_via_hierarchy,editor/rename_object_modal,"
                "editor/object_context_menu_in_hierarchy,"
                "editor/duplicate_object_changes_hierarchy,"
                "editor/delete_selected_object_flow,editor/undo_redo_via_edit_menu"},
            UiAutomationTestSuiteDefinition{
                "mcp-project",
                "launcher/create_project_from_launcher,editor/project_tab_visible,"
                "editor/object_type_filter_in_hierarchy,"
                "editor/assets_panel_add_asset_button,"
                "editor/properties_shows_camera_fields,"
                "editor/multi_select_shows_batch_panel,"
                "editor/edit_menu_shows_history_items,editor/play_mode_toggle,"
                "editor/file_new_scene_cancel_dirty,editor/file_open_scene_dismiss,"
                "editor/file_reset_layout,editor/view_fly_mode_activate"},
            UiAutomationTestSuiteDefinition{
                "modals-mcp",
                "launcher/create_project_from_launcher,"
                "editor/settings_modal_apply_button,editor/settings_modal_port_input,"
                "editor/create_asset_modal_fill_cancel,"
                "editor/delete_confirm_modal_accept,"
                "editor/scene_header_context_add_panel,"
                "editor/hierarchy_context_menu_add_child,editor/edit_menu_create_prefab,"
                "editor/assets_panel_search_open_dismiss,"
                "editor/console_filter_warn_toggle,"
                "editor/hierarchy_empty_space_context_add,"
                "editor/unsaved_changes_discard,"
                "editor/mcp_enable_and_verify_running,editor/mcp_tab_content_visible,"
                "editor/mcp_send_request_and_verify_log,"
                "editor/mcp_live_request_visibility,"
                "editor/mcp_request_detail_fields_visible,"
                "editor/mcp_send_tool_call_verify_method,"
                "editor/mcp_catalog_shows_tools,editor/mcp_open_settings_from_tab,"
                "editor/mcp_clear_request_log"},
            UiAutomationTestSuiteDefinition{
                "properties-close",
                "launcher/create_project_from_launcher,"
                "editor/properties_panel_no_selection,"
                "editor/properties_panel_panel_object_transform,"
                "editor/properties_panel_light_object_fields,"
                "editor/properties_panel_identity_section,"
                "editor/console_info_checkbox_toggle,"
                "editor/console_error_checkbox_toggle,"
                "editor/add_prop_object_and_check_properties,"
                "editor/hierarchy_add_multiple_then_multi_select,"
                "editor/close_editor_button,editor/close_editor_returns_to_launcher"},
        };
    } // namespace

    /** @copydoc ShouldLogUiAutomationHeartbeat */
    bool ShouldLogUiAutomationHeartbeat(const bool enabled,
                                        const int frameCount,
                                        const int heartbeatInterval) {
        return enabled && heartbeatInterval > 0 &&
               (frameCount == 1 || (frameCount % heartbeatInterval) == 0);
    }

    /** @copydoc ShouldWarnUiAutomationLargeFrameDelta */
    bool ShouldWarnUiAutomationLargeFrameDelta(const double frameDeltaSec) {
        return frameDeltaSec > kUiAutomationLargeFrameDeltaWarningSec;
    }

    /** @copydoc ShouldLogEditorRenderHeartbeat */
    bool ShouldLogEditorRenderHeartbeat(const bool enabled,
                                        const int frameCount) {
        return enabled && (frameCount == 1 || (frameCount % 60) == 0);
    }

    /** @copydoc UiAutomationTestSuiteNames */
    std::vector<std::string_view> UiAutomationTestSuiteNames() {
        std::vector<std::string_view> names;
        names.reserve(kUiAutomationTestSuites.size());
        for (const UiAutomationTestSuiteDefinition &suite : kUiAutomationTestSuites)
            names.push_back(suite.name);
        return names;
    }

    /** @copydoc ResolveUiAutomationScenarioFilter */
    std::string_view ResolveUiAutomationScenarioFilter(
        const std::string_view explicitFilter, const std::string_view suiteName) {
        if (!explicitFilter.empty())
            return explicitFilter;
        for (const UiAutomationTestSuiteDefinition &suite : kUiAutomationTestSuites) {
            if (suite.name == suiteName)
                return suite.filter;
        }
        if (!suiteName.empty())
            return "__invalid_ui_shard__";
        return {};
    }

    /** @copydoc ParseUiAutomationBoolValue */
    bool ParseUiAutomationBoolValue(std::string_view value, bool fallback) {
        if (value.empty())
            return fallback;
        return value != "0";
    }

    /** @copydoc ParseUiAutomationNonNegativeIntValue */
    int ParseUiAutomationNonNegativeIntValue(std::string_view value, int fallback) {
        if (value.empty())
            return fallback;

        int parsed = 0;
        const char *first = value.data();
        const char *last = first + value.size();
        const auto [ptr, ec] = std::from_chars(first, last, parsed);
        if (ec != std::errc() || ptr != last || parsed < 0)
            return fallback;
        return parsed;
    }

    /** @copydoc ResolveUiCaptureOutputDir */
    std::filesystem::path
    ResolveUiCaptureOutputDir(const bool captureEnabled,
                              const std::string_view outputDirEnv,
                              const std::filesystem::path &currentPath) {
        if (!captureEnabled)
            return {};
        if (!outputDirEnv.empty())
            return std::filesystem::path(outputDirEnv);
        return currentPath / "ui_test_output";
    }

    /** @copydoc SelectUiAutomationBaseDir */
    std::filesystem::path SelectUiAutomationBaseDir(
        const std::string_view homePath, const std::string_view userProfilePath,
        const std::filesystem::path &currentPath, const bool isWindows) {
        if (isWindows) {
            if (!userProfilePath.empty())
                return std::filesystem::path(userProfilePath);
            if (!homePath.empty())
                return std::filesystem::path(homePath);
            return currentPath;
        }

        if (!homePath.empty())
            return std::filesystem::path(homePath);
        return currentPath;
    }
} // namespace Horo
