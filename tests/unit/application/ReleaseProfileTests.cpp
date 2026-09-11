#include "Horo/Release/ReleaseProfile.h"

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>
#include <string>
#include <utility>

namespace {
    using namespace Horo::Release;
    using Json = nlohmann::json;

    const std::array AvailableCapabilities{ReleaseCapabilityId{"release.packaging"}};

    constexpr std::string_view ValidCatalog = R"json({
  "schemaVersion": 1,
  "presets": [
    {
      "id": "shipping-base",
      "product": {"kind": "engine-editor"},
      "artifactClass": "installable",
      "platform": "windows",
      "packageFormat": "zip",
      "content": {
        "executables": true,
        "runtimeLibraries": true,
        "assets": "single-package",
        "developerDiagnostics": false,
        "crashReports": true
      },
      "symbols": "separate",
      "signing": "when-supported",
      "notarizationRequired": false,
      "includeLicensesAndNotices": true,
      "includeReleaseNotes": true,
      "updateEligible": true,
      "patchEligible": true,
      "eligibleDestinations": ["internal", "public-download"],
      "requiredCapabilities": ["release.packaging"]
    },
    {
      "id": "linux-preview",
      "parent": "shipping-base",
      "platform": "linux",
      "packageFormat": "tar-gzip",
      "signing": "disabled",
      "includeReleaseNotes": false,
      "eligibleDestinations": ["internal"]
    }
  ]
})json";

    void RequireError(const auto &result, const std::string_view code) {
        REQUIRE(result.HasError());
        CHECK(result.ErrorValue().domain.Value() == "horo.release");
        CHECK(result.ErrorValue().code.Value() == code);
    }

    ReleaseProfileCatalog Catalog() {
        auto parsed = ReleaseProfileCatalog::Parse(ValidCatalog);
        REQUIRE(parsed.HasValue());
        return std::move(parsed).Value();
    }

    std::vector<ReleaseProfilePreset> Presets() {
        const auto catalog = Catalog();
        return {catalog.Presets().begin(), catalog.Presets().end()};
    }
}  // namespace

TEST_CASE("Named release presets resolve inherited policy into one deterministic immutable profile",
          "[unit][application][release][profile][headless]") {
    const auto catalog = Catalog();
    const ReleaseCapabilityId capability{"release.packaging"};
    const auto resolved = catalog.Resolve({"linux-preview"}, std::span{&capability, 1U});
    REQUIRE(resolved.HasValue());

    const EffectiveReleaseProfile &profile = resolved.Value();
    CHECK(profile.Id().value == "linux-preview");
    CHECK(profile.Product().kind == DistributionProductKind::Editor);
    CHECK(profile.ArtifactClass() == DistributionArtifactClass::InstallableProduct);
    CHECK(profile.Platform() == DistributionPlatform::Linux);
    CHECK(profile.PackageFormat() == DistributionPackageFormat::TarGzip);
    CHECK(profile.Content() == ReleaseContentPolicy{true, true, ReleaseAssetPolicy::SinglePackage, false, true});
    CHECK(profile.Symbols() == ReleaseSymbolPolicy::SeparateArtifact);
    CHECK(profile.Signing() == ReleaseSigningPolicy::Disabled);
    CHECK_FALSE(profile.NotarizationRequired());
    CHECK(profile.IncludesLicensesAndNotices());
    CHECK_FALSE(profile.IncludesReleaseNotes());
    CHECK(profile.UpdateEligible());
    CHECK(profile.PatchEligible());
    REQUIRE(profile.EligibleDestinations().size() == 1U);
    CHECK(profile.EligibleDestinations().front().value == "internal");

    const std::string first = profile.SerializeCanonical();
    const std::string second = catalog.Resolve({"linux-preview"}, std::span{&capability, 1U}).Value().SerializeCanonical();
    CHECK(first == second);
}

TEST_CASE("Release profile catalogs save canonically and round trip named presets", "[unit][application][release][profile][headless]") {
    const auto catalog = Catalog();
    const std::string encoded = catalog.SerializeCanonical();
    const auto reparsed = ReleaseProfileCatalog::Parse(encoded);
    REQUIRE(reparsed.HasValue());
    CHECK(reparsed.Value().SerializeCanonical() == encoded);
    REQUIRE(reparsed.Value().Presets().size() == 2U);
    CHECK(reparsed.Value().Presets()[0].id.value == "linux-preview");
    CHECK(reparsed.Value().Presets()[1].id.value == "shipping-base");

    const Json effective = Json::parse(catalog.Resolve({"shipping-base"}, AvailableCapabilities).Value().SerializeCanonical());
    CHECK(effective.at("eligibleDestinations") == Json::array({"internal", "public-download"}));
    CHECK(effective.at("requiredCapabilities") == Json::array({"release.packaging"}));
}

TEST_CASE("Every distribution product resolves and round trips through release profiles",
          "[unit][application][release][profile][headless]") {
    struct ProductCase final {
        std::string_view name;
        DistributionProductKind kind;
        std::string_view componentId;
    };

    constexpr std::array products{ProductCase{"engine-editor", DistributionProductKind::Editor, {}},
                                  ProductCase{"engine-cli", DistributionProductKind::EngineCli, {}},
                                  ProductCase{"package-tool-cli", DistributionProductKind::PackageToolCli, {}},
                                  ProductCase{"sdk", DistributionProductKind::PublicSdk, {}},
                                  ProductCase{"renderer-component", DistributionProductKind::RendererComponent, "renderer.vulkan"},
                                  ProductCase{"game-runtime", DistributionProductKind::GameRuntime, {}},
                                  ProductCase{"game-dedicated-server", DistributionProductKind::GameDedicatedServer, {}}};

    for (const ProductCase &product : products) {
        CAPTURE(product.name);
        Json encodedProduct{{"kind", product.name}};
        if (!product.componentId.empty())
            encodedProduct["componentId"] = product.componentId;
        Json catalogJson = Json::parse(ValidCatalog);
        catalogJson["presets"][0]["product"] = std::move(encodedProduct);

        const auto catalog = ReleaseProfileCatalog::Parse(catalogJson.dump());
        REQUIRE(catalog.HasValue());
        const auto resolved = catalog.Value().Resolve({"linux-preview"}, AvailableCapabilities);
        REQUIRE(resolved.HasValue());
        CHECK(resolved.Value().Product() == DistributionProductIdentity{product.kind, std::string{product.componentId}});

        const std::string canonical = catalog.Value().SerializeCanonical();
        const auto reparsed = ReleaseProfileCatalog::Parse(canonical);
        REQUIRE(reparsed.HasValue());
        CHECK(reparsed.Value().SerializeCanonical() == canonical);
    }
}

TEST_CASE("Renderer component identity is required only for renderer products", "[unit][application][release][profile][headless]") {
    Json changed = Json::parse(ValidCatalog);
    changed["presets"][0]["product"] = {{"kind", "renderer-component"}};
    RequireError(ReleaseProfileCatalog::Parse(changed.dump()), "release.profile.invalid");

    changed = Json::parse(ValidCatalog);
    changed["presets"][0]["product"]["componentId"] = "renderer.vulkan";
    RequireError(ReleaseProfileCatalog::Parse(changed.dump()), "release.profile.invalid");

    changed = Json::parse(ValidCatalog);
    changed["presets"][0]["product"] = {{"kind", "renderer-component"}, {"componentId", "Renderer Vulkan"}};
    RequireError(ReleaseProfileCatalog::Parse(changed.dump()), "release.profile.invalid");
}

TEST_CASE("Installable symbol and diagnostics profiles resolve their distinct content contracts",
          "[unit][application][release][profile][headless]") {
    struct ArtifactCase final {
        std::string_view artifactClass;
        std::string_view symbols;
        ReleaseContentPolicy content;
    };

    constexpr std::array artifacts{ArtifactCase{"installable", "omit", {true, true, ReleaseAssetPolicy::SinglePackage, false, true}},
                                   ArtifactCase{"symbols", "separate", {false, false, ReleaseAssetPolicy::Omit, false, true}},
                                   ArtifactCase{"developer-diagnostics", "omit", {false, false, ReleaseAssetPolicy::Omit, true, true}}};

    for (const ArtifactCase &artifact : artifacts) {
        CAPTURE(artifact.artifactClass);
        Json catalogJson = Json::parse(ValidCatalog);
        Json &preset = catalogJson["presets"][0];
        preset["artifactClass"] = artifact.artifactClass;
        preset["packageFormat"] = "zip";
        preset["signing"] = "when-supported";
        preset["symbols"] = artifact.symbols;
        preset["content"] = {{"executables", artifact.content.executables},
                             {"runtimeLibraries", artifact.content.runtimeLibraries},
                             {"assets", artifact.content.assets == ReleaseAssetPolicy::SinglePackage ? "single-package" : "omit"},
                             {"developerDiagnostics", artifact.content.developerDiagnostics},
                             {"crashReports", artifact.content.crashReports}};

        const auto catalog = ReleaseProfileCatalog::Parse(catalogJson.dump());
        REQUIRE(catalog.HasValue());
        const auto resolved = catalog.Value().Resolve({"shipping-base"}, AvailableCapabilities);
        REQUIRE(resolved.HasValue());
        CHECK(resolved.Value().Content() == artifact.content);
    }
}

TEST_CASE("Build toolchains and credential bindings cannot enter release profile persistence",
          "[unit][application][release][profile][security][headless]") {
    Json catalog = Json::parse(ValidCatalog);
    catalog["presets"][0]["toolchainPreset"] = "cmake-linux-release";
    RequireError(ReleaseProfileCatalog::Parse(catalog.dump()), "release.profile.invalid");

    catalog = Json::parse(ValidCatalog);
    catalog["presets"][0]["signingCredentialRef"] = "keychain://release";
    RequireError(ReleaseProfileCatalog::Parse(catalog.dump()), "release.profile.invalid");

    catalog = Json::parse(ValidCatalog);
    catalog["presets"][0]["destinationCredential"] = "token";
    RequireError(ReleaseProfileCatalog::Parse(catalog.dump()), "release.profile.invalid");

    const std::string encoded = Catalog().SerializeCanonical();
    CHECK(encoded.find("toolchain") == std::string::npos);
    CHECK(encoded.find("credential") == std::string::npos);
}

TEST_CASE("Unknown required capabilities fail with their stable public identity", "[unit][application][release][profile][headless]") {
    const auto catalog = Catalog();
    const auto missing = catalog.Resolve({"shipping-base"}, {});
    RequireError(missing, "release.profile.capability_unsupported");
    CHECK(missing.ErrorValue().message.find("release.packaging") != std::string::npos);

    const std::array duplicate{ReleaseCapabilityId{"release.packaging"}, ReleaseCapabilityId{"release.packaging"}};
    RequireError(catalog.Resolve({"shipping-base"}, duplicate), "release.profile.invalid");
    const std::array malformed{ReleaseCapabilityId{"Release Packaging"}};
    RequireError(catalog.Resolve({"shipping-base"}, malformed), "release.profile.invalid");
}

TEST_CASE("Profile inheritance rejects missing parents cycles and excessive depth deterministically",
          "[unit][application][release][profile][headless]") {
    Json missing = Json::parse(ValidCatalog);
    missing["presets"][1]["parent"] = "absent";
    RequireError(ReleaseProfileCatalog::Parse(missing.dump()), "release.profile.invalid");

    Json cycle = Json::parse(ValidCatalog);
    cycle["presets"][0]["parent"] = "linux-preview";
    RequireError(ReleaseProfileCatalog::Parse(cycle.dump()), "release.profile.conflict");

    cycle = Json::parse(ValidCatalog);
    cycle["presets"][0]["parent"] = "shipping-base";
    RequireError(ReleaseProfileCatalog::Parse(cycle.dump()), "release.profile.conflict");

    ReleaseProfileLimits limits;
    limits.inheritanceDepth = 1U;
    Json deep = Json::parse(ValidCatalog);
    deep["presets"].push_back({{"id", "root"}});
    deep["presets"][0]["parent"] = "root";
    RequireError(ReleaseProfileCatalog::Parse(deep.dump(), limits), "release.profile.limit");
}

TEST_CASE("Typed catalog creation rejects invalid enum casts duplicate IDs and unsafe limits",
          "[unit][application][release][profile][headless]") {
    auto presets = Presets();
    presets[1].product->kind = static_cast<DistributionProductKind>(255);
    RequireError(ReleaseProfileCatalog::Create(std::move(presets)), "release.profile.invalid");

    presets = Presets();
    presets[1].artifactClass = static_cast<DistributionArtifactClass>(255);
    RequireError(ReleaseProfileCatalog::Create(std::move(presets)), "release.profile.invalid");
    presets = Presets();
    presets[1].platform = static_cast<DistributionPlatform>(255);
    RequireError(ReleaseProfileCatalog::Create(std::move(presets)), "release.profile.invalid");
    presets = Presets();
    presets[1].packageFormat = static_cast<DistributionPackageFormat>(255);
    RequireError(ReleaseProfileCatalog::Create(std::move(presets)), "release.profile.invalid");
    presets = Presets();
    presets[1].symbols = static_cast<ReleaseSymbolPolicy>(255);
    RequireError(ReleaseProfileCatalog::Create(std::move(presets)), "release.profile.invalid");
    presets = Presets();
    presets[1].signing = static_cast<ReleaseSigningPolicy>(255);
    RequireError(ReleaseProfileCatalog::Create(std::move(presets)), "release.profile.invalid");
    presets = Presets();
    presets[1].content->assets = static_cast<ReleaseAssetPolicy>(255);
    RequireError(ReleaseProfileCatalog::Create(std::move(presets)), "release.profile.invalid");

    presets = Presets();
    presets[1].id = presets[0].id;
    RequireError(ReleaseProfileCatalog::Create(std::move(presets)), "release.profile.conflict");

    presets = Presets();
    presets[0].parent = presets[0].id;
    RequireError(ReleaseProfileCatalog::Create(std::move(presets)), "release.profile.conflict");

    ReleaseProfileLimits limits;
    limits.identityBytes = 0U;
    RequireError(ReleaseProfileCatalog::Create(Presets(), limits), "release.profile.limit");
    limits = {};
    limits.identityBytes = MaximumDistributionIdentityBytes + 1U;
    RequireError(ReleaseProfileCatalog::Create(Presets(), limits), "release.profile.limit");
}

TEST_CASE("Package signing notarization and artifact overrides are validated by release-domain policy",
          "[unit][application][release][profile][headless]") {
    Json changed = Json::parse(ValidCatalog);
    changed["presets"][1]["packageFormat"] = "windows-msi";
    auto catalog = ReleaseProfileCatalog::Parse(changed.dump());
    REQUIRE(catalog.HasValue());
    RequireError(catalog.Value().Resolve({"linux-preview"}, AvailableCapabilities), "release.distribution.combination_unsupported");

    changed = Json::parse(ValidCatalog);
    changed["presets"][0]["packageFormat"] = "windows-msi";
    changed["presets"][0]["signing"] = "disabled";
    catalog = ReleaseProfileCatalog::Parse(changed.dump());
    REQUIRE(catalog.HasValue());
    RequireError(catalog.Value().Resolve({"shipping-base"}, AvailableCapabilities), "release.profile.conflict");

    changed = Json::parse(ValidCatalog);
    changed["presets"][1]["notarizationRequired"] = true;
    changed["presets"][1]["signing"] = "required";
    catalog = ReleaseProfileCatalog::Parse(changed.dump());
    REQUIRE(catalog.HasValue());
    RequireError(catalog.Value().Resolve({"linux-preview"}, AvailableCapabilities), "release.profile.conflict");

    changed = Json::parse(ValidCatalog);
    changed["presets"][1]["artifactClass"] = "developer-diagnostics";
    catalog = ReleaseProfileCatalog::Parse(changed.dump());
    REQUIRE(catalog.HasValue());
    RequireError(catalog.Value().Resolve({"linux-preview"}, AvailableCapabilities), "release.profile.conflict");
}

TEST_CASE("Profile admission rejects duplicate keys unknown fields identities and resource overflow",
          "[unit][application][release][profile][headless]") {
    std::string duplicate{ValidCatalog};
    duplicate.insert(duplicate.find("\"schemaVersion\""), "\"schemaVersion\": 1,\n  ");
    RequireError(ReleaseProfileCatalog::Parse(duplicate), "release.profile.invalid");

    Json changed = Json::parse(ValidCatalog);
    changed["unknown"] = true;
    RequireError(ReleaseProfileCatalog::Parse(changed.dump()), "release.profile.invalid");

    changed = Json::parse(ValidCatalog);
    changed["presets"][0]["id"] = "Invalid Profile";
    RequireError(ReleaseProfileCatalog::Parse(changed.dump()), "release.profile.invalid");

    ReleaseProfileLimits limits;
    limits.documentBytes = ValidCatalog.size() - 1U;
    RequireError(ReleaseProfileCatalog::Parse(ValidCatalog, limits), "release.profile.limit");

    limits = {};
    limits.presets = 1U;
    RequireError(ReleaseProfileCatalog::Parse(ValidCatalog, limits), "release.profile.limit");

    limits = {};
    limits.destinationsPerPreset = 1U;
    RequireError(ReleaseProfileCatalog::Parse(ValidCatalog, limits), "release.profile.limit");
}
