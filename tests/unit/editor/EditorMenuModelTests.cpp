#include "Horo/Editor/EditorMenuModel.h"

#include <array>
#include <cassert>
#include <string_view>

using namespace Horo::Editor;

namespace
{
const EditorMenuItem *FindChild(const EditorMenuItem &parent, const std::string_view labelKey)
{
    for (const EditorMenuItem &child : parent.children)
    {
        if (child.labelKey == labelKey)
        {
            return &child;
        }
    }
    return nullptr;
}

const EditorMenuItem *FindAction(const EditorMenuItem &parent, const EditorMenuAction action)
{
    if (parent.action == action)
    {
        return &parent;
    }
    for (const EditorMenuItem &child : parent.children)
    {
        if (const EditorMenuItem *match = FindAction(child, action))
        {
            return match;
        }
    }
    return nullptr;
}

void MirrorsTheWorkspaceDesignMenuHierarchy()
{
    const EditorMenuModel &model = GetEditorMenuModel();
    constexpr std::array expectedTopLevelKeys = {
        "web_workspace.menu.file",        "web_workspace.menu.edit",      "web_workspace.menu.assets",
        "web_workspace.menu.game_object", "web_workspace.menu.component", "web_workspace.menu.window",
        "web_workspace.menu.build",       "web_workspace.menu.help",
    };

    assert(model.menus.size() == expectedTopLevelKeys.size());
    for (std::size_t index = 0; index < expectedTopLevelKeys.size(); ++index)
    {
        assert(model.menus[index].kind == EditorMenuItemKind::Submenu);
        assert(model.menus[index].labelKey == expectedTopLevelKeys[index]);
    }

    const EditorMenuItem *workspace = FindChild(model.menus[5], "web_workspace.menu.workspace");
    const EditorMenuItem *assetEditors = FindChild(model.menus[5], "web_workspace.menu.asset_editors");
    const EditorMenuItem *tools = FindChild(model.menus[5], "web_workspace.menu.tools");
    const EditorMenuItem *diagnostics = FindChild(model.menus[5], "web_workspace.menu.diagnostics");
    const EditorMenuItem *runtime = FindChild(model.menus[5], "web_workspace.menu.runtime");
    assert(workspace != nullptr && workspace->kind == EditorMenuItemKind::Submenu);
    assert(assetEditors != nullptr && assetEditors->kind == EditorMenuItemKind::Submenu);
    assert(tools != nullptr && tools->kind == EditorMenuItemKind::Submenu);
    assert(diagnostics != nullptr && diagnostics->kind == EditorMenuItemKind::Submenu);
    assert(runtime != nullptr && runtime->kind == EditorMenuItemKind::Submenu);
}

void DistinguishesImplementedAndPlaceholderCommands()
{
    const EditorMenuModel &model = GetEditorMenuModel();
    const EditorMenuItem *save = FindAction(model.menus[0], EditorMenuAction::SaveScene);
    const EditorMenuItem *exit = FindAction(model.menus[0], EditorMenuAction::ExitApplication);
    const EditorMenuItem *newProject = FindAction(model.menus[0], EditorMenuAction::NewProject);
    const EditorMenuItem *openProject = FindAction(model.menus[0], EditorMenuAction::OpenProject);
    const EditorMenuItem *undo = FindAction(model.menus[1], EditorMenuAction::Undo);
    const EditorMenuItem *redo = FindAction(model.menus[1], EditorMenuAction::Redo);
    assert(save != nullptr && save->enabledByDefault);
    assert(exit != nullptr && exit->enabledByDefault);
    assert(newProject != nullptr && newProject->enabledByDefault);
    assert(openProject != nullptr && openProject->enabledByDefault);
    assert(undo != nullptr && undo->enabledByDefault);
    assert(redo != nullptr && redo->enabledByDefault);

    const EditorMenuItem *reimport = FindChild(model.menus[2], "web_workspace.menu.reimport_all");
    assert(reimport != nullptr);
    assert(reimport->action == EditorMenuAction::None);
    assert(!reimport->enabledByDefault);
}

void BuildsTheSharedCatalogCreateTree()
{
    const std::vector<EditorMenuItem> &items = GetPrimitiveCreateMenuItems();
    assert(items.size() == 6);
    assert(items[0].labelKey == "primitive.object.empty");
    constexpr std::array expectedGroups = {"workspace.create.group.objects_3d", "workspace.create.group.cameras",
                                           "workspace.create.group.lights", "workspace.create.group.volumes",
                                           "workspace.create.group.audio"};
    std::size_t primitiveCount = 1;
    for (std::size_t index = 0; index < expectedGroups.size(); ++index)
    {
        assert(items[index + 1].kind == EditorMenuItemKind::Submenu);
        assert(items[index + 1].labelKey == expectedGroups[index]);
        primitiveCount += items[index + 1].children.size();
        for (const EditorMenuItem &primitive : items[index + 1].children)
        {
            assert(primitive.action == EditorMenuAction::CreatePrimitive);
            assert(primitive.primitive.has_value());
            assert(primitive.enabledByDefault);
            assert(!primitive.iconToken.empty());
            assert(primitive.primitive->value.find("primitive.collider.") == std::string_view::npos);
        }
    }
    assert(primitiveCount == 14);

    const EditorMenuItem *create = FindChild(GetEditorMenuModel().menus[3], "workspace.create");
    assert(create != nullptr && create->kind == EditorMenuItemKind::Submenu);
    assert(create->children.size() == items.size());
}
} // namespace

int main()
{
    MirrorsTheWorkspaceDesignMenuHierarchy();
    DistinguishesImplementedAndPlaceholderCommands();
    BuildsTheSharedCatalogCreateTree();
    return 0;
}
