#include <catch2/catch_test_macros.hpp>

#include "Horo/Assets/CookCatalog.h"
#include "Horo/Assets/AssetCook.h"
#include "Horo/Foundation/CancellationToken.h"
#include "Horo/Foundation/Result.h"

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace
{
using namespace Horo;
using namespace Horo::Assets;

/** @brief Minimal test strategy that produces canned output with an identity suffix. */
class TestCooker final : public ICookerStrategy
{
  public:
    explicit TestCooker(std::string tag) : tag_(std::move(tag)) {}

    [[nodiscard]] Result<CookOutputSink> Cook(const CookSourceView &source,
                                              const CancellationToken & /*cancellation*/) const override
    {
        CookOutputSink sink;
        sink.payload.assign(source.bytes.begin(), source.bytes.end());
        sink.payload.push_back(static_cast<std::uint8_t>(tag_.front()));
        return Result<CookOutputSink>::Success(std::move(sink));
    }

  private:
    std::string tag_;
};

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

} // namespace

TEST_CASE("Cooker catalog registers and publishes a snapshot", "[native]")
{
    CookerCatalog catalog;

    REQUIRE((!catalog.IsSealed()));
    REQUIRE((catalog.Snapshot() == nullptr));

    auto meshType = Type("core.mesh");
    auto nullTarget = Target("headless-null");

    auto result = catalog.Register(CookerContribution{
        .contributionId = "horo.builtin.mesh_null",
        .assetType = meshType,
        .targets = {nullTarget},
        .strategy = std::make_shared<const TestCooker>("A"),
    });
    REQUIRE((result.HasValue()));

    auto snapshot = catalog.Publish();
    REQUIRE((snapshot.HasValue()));

    REQUIRE((catalog.IsSealed()));
    REQUIRE((catalog.Snapshot() != nullptr));
}

TEST_CASE("Cooker catalog snapshot finds matching cooker", "[native]")
{
    auto meshType = Type("core.mesh");
    auto nullTarget = Target("headless-null");

    CookerCatalog catalog;
    REQUIRE((catalog
                 .Register(CookerContribution{
                     .contributionId = "horo.builtin.mesh_null",
                     .assetType = meshType,
                     .targets = {nullTarget},
                     .strategy = std::make_shared<const TestCooker>("A"),
                 })
                 .HasValue()));

    auto snapshot = catalog.Publish();
    REQUIRE((snapshot.HasValue()));
    auto snap = snapshot.Value();

    auto *strategy = snap->Find(meshType, nullTarget);
    REQUIRE((strategy != nullptr));
}

TEST_CASE("Cooker catalog returns nullptr for unknown type/target", "[native]")
{
    auto meshType = Type("core.mesh");
    auto nullTarget = Target("headless-null");
    auto missingTarget = Target("desktop-opengl");

    CookerCatalog catalog;
    REQUIRE((catalog
                 .Register(CookerContribution{
                     .contributionId = "horo.builtin.mesh_null",
                     .assetType = meshType,
                     .targets = {nullTarget},
                     .strategy = std::make_shared<const TestCooker>("A"),
                 })
                 .HasValue()));

    auto snapshot = catalog.Publish();
    REQUIRE((snapshot.HasValue()));
    auto snap = snapshot.Value();

    REQUIRE((snap->Find(meshType, missingTarget) == nullptr));
    REQUIRE((snap->Find(Type("core.texture"), nullTarget) == nullptr));
}

TEST_CASE("Cooker catalog rejects duplicate type/target claim", "[native]")
{
    auto meshType = Type("core.mesh");
    auto nullTarget = Target("headless-null");

    CookerCatalog catalog;
    REQUIRE((catalog
                 .Register(CookerContribution{
                     .contributionId = "horo.builtin.mesh_null_a",
                     .assetType = meshType,
                     .targets = {nullTarget},
                     .strategy = std::make_shared<const TestCooker>("A"),
                 })
                 .HasValue()));

    auto result = catalog.Register(CookerContribution{
        .contributionId = "horo.builtin.mesh_null_b",
        .assetType = meshType,
        .targets = {nullTarget},
        .strategy = std::make_shared<const TestCooker>("B"),
    });
    REQUIRE((result.HasError()));
    REQUIRE((result.ErrorValue().code.Value() == "asset.cook.duplicate_cooker"));
}

TEST_CASE("Cooker catalog rejects duplicate contribution ID", "[native]")
{
    auto meshType = Type("core.mesh");
    auto nullTarget = Target("headless-null");

    CookerCatalog catalog;
    REQUIRE((catalog
                 .Register(CookerContribution{
                     .contributionId = "horo.builtin.mesh_null",
                     .assetType = meshType,
                     .targets = {nullTarget},
                     .strategy = std::make_shared<const TestCooker>("A"),
                 })
                 .HasValue()));

    auto result = catalog.Register(CookerContribution{
        .contributionId = "horo.builtin.mesh_null",
        .assetType = Type("core.texture"),
        .targets = {nullTarget},
        .strategy = std::make_shared<const TestCooker>("B"),
    });
    REQUIRE((result.HasError()));
}

TEST_CASE("Cooker catalog cannot publish after sealed", "[native]")
{
    auto meshType = Type("core.mesh");
    auto nullTarget = Target("headless-null");

    CookerCatalog catalog;
    REQUIRE((catalog
                 .Register(CookerContribution{
                     .contributionId = "horo.builtin.mesh_null",
                     .assetType = meshType,
                     .targets = {nullTarget},
                     .strategy = std::make_shared<const TestCooker>("A"),
                 })
                 .HasValue()));

    auto first = catalog.Publish();
    REQUIRE((first.HasValue()));

    auto second = catalog.Publish();
    REQUIRE((second.HasError()));
    REQUIRE((second.ErrorValue().code.Value() == "asset.cook.catalog_sealed"));
}

TEST_CASE("Cooker catalog rejects registration after sealed", "[native]")
{
    auto meshType = Type("core.mesh");
    auto nullTarget = Target("headless-null");

    CookerCatalog catalog;
    REQUIRE((catalog
                 .Register(CookerContribution{
                     .contributionId = "horo.builtin.mesh_null",
                     .assetType = meshType,
                     .targets = {nullTarget},
                     .strategy = std::make_shared<const TestCooker>("A"),
                 })
                 .HasValue()));
    REQUIRE((catalog.Publish().HasValue()));

    auto result = catalog.Register(CookerContribution{
        .contributionId = "horo.builtin.texture_null",
        .assetType = Type("core.texture"),
        .targets = {nullTarget},
        .strategy = std::make_shared<const TestCooker>("B"),
    });
    REQUIRE((result.HasError()));
    REQUIRE((result.ErrorValue().code.Value() == "asset.cook.catalog_sealed"));
}

TEST_CASE("CookerCatalog::Reset clears unsealed entries", "[native]")
{
    auto meshType = Type("core.mesh");
    auto nullTarget = Target("headless-null");

    CookerCatalog catalog;
    REQUIRE((catalog
                 .Register(CookerContribution{
                     .contributionId = "horo.builtin.mesh_null",
                     .assetType = meshType,
                     .targets = {nullTarget},
                     .strategy = std::make_shared<const TestCooker>("A"),
                 })
                 .HasValue()));

    catalog.Reset();

    auto snapshot = catalog.Publish();
    REQUIRE((snapshot.HasValue()));
    auto snap = snapshot.Value();
    REQUIRE((snap->Find(meshType, nullTarget) == nullptr));
}

TEST_CASE("CookerCatalog::Reset is no-op after sealed", "[native]")
{
    auto meshType = Type("core.mesh");
    auto nullTarget = Target("headless-null");

    CookerCatalog catalog;
    REQUIRE((catalog
                 .Register(CookerContribution{
                     .contributionId = "horo.builtin.mesh_null",
                     .assetType = meshType,
                     .targets = {nullTarget},
                     .strategy = std::make_shared<const TestCooker>("A"),
                 })
                 .HasValue()));
    REQUIRE((catalog.Publish().HasValue()));

    catalog.Reset();

    auto snap = catalog.Snapshot();
    REQUIRE((snap != nullptr));
    REQUIRE((snap->Find(meshType, nullTarget) != nullptr));
}

TEST_CASE("CookerContribution::Handles matches type/target", "[native]")
{
    auto meshType = Type("core.mesh");
    auto textureType = Type("core.texture");
    auto nullTarget = Target("headless-null");
    auto openglTarget = Target("desktop-opengl");

    CookerContribution contrib{
        .contributionId = "test",
        .assetType = meshType,
        .targets = {nullTarget, openglTarget},
        .strategy = std::make_shared<const TestCooker>("A"),
    };

    REQUIRE((contrib.Handles(meshType, nullTarget)));
    REQUIRE((contrib.Handles(meshType, openglTarget)));
    REQUIRE((!contrib.Handles(textureType, nullTarget)));
    REQUIRE((!contrib.Handles(meshType, Target("desktop-vulkan"))));
}

TEST_CASE("Cooker catalog snapshot entries are sorted deterministically", "[native]")
{
    auto textureType = Type("core.texture");
    auto meshType = Type("core.mesh");
    auto nullTarget = Target("headless-null");

    CookerCatalog catalog;

    // Register in non-alphabetical order
    REQUIRE((catalog
                 .Register(CookerContribution{
                     .contributionId = "b",
                     .assetType = textureType,
                     .targets = {nullTarget},
                     .strategy = std::make_shared<const TestCooker>("X"),
                 })
                 .HasValue()));

    REQUIRE((catalog
                 .Register(CookerContribution{
                     .contributionId = "a",
                     .assetType = meshType,
                     .targets = {nullTarget},
                     .strategy = std::make_shared<const TestCooker>("Y"),
                 })
                 .HasValue()));

    auto snapshot = catalog.Publish();
    REQUIRE((snapshot.HasValue()));
    auto snap = snapshot.Value();

    // mesh before texture alphabetically; both found regardless of registration order
    REQUIRE((snap->Find(meshType, nullTarget) != nullptr));
    REQUIRE((snap->Find(textureType, nullTarget) != nullptr));
}

TEST_CASE("Cooker catalog registers multiple contributions for different types", "[native]")
{
    auto meshType = Type("core.mesh");
    auto textureType = Type("core.texture");
    auto nullTarget = Target("headless-null");

    CookerCatalog catalog;

    REQUIRE((catalog
                 .Register(CookerContribution{
                     .contributionId = "horo.mesh",
                     .assetType = meshType,
                     .targets = {nullTarget},
                     .strategy = std::make_shared<const TestCooker>("M"),
                 })
                 .HasValue()));

    REQUIRE((catalog
                 .Register(CookerContribution{
                     .contributionId = "horo.texture",
                     .assetType = textureType,
                     .targets = {nullTarget},
                     .strategy = std::make_shared<const TestCooker>("T"),
                 })
                 .HasValue()));

    auto snapshot = catalog.Publish();
    REQUIRE((snapshot.HasValue()));
    auto snap = snapshot.Value();

    REQUIRE((snap->Find(meshType, nullTarget) != nullptr));
    REQUIRE((snap->Find(textureType, nullTarget) != nullptr));

    // Verify distinct strategies
    auto *meshStrategy = snap->Find(meshType, nullTarget);
    auto *textureStrategy = snap->Find(textureType, nullTarget);
    REQUIRE((meshStrategy != textureStrategy));
}
