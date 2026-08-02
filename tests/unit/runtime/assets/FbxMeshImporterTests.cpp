#include <catch2/catch_test_macros.hpp>

#include "FbxMeshImporter.h"

#include "Horo/Assets/AssetImporter.h"
#include "Horo/Foundation/CancellationToken.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace {
using namespace Horo;
using namespace Horo::Assets;

[[nodiscard]] std::vector<std::uint8_t> ReadFixture(const std::string &fileName) {
    const std::filesystem::path path = std::filesystem::path{HORO_UFBX_TEST_DATA_DIR} / fileName;
    std::ifstream input(path, std::ios::binary);
    REQUIRE(input.good());
    return {
        std::istreambuf_iterator<char>{input},
        std::istreambuf_iterator<char>{},
    };
}

[[nodiscard]] std::uint32_t ReadU32(const std::span<const std::uint8_t> bytes,
                                    const std::size_t offset) {
    return static_cast<std::uint32_t>(bytes[offset]) |
           (static_cast<std::uint32_t>(bytes[offset + 1]) << 8U) |
           (static_cast<std::uint32_t>(bytes[offset + 2]) << 16U) |
           (static_cast<std::uint32_t>(bytes[offset + 3]) << 24U);
}

[[nodiscard]] const AssetImporterContribution &FbxContribution(
    AssetImporterCatalog &catalog, std::shared_ptr<const AssetImporterCatalogSnapshot> &published) {
    REQUIRE((RegisterFbxMeshImporter(catalog).HasValue()));
    auto result = catalog.Publish();
    REQUIRE(result.HasValue());
    published = result.Value();
    const auto *contribution = published->FindById("horo.asset-importer.fbx-mesh");
    REQUIRE(contribution != nullptr);
    REQUIRE(contribution->strategy != nullptr);
    REQUIRE(contribution->previewProvider != nullptr);
    return *contribution;
}
} // namespace

TEST_CASE("FBX importer combines transformed scene meshes into a shaded preview", "[native]") {
    AssetImporterCatalog catalog;
    std::shared_ptr<const AssetImporterCatalogSnapshot> published;
    const AssetImporterContribution &contribution = FbxContribution(catalog, published);
    const std::vector<std::uint8_t> source =
        ReadFixture("blender_279_nested_meshes_7400_binary.fbx");

    auto imported = contribution.strategy->Import(
        AssetImportInput{
            .sourceBytes = source,
            .sourceExtension = "fbx",
            .settings = {},
        },
        CancellationToken{});
    REQUIRE(imported.HasValue());
    REQUIRE((ReadU32(imported.Value().editorPayload, 0) == 2));
    REQUIRE((ReadU32(imported.Value().editorPayload, 4) > 8));
    REQUIRE((ReadU32(imported.Value().editorPayload, 8) > 8));

    auto preview = contribution.previewProvider->GeneratePreview(
        AssetPreviewInput{
            .editorPayload = imported.Value().editorPayload,
            .absoluteAssetPath = "/tmp/nested_meshes.horoasset",
            .assetType = AssetTypeId::Parse("core.mesh").Value(),
            .width = 128,
            .height = 128,
        },
        CancellationToken{});
    REQUIRE(preview.HasValue());
    REQUIRE(preview.Value().IsValid());
    std::size_t opaquePixels = 0;
    for (std::size_t alpha = 3; alpha < preview.Value().pixels.size(); alpha += 4U) {
        if (preview.Value().pixels[alpha] != 0)
            ++opaquePixels;
    }
    REQUIRE((opaquePixels > 100));
}

TEST_CASE("FBX importer supports production ASCII scenes", "[native]") {
    AssetImporterCatalog catalog;
    std::shared_ptr<const AssetImporterCatalogSnapshot> published;
    const AssetImporterContribution &contribution = FbxContribution(catalog, published);
    const std::vector<std::uint8_t> source = ReadFixture("maya_cube_hidden_6100_ascii.fbx");

    auto imported = contribution.strategy->Import(
        AssetImportInput{
            .sourceBytes = source,
            .sourceExtension = "fbx",
            .settings = {},
        },
        CancellationToken{});
    REQUIRE(imported.HasValue());
    REQUIRE((ReadU32(imported.Value().editorPayload, 4) > 0));
    REQUIRE((ReadU32(imported.Value().editorPayload, 8) > 0));
}
