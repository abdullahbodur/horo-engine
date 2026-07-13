#include "Horo/Editor/WorkspaceLayoutPersistence.h"
#include "Horo/Editor/WorkspacePanelHost.h"

#include <cassert>
#include <filesystem>

using namespace Horo::Editor;

int main()
{
    WorkspacePanelHost host;
    const auto json = WorkspaceLayoutPersistence::Serialize(host.Layout());
    std::string error;
    const auto restored = WorkspaceLayoutPersistence::Deserialize(json, &error);
    assert(restored.has_value());
    assert(restored->Validate().empty());
    assert(restored->FindTabStack("workspace.document") != nullptr);

    assert(!WorkspaceLayoutPersistence::Deserialize("{\"schemaVersion\":99,\"root\":{}}", &error));
    assert(!error.empty());
    assert(!WorkspaceLayoutPersistence::Deserialize("not json", &error));

    const auto path = std::filesystem::temp_directory_path() / "horo_workspace_layout_test.json";
    assert(WorkspaceLayoutPersistence::Save(path, host.Layout(), &error));
    assert(WorkspaceLayoutPersistence::Load(path, &error).has_value());
    assert(host.RestoreLayout(path, &error));
    std::filesystem::remove(path);
    assert(!host.RestoreLayout(path, &error));
    assert(host.Layout().FindTabStack("workspace.document") != nullptr);
    return 0;
}
