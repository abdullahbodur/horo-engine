#include <catch2/catch_test_macros.hpp>

#include "Horo/Editor/AssetImportModal.h"
#include "Horo/Assets/AssetImporter.h"
#include "Horo/Editor/EditorDataBus.h"
#include "Horo/Editor/EditorModalHost.h"
#include "Horo/Foundation/JobSystem.h"
#include "Horo/Runtime/Input.h"

#include <functional>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <vector>

namespace
{
using namespace Horo;
using namespace Horo::Editor;
using namespace Horo::Assets;

class TestImporter final : public IAssetImporter
{
  public:
    [[nodiscard]] Result<PreparedAssetImport> Import(
        const AssetImportInput &input,
        const CancellationToken &cancellation) const override
    {
        PreparedAssetImport result;
        result.type = AssetTypeId::Parse("core.mesh").Value();
        result.editorPayload.assign(input.sourceBytes.begin(), input.sourceBytes.end());
        return Result<PreparedAssetImport>::Success(std::move(result));
    }
};

/** @brief Test double that overrides Draw for headless testing. */
class TestAssetImportModal : public AssetImportModal
{
  public:
    using AssetImportModal::AssetImportModal;

    ModalFrameResult Draw() override
    {
        if (!m_preparedCalled && m_prepareFn)
        {
            m_prepareFn();
            m_preparedCalled = true;
        }
        return ModalFrameResult::None();
    }

    std::function<void()> m_prepareFn;
    bool m_preparedCalled{false};
};

} // namespace

TEST_CASE("AssetImportModal lifecycle: open, begin, prepare, close", "[native]")
{
    EditorDataBus events;
    Input::InputRouter inputRouter;
    EditorModalHost modalHost{events, inputRouter};
    const auto &fonts = *reinterpret_cast<const Theme::Fonts *>(static_cast<std::uintptr_t>(1));
    JobSystem jobs;

    // Build importer catalog
    AssetImporterCatalog catalog;
    REQUIRE((catalog
                 .Register(AssetImporterContribution{
                     .contributionId = "test.obj",
                     .packageId = "test",
                     .moduleId = "test",
                     .moduleVersion = "1.0.0",
                     .version = "1.0.0",
                     .fileExtensions = {"obj"},
                     .assetTypes = {AssetTypeId::Parse("core.mesh").Value()},
                     .strategy = std::make_shared<const TestImporter>(),
                 })
                 .HasValue()));
    auto catSnapshot = catalog.Publish();
    REQUIRE((catSnapshot.HasValue()));

    auto modal = std::make_unique<TestAssetImportModal>(fonts, jobs, catSnapshot.Value());
    auto *modalPtr = modal.get();

    bool prepared = false;
    modalPtr->m_prepareFn = [&]() {
        CancellationToken cancellation;
        auto result = modalPtr->ImportSingleItem(0, cancellation);
        REQUIRE((result.HasValue()));
        prepared = true;
    };

    auto openResult = modalHost.OpenRoot(std::move(modal));
    REQUIRE((openResult.HasValue()));

    // Open the modal to run OnOpen
    modalHost.OnUpdate(0.016f);

    // Begin import
    CancellationToken cancellation;
    auto beginResult = modalPtr->BeginImport(
        {std::filesystem::path("/tmp/test/cube.obj")},
        std::filesystem::path("/tmp/test"),
        cancellation);
    REQUIRE((beginResult.HasValue()));

    auto &snap = modalPtr->Snapshot();
    REQUIRE((snap.items.size() == 1));

    // Draw triggers prepare
    modalHost.Draw();

    REQUIRE((prepared));

    // Close
    auto closeResult = modalHost.RequestClose(modalPtr->Id(), ModalCloseReason::Completed);
    REQUIRE((closeResult.HasValue()));
    modalHost.OnUpdate(0.016f);
}

TEST_CASE("AssetImportModal presets are scoped by importer contribution and extension", "[native]")
{
    const auto &fonts = *reinterpret_cast<const Theme::Fonts *>(static_cast<std::uintptr_t>(1));
    JobSystem jobs;
    AssetImporterCatalog catalog;
    REQUIRE((catalog
                 .Register(AssetImporterContribution{
                     .contributionId = "test.mesh",
                     .packageId = "test",
                     .moduleId = "mesh",
                     .moduleVersion = "1.0.0",
                     .version = "1.0.0",
                     .fileExtensions = {"obj", "fbx"},
                     .assetTypes = {AssetTypeId::Parse("core.mesh").Value()},
                     .settings = {
                         ImportSettingDescriptor{
                             .id = "optimize",
                             .labelKey = "Optimize",
                             .descriptionKey = "",
                             .kind = ImportSettingKind::Boolean,
                             .defaultValue = false,
                             .includeInPresets = true,
                         },
                         ImportSettingDescriptor{
                             .id = "sourceTag",
                             .labelKey = "Source Tag",
                             .descriptionKey = "",
                             .kind = ImportSettingKind::Text,
                             .defaultValue = std::string{},
                             .includeInPresets = false,
                         },
                     },
                     .strategy = std::make_shared<const TestImporter>(),
                 })
                 .HasValue()));
    auto catalogSnapshot = catalog.Publish();
    REQUIRE((catalogSnapshot.HasValue()));

    TestAssetImportModal modal{fonts, jobs, catalogSnapshot.Value()};
    CancellationToken cancellation;
    REQUIRE((modal.BeginImport(
        {"/tmp/test/cube.obj", "/tmp/test/character.fbx"}, "/tmp/test", cancellation).HasValue()));

    auto& objItem = const_cast<AssetImportItem&>(modal.Snapshot().items[0]);
    objItem.displayName = "PresetIndependentName";
    objItem.destinationFolder = "assets/Characters";
    objItem.subfolderByType = 2;
    objItem.assetIdStrategy = 1;
    objItem.createMetaSidecar = false;
    objItem.overwriteWithoutPrompt = true;
    objItem.settings["settings.optimize"] = "true";
    objItem.settings["settings.sourceTag"] = "first-source";
    REQUIRE((modal.CreatePreset(0, "Fast")));
    REQUIRE((modal.ActivePresetName(0) == "Fast"));
    REQUIRE((modal.PresetNames(0) == std::vector<std::string>{"Default", "Fast"}));
    REQUIRE((modal.PresetNames(1) == std::vector<std::string>{"Default"}));

    objItem.displayName = "ChangedName";
    objItem.destinationFolder = "assets/Changed";
    objItem.subfolderByType = 0;
    objItem.assetIdStrategy = 0;
    objItem.createMetaSidecar = true;
    objItem.overwriteWithoutPrompt = false;
    objItem.settings["settings.optimize"] = "false";
    objItem.settings["settings.sourceTag"] = "second-source";
    REQUIRE((modal.ApplyPreset(0, "Fast")));
    REQUIRE((objItem.displayName == "ChangedName"));
    REQUIRE((objItem.destinationFolder == "assets/Characters"));
    REQUIRE((objItem.subfolderByType == 2));
    REQUIRE((objItem.assetIdStrategy == 1));
    REQUIRE((!objItem.createMetaSidecar));
    REQUIRE((objItem.overwriteWithoutPrompt));
    REQUIRE((objItem.settings["settings.optimize"] == "true"));
    REQUIRE((objItem.settings["settings.sourceTag"] == "second-source"));
    REQUIRE((modal.ApplyPreset(0, "Default")));
    REQUIRE((objItem.displayName == "ChangedName"));
    REQUIRE((objItem.destinationFolder.empty()));
    REQUIRE((objItem.settings["settings.optimize"] == "false"));
    REQUIRE((objItem.settings["settings.sourceTag"] == "second-source"));
}

TEST_CASE("AssetImportModal applies an absolute Content Browser destination", "[native]")
{
    const auto& fonts =
        *reinterpret_cast<const Theme::Fonts*>(static_cast<std::uintptr_t>(1));
    JobSystem jobs;
    AssetImporterCatalog catalog;
    REQUIRE((catalog
                 .Register(AssetImporterContribution{
                     .contributionId = "test.obj",
                     .packageId = "test",
                     .moduleId = "test",
                     .moduleVersion = "1.0.0",
                     .version = "1.0.0",
                     .fileExtensions = {"obj"},
                     .assetTypes = {AssetTypeId::Parse("core.mesh").Value()},
                     .strategy = std::make_shared<const TestImporter>(),
                 })
                 .HasValue()));
    auto catalogSnapshot = catalog.Publish();
    REQUIRE((catalogSnapshot.HasValue()));

    const std::filesystem::path projectRoot =
        std::filesystem::temp_directory_path() /
        ("horo-import-destination-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    const std::filesystem::path destination = projectRoot / "assets/Meshes";
    std::filesystem::create_directories(destination);

    TestAssetImportModal modal{fonts, jobs, catalogSnapshot.Value()};
    modal.SetProjectRoot(projectRoot);
    modal.SetDefaultDestination(destination);
    CancellationToken cancellation;
    REQUIRE((modal.BeginImport(
        {projectRoot / "source/cube.obj"}, projectRoot, cancellation).HasValue()));
    REQUIRE((modal.Snapshot().items.size() == 1));
    REQUIRE((modal.Snapshot().items[0].destinationFolder == "assets/Meshes"));

    std::error_code cleanupError;
    std::filesystem::remove_all(projectRoot, cleanupError);
}

TEST_CASE("AssetImportModal does not duplicate an already selected type folder", "[native]")
{
    const auto& fonts = *reinterpret_cast<const Theme::Fonts*>(static_cast<std::uintptr_t>(1));
    JobSystem jobs;
    AssetImporterCatalog catalog;
    REQUIRE((catalog.Register(AssetImporterContribution{
        .contributionId = "test.mesh",
        .packageId = "test",
        .moduleId = "mesh",
        .moduleVersion = "1.0.0",
        .version = "1.0.0",
        .fileExtensions = {"fbx"},
        .assetTypes = {AssetTypeId::Parse("core.mesh").Value()},
        .subfolderCategory = "Meshes",
        .strategy = std::make_shared<const TestImporter>(),
    }).HasValue()));
    auto catalogSnapshot = catalog.Publish();
    REQUIRE(catalogSnapshot.HasValue());

    const std::filesystem::path projectRoot =
        std::filesystem::temp_directory_path() /
        ("horo-import-destination-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    const std::filesystem::path sourcePath = projectRoot / "source.fbx";
    std::filesystem::create_directories(projectRoot / "assets/Meshes");
    {
        std::ofstream source(sourcePath, std::ios::binary);
        source << "fbx";
    }

    TestAssetImportModal modal{fonts, jobs, catalogSnapshot.Value()};
    CancellationToken cancellation;
    REQUIRE((modal.BeginImport({sourcePath}, projectRoot, cancellation).HasValue()));
    auto& item = const_cast<AssetImportItem&>(modal.Snapshot().items[0]);
    item.destinationFolder = "assets/Meshes";
    item.subfolderByType = 0;
    REQUIRE((modal.ImportSingleItem(0, cancellation).HasValue()));

    REQUIRE((std::filesystem::exists(projectRoot / "assets/Meshes/source.horoasset")));
    REQUIRE((!std::filesystem::exists(projectRoot / "assets/Meshes/Meshes")));

    std::error_code cleanupError;
    std::filesystem::remove_all(projectRoot, cleanupError);
}
