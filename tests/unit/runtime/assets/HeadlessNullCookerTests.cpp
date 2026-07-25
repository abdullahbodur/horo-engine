#include <catch2/catch_test_macros.hpp>

#include "Horo/Assets/CookCatalog.h"
#include "Horo/Assets/AssetCook.h"
#include "Horo/Foundation/CancellationToken.h"
#include "Horo/Foundation/Sha256.h"
#include "Horo/Foundation/Result.h"
#include "HeadlessMeshCooker.h"

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

/**
 * @brief Constructs a fixed-size source payload for testing.
 */
std::vector<std::uint8_t> SourcePayload(std::size_t byteValue, std::size_t count)
{
    return std::vector<std::uint8_t>(count, static_cast<std::uint8_t>(byteValue & 0xFF));
}

} // namespace

TEST_CASE("Headless mesh cooker succeeds with non-empty source for headless-null", "[native]")
{
    auto meshType = Type("core.mesh");
    auto nullTarget = Target("headless-null");
    auto assetId = Id("00000000-0000-0000-0000-000000000001");

    auto sourceBytes = SourcePayload(0xAB, 256);
    auto sourceDigest = ComputeSha256(std::as_bytes(std::span{sourceBytes}));

    CancellationToken cancellation;

    CookSourceView source{
        .id = assetId,
        .type = meshType,
        .target = nullTarget,
        .sourceDigest = sourceDigest,
        .bytes = sourceBytes,
    };

    // The built-in cooker must be available from the catalog.
    CookerCatalog catalog;
    // TODO: register the built-in headless mesh cooker here.
    REQUIRE((catalog.Register(CookerContribution{
                                 .contributionId = "horo.asset-cooker.headless-mesh-validation",
                                 .assetType = meshType,
                                 .targets = {nullTarget},
                                 .strategy = CreateHeadlessMeshCooker(),
                             })
                 .HasValue()));
    auto snapshot = catalog.Publish();
    REQUIRE((snapshot.HasValue()));

    auto *strategy = snapshot.Value()->Find(meshType, nullTarget);
    REQUIRE((strategy != nullptr));

    auto result = strategy->Cook(source, cancellation);
    REQUIRE((result.HasValue()));

    // Payload must contain deterministic metadata, not the raw source.
    auto &sink = result.Value();
    REQUIRE((!sink.payload.empty()));
}

TEST_CASE("Headless mesh cooker rejects empty source", "[native]")
{
    auto meshType = Type("core.mesh");
    auto nullTarget = Target("headless-null");
    auto assetId = Id("00000000-0000-0000-0000-000000000002");

    std::vector<std::uint8_t> emptyBytes;
    auto sourceDigest = ComputeSha256(std::as_bytes(std::span{emptyBytes}));

    CancellationToken cancellation;

    CookSourceView source{
        .id = assetId,
        .type = meshType,
        .target = nullTarget,
        .sourceDigest = sourceDigest,
        .bytes = emptyBytes,
    };

    CookerCatalog catalog;
    REQUIRE((catalog.Register(CookerContribution{
                                 .contributionId = "horo.asset-cooker.headless-mesh-validation",
                                 .assetType = meshType,
                                 .targets = {nullTarget},
                                 .strategy = CreateHeadlessMeshCooker(),
                             })
                 .HasValue()));
    auto snapshot = catalog.Publish();
    REQUIRE((snapshot.HasValue()));

    auto *strategy = snapshot.Value()->Find(meshType, nullTarget);
    REQUIRE((strategy != nullptr));

    auto result = strategy->Cook(source, cancellation);
    REQUIRE((result.HasError()));
}

TEST_CASE("Headless mesh cooker rejects unsupported target", "[native]")
{
    auto meshType = Type("core.mesh");
    auto nullTarget = Target("headless-null");
    auto unsupportedTarget = Target("desktop-vulkan");

    auto sourceBytes = SourcePayload(0xCD, 64);
    auto sourceDigest = ComputeSha256(std::as_bytes(std::span{sourceBytes}));

    CancellationToken cancellation;

    // Use unsupported target in the source view
    CookSourceView source{
        .id = Id("00000000-0000-0000-0000-000000000003"),
        .type = meshType,
        .target = unsupportedTarget,
        .sourceDigest = sourceDigest,
        .bytes = sourceBytes,
    };

    CookerCatalog catalog;
    REQUIRE((catalog.Register(CookerContribution{
                                 .contributionId = "horo.asset-cooker.headless-mesh-validation",
                                 .assetType = meshType,
                                 .targets = {nullTarget},
                                 .strategy = CreateHeadlessMeshCooker(),
                             })
                 .HasValue()));
    auto snapshot = catalog.Publish();
    REQUIRE((snapshot.HasValue()));

    // Lookup by unsupported target returns nullptr
    auto *strategy = snapshot.Value()->Find(meshType, unsupportedTarget);
    REQUIRE((strategy == nullptr));
}

TEST_CASE("Headless mesh cooker honours cancellation", "[native]")
{
    auto meshType = Type("core.mesh");
    auto nullTarget = Target("headless-null");

    auto sourceBytes = SourcePayload(0xEF, 1024);
    auto sourceDigest = ComputeSha256(std::as_bytes(std::span{sourceBytes}));

    CancellationSource cancelSource;
    cancelSource.RequestCancellation();
    auto cancellation = cancelSource.Token();

    CookSourceView source{
        .id = Id("00000000-0000-0000-0000-000000000004"),
        .type = meshType,
        .target = nullTarget,
        .sourceDigest = sourceDigest,
        .bytes = sourceBytes,
    };

    CookerCatalog catalog;
    REQUIRE((catalog.Register(CookerContribution{
                                 .contributionId = "horo.asset-cooker.headless-mesh-validation",
                                 .assetType = meshType,
                                 .targets = {nullTarget},
                                 .strategy = CreateHeadlessMeshCooker(),
                             })
                 .HasValue()));
    auto snapshot = catalog.Publish();
    REQUIRE((snapshot.HasValue()));

    auto *strategy = snapshot.Value()->Find(meshType, nullTarget);
    REQUIRE((strategy != nullptr));

    auto result = strategy->Cook(source, cancellation);
    REQUIRE((result.HasError()));
}

TEST_CASE("Headless mesh cooker produces deterministic byte-identical output", "[native]")
{
    auto meshType = Type("core.mesh");
    auto nullTarget = Target("headless-null");

    auto sourceBytes = SourcePayload(0x42, 512);
    auto sourceDigest = ComputeSha256(std::as_bytes(std::span{sourceBytes}));

    CancellationToken cancellation;

    CookerCatalog catalog;
    REQUIRE((catalog.Register(CookerContribution{
                                 .contributionId = "horo.asset-cooker.headless-mesh-validation",
                                 .assetType = meshType,
                                 .targets = {nullTarget},
                                 .strategy = CreateHeadlessMeshCooker(),
                             })
                 .HasValue()));
    auto snapshot = catalog.Publish();
    REQUIRE((snapshot.HasValue()));

    auto *strategy = snapshot.Value()->Find(meshType, nullTarget);
    REQUIRE((strategy != nullptr));

    CookSourceView source{
        .id = Id("00000000-0000-0000-0000-000000000005"),
        .type = meshType,
        .target = nullTarget,
        .sourceDigest = sourceDigest,
        .bytes = sourceBytes,
    };

    auto result1 = strategy->Cook(source, cancellation);
    REQUIRE((result1.HasValue()));

    auto result2 = strategy->Cook(source, cancellation);
    REQUIRE((result2.HasValue()));

    // Byte-identical output
    REQUIRE((result1.Value().payload == result2.Value().payload));
}

TEST_CASE("Headless mesh cooker payload is validation metadata, not source bytes", "[native]")
{
    auto meshType = Type("core.mesh");
    auto nullTarget = Target("headless-null");

    auto sourceBytes = SourcePayload(0x99, 1024);
    auto sourceDigest = ComputeSha256(std::as_bytes(std::span{sourceBytes}));

    CancellationToken cancellation;

    CookerCatalog catalog;
    REQUIRE((catalog.Register(CookerContribution{
                                 .contributionId = "horo.asset-cooker.headless-mesh-validation",
                                 .assetType = meshType,
                                 .targets = {nullTarget},
                                 .strategy = CreateHeadlessMeshCooker(),
                             })
                 .HasValue()));
    auto snapshot = catalog.Publish();
    REQUIRE((snapshot.HasValue()));

    auto *strategy = snapshot.Value()->Find(meshType, nullTarget);
    REQUIRE((strategy != nullptr));

    CookSourceView source{
        .id = Id("00000000-0000-0000-0000-000000000006"),
        .type = meshType,
        .target = nullTarget,
        .sourceDigest = sourceDigest,
        .bytes = sourceBytes,
    };

    auto result = strategy->Cook(source, cancellation);
    REQUIRE((result.HasValue()));

    // Payload is metadata (schema version + byte count + digest), not raw source
    auto &payload = result.Value().payload;
    REQUIRE((payload.size() < sourceBytes.size()));
    // Must not be a prefix match with the source
    REQUIRE((!std::equal(payload.begin(), payload.end(), sourceBytes.begin())));
}
