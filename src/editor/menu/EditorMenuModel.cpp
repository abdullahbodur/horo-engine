#include "Horo/Editor/EditorMenuModel.h"

#include <initializer_list>
#include <utility>

namespace Horo::Editor
{
namespace
{
EditorMenuItem Command(const std::string_view labelKey, const EditorMenuAction action = EditorMenuAction::None,
                       const bool enabled = false, const std::string_view shortcut = {},
                       const std::string_view macKeyEquivalent = {})
{
    return EditorMenuItem{EditorMenuItemKind::Command, labelKey, action, shortcut, macKeyEquivalent, enabled, {}};
}

EditorMenuItem Separator()
{
    return EditorMenuItem{.kind = EditorMenuItemKind::Separator};
}

EditorMenuItem Submenu(const std::string_view labelKey, std::initializer_list<EditorMenuItem> children)
{
    EditorMenuItem item;
    item.kind = EditorMenuItemKind::Submenu;
    item.labelKey = labelKey;
    item.enabledByDefault = true;
    item.children.assign(children);
    return item;
}
} // namespace

/** @copydoc GetEditorMenuModel */
const EditorMenuModel &GetEditorMenuModel()
{
    static const EditorMenuModel model{{
        Submenu("web_workspace.menu.file",
                {
                    Command("web_workspace.menu.new_project", EditorMenuAction::NewProject, true, "Ctrl+N", "n"),
                    Command("web_workspace.menu.open_project", EditorMenuAction::OpenProject, true, "Ctrl+O", "o"),
                    Command("web_workspace.menu.open_recent"),
                    Separator(),
                    Command("web_workspace.menu.save", EditorMenuAction::SaveScene, true, "Ctrl+S", "s"),
                    Command("web_workspace.menu.save_as"),
                    Separator(),
                    Command("web_workspace.menu.editor_settings", EditorMenuAction::OpenEditorSettings, true, "Ctrl+,",
                            ","),
                    Command("web_workspace.menu.exit", EditorMenuAction::ExitApplication, true, "Alt+F4", "q"),
                }),
        Submenu("web_workspace.menu.edit",
                {
                    Command("web_workspace.menu.undo"),
                    Command("web_workspace.menu.redo"),
                    Separator(),
                    Command("web_workspace.menu.cut"),
                    Command("web_workspace.menu.copy"),
                    Command("web_workspace.menu.paste"),
                    Separator(),
                    Command("web_workspace.menu.preferences"),
                }),
        Submenu("web_workspace.menu.assets",
                {
                    Command("web_workspace.menu.import_asset"),
                    Command("web_workspace.menu.reimport_all"),
                    Command("web_workspace.menu.refresh_asset_index"),
                }),
        Submenu("web_workspace.menu.game_object",
                {
                    Command("web_workspace.menu.create_empty"),
                    Command("web_workspace.menu.create_primitive"),
                    Command("web_workspace.menu.create_light"),
                    Separator(),
                    Command("web_workspace.menu.character_setup"),
                }),
        Submenu("web_workspace.menu.component", {Command("web_workspace.menu.add_component")}),
        Submenu("web_workspace.menu.window",
                {
                    Submenu("web_workspace.menu.workspace",
                            {
                                Command("web_workspace.menu.editor_workspace"),
                                Command("web_workspace.menu.ui_canvas_editor"),
                                Command("web_workspace.menu.cinematic_sequencer"),
                            }),
                    Submenu("web_workspace.menu.asset_editors",
                            {
                                Command("web_workspace.menu.animation_editor"),
                                Command("web_workspace.menu.material_editor"),
                                Command("web_workspace.menu.particle_editor"),
                                Command("web_workspace.menu.prefab_editor"),
                                Command("web_workspace.menu.shader_graph"),
                            }),
                    Submenu("web_workspace.menu.tools",
                            {
                                Command("web_workspace.menu.asset_browser"),
                                Command("web_workspace.menu.plugin_manager"),
                                Command("web_workspace.menu.package_manager"),
                                Command("web_workspace.menu.primitives_panel"),
                                Command("web_workspace.menu.decal_placement"),
                                Command("web_workspace.menu.destruction_setup"),
                                Command("web_workspace.menu.save_load_manager"),
                                Command("web_workspace.menu.post_processing_stack"),
                                Command("web_workspace.menu.xr_setup"),
                            }),
                    Submenu("web_workspace.menu.diagnostics",
                            {
                                Command("web_workspace.menu.physics_debugger"),
                                Command("web_workspace.menu.network_debugger"),
                                Command("web_workspace.menu.navigation_bake"),
                                Command("web_workspace.menu.lod_debugger"),
                                Command("web_workspace.menu.virtual_texturing_debug"),
                                Command("web_workspace.menu.observability_dashboard"),
                            }),
                    Submenu("web_workspace.menu.runtime",
                            {
                                Command("web_workspace.menu.mcp_panel"),
                                Command("web_workspace.menu.console_panel"),
                                Command("web_workspace.menu.audio_mixer"),
                                Command("web_workspace.menu.input_mapping_editor"),
                                Command("web_workspace.menu.pcg_graph_editor"),
                                Command("web_workspace.menu.gameplay_behavior_editor"),
                            }),
                    Separator(),
                    Command("web_workspace.menu.localization_editor"),
                }),
        Submenu("web_workspace.menu.build",
                {
                    Command("web_workspace.menu.build_release"),
                    Separator(),
                    Command("web_workspace.menu.build_output"),
                    Command("web_workspace.menu.project_settings"),
                    Command("web_workspace.menu.render_settings"),
                    Command("web_workspace.menu.platform_services"),
                    Command("web_workspace.menu.gameplay_integration"),
                    Separator(),
                    Command("web_workspace.menu.clean_build"),
                    Command("web_workspace.menu.validate_project"),
                }),
        Submenu("web_workspace.menu.help", {Command("web_workspace.menu.documentation")}),
    }};
    return model;
}
} // namespace Horo::Editor
