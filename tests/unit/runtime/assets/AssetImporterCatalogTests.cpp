#include <catch2/catch_test_macros.hpp>

#include "Horo/Assets/AssetImporter.h"
#include "Horo/Assets/AssetRegistry.h"
#include "Horo/Foundation/CancellationToken.h"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace
{
using namespace Horo;
using namespace Horo::Assets;

AssetTypeId Type(const std::string_view value)
{
    auto parsed = AssetTypeId::Parse(value);
    REQUIRE((parsed.HasValue()));
    return parsed.Value();
}

class TestImporter final : public IAssetImporter
{
  public:
    explicit TestImporter(std::string tag) : tag_(std::move(tag)) {}

    [[nodiscard]] Result<PreparedAssetImport> Import(
        const AssetImportInput &input,
        const CancellationToken & /*cancellation*/) const override
    {
        PreparedAssetImport result;
        result.type = Type("core.mesh");
        result.editorPayload.assign(input.sourceBytes.begin(), input.sourceBytes.end());
        result.editorPayload.push_back(static_cast<std::uint8_t>(tag_.front()));
        return Result<PreparedAssetImport>::Success(std::move(result));
    }

  private:
    std::string tag_;
};

AssetImporterContribution MakeContribution(std::string id, std::string ext, std::string tag)
{
    return AssetImporterContribution{
        .contributionId = std::move(id),
        .packageId = "test.pkg",
        .moduleId = "test.mod",
        .version = "1.0.0",
        .fileExtensions = {std::move(ext)},
        .assetTypes = {Type("core.mesh")},
        .settings = {},
        .builtIn = false,
        .strategy = std::make_shared<const TestImporter>(std::move(tag)),
    };
}

} // namespace

TEST_CASE("Importer catalog registers and publishes a snapshot", "[native]")
{
    AssetImporterCatalog catalog;

    REQUIRE((!catalog.IsSealed()));
    REQUIRE((catalog.Snapshot() == nullptr));

    auto result = catalog.Register(MakeContribution("test.obj", "obj", "A"));
    REQUIRE((result.HasValue()));

    auto snapshot = catalog.Publish();
    REQUIRE((snapshot.HasValue()));
    REQUIRE((catalog.IsSealed()));
}

TEST_CASE("Importer catalog finds importer by extension", "[native]")
{
    AssetImporterCatalog catalog;
    REQUIRE((catalog.Register(MakeContribution("test.obj", "obj", "A")).HasValue()));
    auto snapshot = catalog.Publish();
    REQUIRE((snapshot.HasValue()));

    auto *strategy = snapshot.Value()->FindByExtension("obj");
    REQUIRE((strategy != nullptr));
}

TEST_CASE("Importer catalog returns nullptr for unknown extension", "[native]")
{
    AssetImporterCatalog catalog;
    REQUIRE((catalog.Register(MakeContribution("test.obj", "obj", "A")).HasValue()));
    auto snapshot = catalog.Publish();
    REQUIRE((snapshot.HasValue()));

    REQUIRE((snapshot.Value()->FindByExtension("fbx") == nullptr));
}

TEST_CASE("Importer catalog rejects duplicate contribution ID", "[native]")
{
    AssetImporterCatalog catalog;
    REQUIRE((catalog.Register(MakeContribution("test.obj", "obj", "A")).HasValue()));

    auto result = catalog.Register(MakeContribution("test.obj", "fbx", "B"));
    REQUIRE((result.HasError()));
}

TEST_CASE("Importer catalog can register multiple extensions", "[native]")
{
    AssetImporterCatalog catalog;
    REQUIRE((catalog.Register(MakeContribution("test.mesh", "obj", "A")).HasValue()));
    REQUIRE((catalog.Register(MakeContribution("test.model", "fbx", "B")).HasValue()));

    auto snapshot = catalog.Publish();
    REQUIRE((snapshot.HasValue()));

    REQUIRE((snapshot.Value()->FindByExtension("obj") != nullptr));
    REQUIRE((snapshot.Value()->FindByExtension("fbx") != nullptr));
}

TEST_CASE("Importer catalog rejects registration after sealed", "[native]")
{
    AssetImporterCatalog catalog;
    REQUIRE((catalog.Register(MakeContribution("test.obj", "obj", "A")).HasValue()));
    REQUIRE((catalog.Publish().HasValue()));

    auto result = catalog.Register(MakeContribution("test.fbx", "fbx", "B"));
    REQUIRE((result.HasError()));
}

TEST_CASE("Importer catalog finds by contribution ID", "[native]")
{
    AssetImporterCatalog catalog;
    REQUIRE((catalog.Register(MakeContribution("test.obj", "obj", "A")).HasValue()));
    auto snapshot = catalog.Publish();
    REQUIRE((snapshot.HasValue()));

    auto *contrib = snapshot.Value()->FindById("test.obj");
    REQUIRE((contrib != nullptr));
    REQUIRE((contrib->contributionId == "test.obj"));
}

TEST_CASE("Importer catalog Reset clears unsealed entries", "[native]")
{
    AssetImporterCatalog catalog;
    REQUIRE((catalog.Register(MakeContribution("test.obj", "obj", "A")).HasValue()));
    catalog.Reset();

    auto snapshot = catalog.Publish();
    REQUIRE((snapshot.HasValue()));
    REQUIRE((snapshot.Value()->FindByExtension("obj") == nullptr));
}

TEST_CASE("AssetImporterContribution::HandlesExtension", "[native]")
{
    auto contrib = MakeContribution("test", "obj", "A");

    REQUIRE((contrib.HandlesExtension("obj")));
    REQUIRE((!contrib.HandlesExtension("fbx")));
    REQUIRE((!contrib.HandlesExtension("OBJ"))); // case-sensitive
}
