#include "Horo/Assets/AssetImporter.h"
#include "Horo/Editor/AssetImportModal.h"
#include "Horo/Foundation/JobSystem.h"
#include "Horo/Foundation/Paths.h"
#include "helpers/editor_ui/HeadlessEditorGuiFixture.h"

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <memory>

namespace {
    std::shared_ptr<const Horo::Assets::AssetImporterCatalogSnapshot> MakeCatalog() {
        using namespace Horo::Assets;

        AssetImporterContribution contribution{
            .contributionId = "horo.builtin.obj-mesh",
            .packageId = "horo.builtin.assets",
            .moduleId = "horo.assets.obj",
            .moduleVersion = "1.0.0",
            .version = "1.0.0",
            .fileExtensions = {"obj"},
            .assetTypes = {AssetTypeId::Parse("core.mesh").Value()},
            .settings =
                {
                    {.id = "optimize",
                     .labelKey = "Optimize",
                     .descriptionKey = "Optimize mesh data",
                     .kind = ImportSettingKind::Boolean,
                     .defaultValue = true},
                    {.id = "lod-count",
                     .labelKey = "LOD count",
                     .descriptionKey = "Generated detail levels",
                     .kind = ImportSettingKind::Integer,
                     .defaultValue = std::int64_t{3}},
                    {.id = "scale",
                     .labelKey = "Scale",
                     .descriptionKey = "Import scale",
                     .kind = ImportSettingKind::Float,
                     .defaultValue = 1.0},
                    {.id = "tag",
                     .labelKey = "Tag",
                     .descriptionKey = "Source tag",
                     .kind = ImportSettingKind::Text,
                     .defaultValue = std::string{"environment"}},
                    {.id = "normals",
                     .labelKey = "Normals",
                     .descriptionKey = "Normal generation policy",
                     .kind = ImportSettingKind::Choice,
                     .defaultValue = std::size_t{0},
                     .choices = {{.id = "source", .labelKey = "Source", .value = std::size_t{0}},
                                 {.id = "generate", .labelKey = "Generate", .value = std::size_t{1}}}},
                },
            .builtIn = true,
        };
        return std::make_shared<const AssetImporterCatalogSnapshot>(std::vector<AssetImporterContribution>{std::move(contribution)});
    }

    Horo::Assets::AssetImportItem MakeItem() {
        using namespace Horo;
        using namespace Horo::Assets;

        AssetImportItem item{
            .sourceFile = ProjectPath::Parse("source/scene.obj").Value(),
            .absoluteSourcePath = "/tmp/source/scene.obj",
        };
        item.importerContributionId = "horo.builtin.obj-mesh";
        item.importerVersion = "1.0.0";
        item.importerPackageId = "horo.builtin.assets";
        item.importerModuleId = "horo.assets.obj";
        item.importerModuleVersion = "1.0.0";
        item.resolvedType = AssetTypeId::Parse("core.mesh").Value();
        item.sourceExtension = "obj";
        item.displayName = "scene";
        item.destinationFolder = "assets/Meshes";
        item.diagnostics = {
            {.severity = ImportDiagnostic::Severity::Info, .code = "mesh.info", .message = "scene.obj: source metadata read"},
            {.severity = ImportDiagnostic::Severity::Warning,
             .code = "mesh.warning",
             .message = "scene: missing tangents; generation requested"},
            {.severity = ImportDiagnostic::Severity::Error, .code = "mesh.error", .message = "Malformed optional group"},
        };
        return item;
    }

    void DrawFrame(Horo::Editor::Tests::HeadlessEditorGuiFixture &imgui, Horo::Editor::AssetImportModal &modal) {
        imgui.BeginFrame();
        static_cast<void>(modal.Draw());
        imgui.EndFrame();
    }

    void ClickTab(Horo::Editor::Tests::HeadlessEditorGuiFixture &imgui, Horo::Editor::AssetImportModal &modal, const float x) {
        ImGuiIO &io = ImGui::GetIO();
        io.AddMousePosEvent(x, 165.0F);
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
        DrawFrame(imgui, modal);
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
        DrawFrame(imgui, modal);
    }
}  // namespace

TEST_CASE("Asset import presentation renders queue diagnostics settings destination and terminal phases",
          "[unit][editor][gui][asset-import]") {
    using namespace Horo;
    using namespace Horo::Assets;
    using namespace Horo::Editor;

    Tests::HeadlessEditorGuiFixture imgui;
    JobSystem jobs{JobSystemConfig{.workerCount = 1, .maxQueuedJobs = 8}};
    AssetImportModal modal{imgui.Fonts(), jobs, MakeCatalog()};
    auto &snapshot = modal.MutableSnapshot();
    snapshot.items = {MakeItem()};
    snapshot.phase = AssetImportPhase::Selecting;
    snapshot.canCancel = true;

    DrawFrame(imgui, modal);
    ClickTab(imgui, modal, 315.0F);
    ClickTab(imgui, modal, 445.0F);
    ClickTab(imgui, modal, 590.0F);

    snapshot.items.front().result = PreparedAssetImport{.type = AssetTypeId::Parse("core.mesh").Value(), .editorPayload = {1, 2, 3}};
    snapshot.items.front().diagnostics.clear();
    static constexpr std::array phases{AssetImportPhase::Preparing, AssetImportPhase::ReadyToCommit, AssetImportPhase::Committing,
                                       AssetImportPhase::Completed, AssetImportPhase::Failed,        AssetImportPhase::Cancelled};
    for (const AssetImportPhase phase : phases) {
        snapshot.phase = phase;
        DrawFrame(imgui, modal);
    }

    REQUIRE(snapshot.items.size() == 1);
    REQUIRE(snapshot.items.front().displayName == "scene");
    REQUIRE(snapshot.items.front().result.has_value());
    jobs.Shutdown(ShutdownPolicy::Cancel);
}

TEST_CASE("Asset import presentation handles empty and unresolved importer selections", "[unit][editor][gui][asset-import]") {
    using namespace Horo;
    using namespace Horo::Assets;
    using namespace Horo::Editor;

    Tests::HeadlessEditorGuiFixture imgui;
    JobSystem jobs{JobSystemConfig{.workerCount = 1, .maxQueuedJobs = 8}};
    AssetImportModal modal{imgui.Fonts(), jobs, MakeCatalog()};
    DrawFrame(imgui, modal);

    auto unresolved = MakeItem();
    unresolved.sourceFile = ProjectPath::Parse("source/material.unknown").Value();
    unresolved.sourceExtension = "unknown";
    unresolved.importerContributionId.clear();
    modal.MutableSnapshot().items = {std::move(unresolved)};
    modal.MutableSnapshot().selectedItemIndex = 0;
    ClickTab(imgui, modal, 445.0F);

    REQUIRE(modal.Snapshot().items.front().importerContributionId.empty());
    jobs.Shutdown(ShutdownPolicy::Cancel);
}
