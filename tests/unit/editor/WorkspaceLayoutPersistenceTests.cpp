#include <catch2/catch_test_macros.hpp>

#include "Horo/Editor/WorkspaceLayoutPersistence.h"
#include "Horo/Editor/WorkspacePanelHost.h"

#include <filesystem>

using namespace Horo::Editor;

TEST_CASE("Workspace Layout Persistence Tests", "[unit][editor]")
{
    WorkspacePanelHost host;
    const auto json = WorkspaceLayoutPersistence::Serialize(host.Layout());
    std::string error;
    const auto restored = WorkspaceLayoutPersistence::Deserialize(json, &error);
    REQUIRE((restored.has_value()));
    REQUIRE((restored->Validate().empty()));
    REQUIRE((restored->FindTabStack("workspace.document") != nullptr));

    REQUIRE((!WorkspaceLayoutPersistence::Deserialize("{\"schemaVersion\":99,\"root\":{}}", &error)));
    REQUIRE((!error.empty()));
    REQUIRE((!WorkspaceLayoutPersistence::Deserialize("not json", &error)));

    const auto path = std::filesystem::temp_directory_path() / "horo_workspace_layout_test.json";
    REQUIRE((WorkspaceLayoutPersistence::Save(path, host.Layout(), &error)));
    REQUIRE((WorkspaceLayoutPersistence::Load(path, &error).has_value()));
    REQUIRE((host.RestoreLayout(path, &error)));
    std::filesystem::remove(path);
    REQUIRE((!host.RestoreLayout(path, &error)));
    REQUIRE((host.Layout().FindTabStack("workspace.document") != nullptr));
}
