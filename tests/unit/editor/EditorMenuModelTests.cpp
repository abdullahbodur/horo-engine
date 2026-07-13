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
    assert(save != nullptr && save->enabledByDefault);
    assert(exit != nullptr && exit->enabledByDefault);
    assert(newProject != nullptr && newProject->enabledByDefault);
    assert(openProject != nullptr && openProject->enabledByDefault);

    const EditorMenuItem *reimport = FindChild(model.menus[2], "web_workspace.menu.reimport_all");
    assert(reimport != nullptr);
    assert(reimport->action == EditorMenuAction::None);
    assert(!reimport->enabledByDefault);
}
} // namespace

int main()
{
    MirrorsTheWorkspaceDesignMenuHierarchy();
    DistinguishesImplementedAndPlaceholderCommands();
    return 0;
}
