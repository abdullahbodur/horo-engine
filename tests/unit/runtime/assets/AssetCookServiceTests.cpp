#include <catch2/catch_test_macros.hpp>

#include "Horo/Assets/AssetCookService.h"
#include "Horo/Assets/AssetCook.h"
#include "Horo/Assets/CookCatalog.h"
#include "Horo/Foundation/CancellationToken.h"
#include "Horo/Foundation/JobSystem.h"
#include "HeadlessMeshCooker.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace
{
using namespace Horo;
using namespace Horo::Assets;

AssetId Id(const std::string_view value)
{
    auto parsed = AssetId::Parse(value);
    REQUIRE((parsed.HasValue()));
    return parsed.Value();
}

AssetTypeId Type(const std::string_view value)
{
    auto parsed = AssetTypeId::Parse(value);
    REQUIRE((parsed.HasValue()));
    return parsed.Value();
}

AssetCookTargetId Target(const std::string_view value)
{
    auto parsed = AssetCookTargetId::Parse(value);
    REQUIRE((parsed.HasValue()));
    return parsed.Value();
}

struct TempDir
{
    std::filesystem::path path;

    TempDir()
    {
        auto tmp = std::filesystem::temp_directory_path() / "horo_service_test";
        std::filesystem::create_directories(tmp);
        auto unique = tmp / ("test_" + std::to_string(
                                            std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(unique);
        path = unique;
    }

    ~TempDir()
    {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
};

/** @brief Creates a minimal file with given content. */
void WriteFile(const std::filesystem::path &path, std::span<const std::uint8_t> bytes)
{
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    f.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

/** @brief Creates a fake .horo sidecar so the registry picks up the file. */
std::string SidecarJson(std::string_view assetId, std::string_view assetType)
{
    return std::string("{\"schemaVersion\":1,\"assetId\":\"") + std::string(assetId) +
           "\",\"assetType\":\"" + std::string(assetType) + "\"}";
}

/**
 * @brief Sets up a minimal project directory structure with one source asset and sidecar.
 */
struct TestProject
{
    TempDir dir;
    std::filesystem::path assetsDir;
    std::filesystem::path sourceFile;

    TestProject()
    {
        assetsDir = dir.path / "assets";
        std::filesystem::create_directories(assetsDir);

        // Create a minimal source file
        sourceFile = assetsDir / "test_mesh.fbx";
        std::vector<std::uint8_t> data = {0x01, 0x02, 0x03, 0x04, 0x05};
        WriteFile(sourceFile, data);

        // Create sidecar
        auto sidecarJson = SidecarJson("00000000-0000-0000-0000-0000000000a1", "core.mesh");
        auto sidecarBytes = std::span<const std::uint8_t>(
            reinterpret_cast<const std::uint8_t *>(sidecarJson.data()), sidecarJson.size());
        WriteFile(std::string(sourceFile.string()) + ".horo", sidecarBytes);
    }
};

} // namespace

TEST_CASE("AssetCookService empty registry publishes empty generation", "[native]")
{
    TestProject project;
    TempDir cacheDir;
    TempDir cookedDir;

    JobSystem jobs;

    // Build an empty registry
    AssetRegistry registry;
    auto emptySnapshot = registry.Snapshot();

    // Build catalog with headless mesh cooker
    CookerCatalog catalog;
    REQUIRE((RegisterHeadlessMeshCooker(catalog).HasValue()));
    auto catSnapshot = catalog.Publish();
    REQUIRE((catSnapshot.HasValue()));

    AssetCookService service(jobs, catSnapshot.Value());

    AssetCookRequest request{
        .sourceRoot = project.dir.path,
        .cacheRoot = cacheDir.path,
        .cookedRoot = cookedDir.path,
        .registry = emptySnapshot,
        .target = Target("headless-null"),
    };

    CancellationToken cancellation;
    auto result = service.Cook(request, cancellation);
    REQUIRE((result.HasValue()));

    auto &report = result.Value();
    REQUIRE((report.totalAssets == 0));
    REQUIRE((report.cookedAssets == 0));
    REQUIRE((report.cacheHits == 0));
}

TEST_CASE("AssetCookService honours cancellation before work", "[native]")
{
    TestProject project;
    TempDir cacheDir;
    TempDir cookedDir;

    JobSystem jobs;

    AssetRegistry registry;
    auto snapshot = registry.Snapshot();

    CookerCatalog catalog;
    REQUIRE((RegisterHeadlessMeshCooker(catalog).HasValue()));
    auto catSnapshot = catalog.Publish();
    REQUIRE((catSnapshot.HasValue()));

    AssetCookService service(jobs, catSnapshot.Value());

    CancellationSource cancelSource;
    cancelSource.RequestCancellation();
    auto cancellation = cancelSource.Token();

    AssetCookRequest request{
        .sourceRoot = project.dir.path,
        .cacheRoot = cacheDir.path,
        .cookedRoot = cookedDir.path,
        .registry = snapshot,
        .target = Target("headless-null"),
    };

    auto result = service.Cook(request, cancellation);
    REQUIRE((result.HasError()));
}
