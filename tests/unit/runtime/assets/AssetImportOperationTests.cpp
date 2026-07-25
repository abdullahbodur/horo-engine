#include <catch2/catch_test_macros.hpp>

#include "Horo/Assets/AssetImportOperation.h"
#include "Horo/Assets/AssetImporter.h"
#include "Horo/Foundation/CancellationToken.h"
#include "Horo/Foundation/JobSystem.h"

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace
{
using namespace Horo;
using namespace Horo::Assets;

class TestImporter final : public IAssetImporter
{
  public:
    [[nodiscard]] Result<PreparedAssetImport> Import(
        const AssetImportInput &input,
        const CancellationToken & /*cancellation*/) const override
    {
        PreparedAssetImport result;
        result.type = AssetTypeId::Parse("core.mesh").Value();
        result.editorPayload.assign(input.sourceBytes.begin(), input.sourceBytes.end());
        return Result<PreparedAssetImport>::Success(std::move(result));
    }
};

} // namespace

TEST_CASE("AssetImportOperation Start enters Selecting phase", "[native]")
{
    JobSystem jobs;

    AssetImporterCatalog catalog;
    REQUIRE((catalog
                 .Register(AssetImporterContribution{
                     .contributionId = "test.obj",
                     .packageId = "test",
                     .moduleId = "test",
                     .version = "1.0",
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

TEST_CASE("AssetImportOperation diagnostics for unsupported extension", "[native]")
{
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

TEST_CASE("AssetImportOperation honours cancellation", "[native]")
{
    JobSystem jobs;

    AssetImporterCatalog catalog;
    REQUIRE((catalog
                 .Register(AssetImporterContribution{
                     .contributionId = "test.obj",
                     .packageId = "test",
                     .moduleId = "test",
                     .version = "1.0",
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
