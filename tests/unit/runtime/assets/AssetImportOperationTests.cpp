#include "Horo/Assets/AssetImportOperation.h"
#include "Horo/Assets/AssetImporter.h"
#include "Horo/Foundation/CancellationToken.h"
#include "Horo/Foundation/JobSystem.h"

#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace {
    using namespace Horo;
    using namespace Horo::Assets;

    class TestImporter final : public IAssetImporter {
    public:
        [[nodiscard]] Result<PreparedAssetImport> Import(const AssetImportInput &input,
                                                         const CancellationToken & /*cancellation*/) const override {
            PreparedAssetImport result;
            result.type = AssetTypeId::Parse("core.mesh").Value();
            result.editorPayload.assign(input.sourceBytes.begin(), input.sourceBytes.end());
            return Result<PreparedAssetImport>::Success(std::move(result));
        }
    };

    class SettingsCapturingImporter final : public IAssetImporter {
    public:
        [[nodiscard]] Result<PreparedAssetImport> Import(const AssetImportInput &input,
                                                         const CancellationToken & /*cancellation*/) const override {
            receivedSettings = input.settings;
            PreparedAssetImport result;
            result.type = AssetTypeId::Parse("core.mesh").Value();
            return Result<PreparedAssetImport>::Success(std::move(result));
        }

        mutable std::vector<ImportSettingValue> receivedSettings;
    };

}  // namespace

TEST_CASE("AssetImportOperation Start enters Selecting phase", "[native]") {
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
    auto catSnapshot = catalog.Publish();
    REQUIRE((catSnapshot.HasValue()));

    AssetImportOperation operation(jobs, catSnapshot.Value());

    AssetImportRequest request{
        .projectRoot = "/tmp/test_project",
        .sourceFiles = {"/tmp/test_project/assets/cube.obj"},
    };

    CancellationToken cancellation;
    auto result = operation.Start(request, cancellation);
    REQUIRE((result.HasValue()));

    auto snap = result.Value();
    REQUIRE((snap.phase == AssetImportPhase::Selecting));
    REQUIRE((snap.items.size() == 1));
    REQUIRE((snap.canCancel));
}

TEST_CASE("AssetImportOperation diagnostics for unsupported extension", "[native]") {
    JobSystem jobs;

    AssetImporterCatalog catalog;
    auto catSnapshot = catalog.Publish();
    REQUIRE((catSnapshot.HasValue()));

    AssetImportOperation operation(jobs, catSnapshot.Value());

    AssetImportRequest request{
        .projectRoot = "/tmp/test_project",
        .sourceFiles = {"/tmp/test_project/assets/unknown.xyz"},
    };

    CancellationToken cancellation;
    auto result = operation.Start(request, cancellation);
    REQUIRE((result.HasValue()));

    auto &item = result.Value().items[0];
    REQUIRE((!item.diagnostics.empty()));
    REQUIRE((item.diagnostics[0].code == "asset.import.no_importer"));
}

TEST_CASE("AssetImportOperation honours cancellation", "[native]") {
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
    auto catSnapshot = catalog.Publish();
    REQUIRE((catSnapshot.HasValue()));

    AssetImportOperation operation(jobs, catSnapshot.Value());

    CancellationSource cancelSource;
    cancelSource.RequestCancellation();

    AssetImportRequest request{
        .projectRoot = "/tmp/test_project",
        .sourceFiles = {"/tmp/test_project/assets/cube.obj"},
    };

    auto result = operation.Start(request, cancelSource.Token());
    REQUIRE((result.HasError()));
}

TEST_CASE("AssetImportOperation resolves queued importer settings", "[native]") {
    const auto sourceFile = std::filesystem::temp_directory_path() / "horo_asset_import_settings.obj";
    {
        std::ofstream source{sourceFile};
        source << "o settings";
    }

    JobSystem jobs;
    auto importer = std::make_shared<SettingsCapturingImporter>();
    AssetImporterCatalog catalog;
    REQUIRE((catalog
                 .Register(AssetImporterContribution{
                     .contributionId = "test.obj.settings",
                     .packageId = "test",
                     .moduleId = "test",
                     .moduleVersion = "1.0.0",
                     .version = "1.0.0",
                     .fileExtensions = {"obj"},
                     .assetTypes = {AssetTypeId::Parse("core.mesh").Value()},
                     .settings =
                         {
                             ImportSettingDescriptor{
                                 .id = "optimize",
                                 .labelKey = "Optimize",
                                 .descriptionKey = "",
                                 .kind = ImportSettingKind::Boolean,
                                 .defaultValue = false,
                             },
                             ImportSettingDescriptor{
                                 .id = "quality",
                                 .labelKey = "Quality",
                                 .descriptionKey = "",
                                 .kind = ImportSettingKind::Integer,
                                 .defaultValue = std::int64_t{1},
                             },
                         },
                     .strategy = importer,
                 })
                 .HasValue()));
    auto snapshot = catalog.Publish();
    REQUIRE((snapshot.HasValue()));

    AssetImportOperation operation(jobs, snapshot.Value());
    CancellationToken cancellation;
    auto start = operation.Start(
        AssetImportRequest{
            .projectRoot = sourceFile.parent_path(),
            .sourceFiles = {sourceFile},
        },
        cancellation);
    REQUIRE((start.HasValue()));
    REQUIRE((start.Value().items[0].displayName == sourceFile.stem().string()));
    REQUIRE((start.Value().items[0].settings.at("settings.optimize") == "false"));
    REQUIRE((start.Value().items[0].settings.at("settings.quality") == "1"));

    auto configured = operation.SetItemSettings(0, {{"settings.optimize", "true"}, {"settings.quality", "7"}});
    REQUIRE((configured.HasValue()));
    REQUIRE((operation.ImportSingleItem(0, cancellation).HasValue()));
    REQUIRE((importer->receivedSettings.size() == 2));
    REQUIRE((std::get<bool>(importer->receivedSettings[0])));
    REQUIRE((std::get<std::int64_t>(importer->receivedSettings[1]) == 7));

    std::error_code error;
    std::filesystem::remove(sourceFile, error);
}
