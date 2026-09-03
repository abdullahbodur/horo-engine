#include "Horo/Assets/AssetImportMetadata.h"
#include "Horo/Assets/AssetReimport.h"
#include "Horo/Extensions/ExtensionDiscovery.h"
#include "Horo/Extensions/ExtensionErrors.h"
#include "Horo/Extensions/ExtensionInventory.h"
#include "Horo/Extensions/ExtensionManager.h"
#include "Horo/Extensions/ExtensionManifest.h"
#include "Horo/Extensions/ExtensionMarketplace.h"
#include "Horo/Foundation/Platform.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <ranges>
#include <string>

namespace Horo::Extensions::Tests {
    namespace fs = std::filesystem;

    TEST_CASE("Marketplace registry parsing filters immutable compatible entries") {
        constexpr std::string_view Registry = R"json({
          "packages": [{
            "id": "com.example.mesh-tools",
            "displayName": "Mesh Tools",
            "description": "Mesh import helpers",
            "publisher": "Example",
            "latest": "1.2.0",
            "versions": {
              "1.2.0": {
                "packageUrl": "https://example.com/mesh-tools-1.2.0.zip",
                "sha256": "sha256:0000000000000000000000000000000000000000000000000000000000000000"
              }
            }
          }]
        })json";

        auto parsed = ParseExtensionMarketplaceRegistry(Registry, "mesh");
        REQUIRE(parsed.HasValue());
        REQUIRE(parsed.Value().size() == 1);
        CHECK(parsed.Value().front().packageId == "com.example.mesh-tools");
        CHECK(parsed.Value().front().version == "1.2.0");

        auto noMatch = ParseExtensionMarketplaceRegistry(Registry, "audio");
        REQUIRE(noMatch.HasValue());
        CHECK(noMatch.Value().empty());
    }

    TEST_CASE("Marketplace registry rejects mutable or unverifiable artifacts") {
        constexpr std::string_view Registry = R"json({
          "packages": [{
            "id": "com.example.unsafe",
            "latest": "1.0.0",
            "versions": {
              "1.0.0": {
                "packageUrl": "http://example.com/unsafe.zip",
                "sha256": "missing"
              }
            }
          }]
        })json";

        auto parsed = ParseExtensionMarketplaceRegistry(Registry, "");
        REQUIRE(parsed.HasValue());
        CHECK(parsed.Value().empty());
    }

    class ExistingImporter final : public Assets::IAssetImporter {
    public:
        [[nodiscard]] Result<Assets::PreparedAssetImport> Import(const Assets::AssetImportInput &,
                                                                 const CancellationToken &) const override {
            return Result<Assets::PreparedAssetImport>::Failure(MakeError(ExtensionErrors::InvocationFailed, "Not invoked by this test."));
        }
    };

    struct ExtensionManagerTestFixture {
        fs::path tempDir;

        ExtensionManagerTestFixture() {
            tempDir = fs::temp_directory_path() / "horo_extension_tests" / "plugins";
            fs::create_directories(tempDir);
        }

        ~ExtensionManagerTestFixture() {
            std::error_code ec;
            fs::remove_all(tempDir, ec);
        }
    };

    TEST_CASE_METHOD(ExtensionManagerTestFixture, "ExtensionManager Discovery", "[Extensions]") {
        const Discovery::RootRequest root{.id = "user",
                                          .path = fs::absolute(tempDir),
                                          .kind = Discovery::RootKind::User,
                                          .approval = Discovery::RootApproval::Approved,
                                          .configuration = Discovery::ConfigurationOrigin::UserLocal};

        SECTION("No declared packages returns an empty list") {
            const auto discovered = Discovery::DiscoverDeclaredPackages(std::span(&root, 1), {}, {});
            REQUIRE(discovered.HasValue());
            REQUIRE(discovered.Value().packages.empty());
        }

        SECTION("An approved declared package resolves to a canonical directory") {
            fs::path pluginDir = tempDir / "com.example.test_plugin";
            fs::create_directories(pluginDir);
            const Discovery::PackageLocation location{"com.example.test-plugin", "user", "com.example.test_plugin"};
            const auto discovered = Discovery::DiscoverDeclaredPackages(std::span(&root, 1), std::span(&location, 1), {});
            REQUIRE(discovered.HasValue());
            REQUIRE(discovered.Value().packages.size() == 1);
            REQUIRE(discovered.Value().packages[0].canonicalPath == fs::canonical(pluginDir));
        }
    }

    TEST_CASE("ExtensionManifest Parser", "[Extensions]") {
        SECTION("Valid JSON returns manifest") {
            std::string json = R"({
                "package": {
                    "id": "com.example.test",
                    "version": "1.2.3",
                    "kind": "editor_panel",
                    "displayName": "Test Panel",
                    "description": "A test panel",
                    "author": "Horo"
                },
                "modules": [{
                    "id": "com.example.test.importer",
                    "version": "2.0.0",
                    "kind": "asset_importer",
                    "entry": "test_importer"
                }]
            })";

            auto result = ParseExtensionManifest(json);
            REQUIRE(result.HasValue());

            const auto &manifest = result.Value();
            REQUIRE(manifest.id == "com.example.test");
            REQUIRE(manifest.version == "1.2.3");
            REQUIRE(manifest.kind == "editor_panel");
            REQUIRE(manifest.displayName == "Test Panel");
            REQUIRE(manifest.description == "A test panel");
            REQUIRE(manifest.author == "Horo");
            REQUIRE(manifest.modules.size() == 1);
            REQUIRE(manifest.modules[0].id == "com.example.test.importer");
            REQUIRE(manifest.modules[0].version == "2.0.0");
        }

        SECTION("Module version is explicit") {
            auto result = ParseExtensionManifest(R"({
                "package": {"id": "com.example.defaulted", "version": "3.2.1"},
                "modules": [{"id": "com.example.defaulted.importer", "kind": "asset_importer"}]
            })");
            REQUIRE(result.HasError());
            REQUIRE_THAT(result.ErrorValue().message, Catch::Matchers::ContainsSubstring("$.modules[0].version"));
        }

        SECTION("Canonical top-level package manifest is accepted") {
            auto result = ParseExtensionManifest(R"({
                "id": "com.example.top-level",
                "version": "4.0.0",
                "modules": [{
                    "id": "com.example.top-level.importer",
                    "version": "4.1.0",
                    "kind": "asset_importer"
                }]
            })");
            REQUIRE(result.HasValue());
            REQUIRE(result.Value().version == "4.0.0");
            REQUIRE(result.Value().modules[0].version == "4.1.0");
        }

        SECTION("Contribution must reference a module in the same package") {
            auto result = ParseExtensionManifest(R"({
                "id": "com.example.contributions",
                "version": "1.0.0",
                "modules": [{
                    "id": "com.example.contributions.native",
                    "version": "1.0.0",
                    "kind": "asset_importer"
                }],
                "contributions": [{
                    "type": "asset.importer",
                    "id": "com.example.contributions.raw",
                    "module": "com.example.missing"
                }]
            })");
            REQUIRE(result.HasError());
        }

        SECTION("Invalid module version is rejected") {
            auto result = ParseExtensionManifest(R"({
                "package": {"id": "com.example.invalid", "version": "1.0.0"},
                "modules": [{"id": "com.example.invalid.importer", "version": "next"}]
            })");
            REQUIRE(result.HasError());
        }

        SECTION("Missing package returns error") {
            std::string json = R"({
                "some_other_field": "value"
            })";

            auto result = ParseExtensionManifest(json);
            REQUIRE(result.HasError());
        }
    }

    TEST_CASE_METHOD(ExtensionManagerTestFixture, "Extension inventory exposes built-ins and persists activation state",
                     "[Extensions][Inventory]") {
        const fs::path installRoot = fs::absolute(tempDir / "installed");
        ExtensionInventory inventory{installRoot};
        REQUIRE(inventory.Refresh().HasValue());
        REQUIRE(inventory.InstallRoot().is_absolute());
        REQUIRE(inventory.Entries().size() == 1);
        const ExtensionInventoryEntry &builtIn = inventory.Entries().front();
        REQUIRE(builtIn.packageId == "horo.builtin.assets");
        REQUIRE(builtIn.origin == ExtensionOrigin::BuiltIn);
        REQUIRE(builtIn.enabled);
        REQUIRE(builtIn.modules.size() == 2);

        inventory.MarkRuntimeActive(builtIn.packageId);
        REQUIRE_FALSE(inventory.Entries().front().RestartRequired());
        REQUIRE(inventory.SetEnabled(builtIn.packageId, false).HasValue());
        REQUIRE(inventory.Entries().front().RestartRequired());

        ExtensionInventory reloaded{installRoot};
        REQUIRE(reloaded.Refresh().HasValue());
        REQUIRE_FALSE(reloaded.IsEnabled(builtIn.packageId));
        REQUIRE(reloaded.InstallRoot() == installRoot.lexically_normal());
    }

    TEST_CASE_METHOD(ExtensionManagerTestFixture, "Extension manifest files are bounded before decoding", "[Extensions][Inventory]") {
        const fs::path source = fs::absolute(tempDir / "oversized");
        fs::create_directories(source);
        {
            std::ofstream manifest{source / "extension.json", std::ios::binary};
            manifest << std::string(ExtensionManifestLimits{}.maximumDocumentBytes + 1U, 'x');
        }

        ExtensionManager manager;
        REQUIRE(manager.LoadExtension(source.string()).HasError());

        ExtensionInventory inventory{fs::absolute(tempDir / "managed")};
        REQUIRE(inventory.InstallFromDirectory(source).HasError());
    }

#ifdef HORO_BASIC_EXTENSION_DIR
    TEST_CASE_METHOD(ExtensionManagerTestFixture, "Extension inventory installs an absolute local package disabled by default",
                     "[Extensions][Inventory]") {
        const fs::path installRoot = fs::absolute(tempDir / "managed");
        ExtensionInventory inventory{installRoot};
        REQUIRE(inventory.Refresh().HasValue());
        REQUIRE(inventory.InstallFromDirectory("relative/package").HasError());

        const fs::path source = fs::absolute(HORO_BASIC_EXTENSION_DIR);
        auto installed = inventory.InstallFromDirectory(source);
        REQUIRE(installed.HasValue());
        REQUIRE(installed.Value() == "com.horo.examples.asset-importer-basic");

        const auto entry = std::ranges::find(inventory.Entries(), installed.Value(), &ExtensionInventoryEntry::packageId);
        REQUIRE(entry != inventory.Entries().end());
        REQUIRE(entry->origin == ExtensionOrigin::UserInstalled);
        REQUIRE(entry->absoluteRootPath.is_absolute());
        REQUIRE(entry->absoluteManifestPath.is_absolute());
        REQUIRE_FALSE(entry->enabled);
        REQUIRE_FALSE(entry->locallyTrusted);
        REQUIRE(inventory.EnabledUserPackageRoots().empty());

        REQUIRE(inventory.SetEnabled(entry->packageId, true).HasValue());
        const auto roots = inventory.EnabledUserPackageRoots();
        REQUIRE(roots.size() == 1);
        REQUIRE(roots.front().is_absolute());
        inventory.MarkRuntimeActive(installed.Value());
        REQUIRE_FALSE(std::ranges::find(inventory.Entries(), installed.Value(), &ExtensionInventoryEntry::packageId)->RestartRequired());

        const fs::path installedManifest = roots.front() / "extension.json";
        std::ifstream manifestInput(installedManifest, std::ios::binary);
        std::string manifestText{std::istreambuf_iterator<char>{manifestInput}, std::istreambuf_iterator<char>{}};
        const std::string versionField{"\"version\": \"1.0.0\""};
        const std::size_t packageVersion = manifestText.find(versionField);
        REQUIRE(packageVersion != std::string::npos);
        const std::size_t moduleVersion = manifestText.find(versionField, packageVersion + versionField.size());
        REQUIRE(moduleVersion != std::string::npos);
        manifestText.replace(moduleVersion, versionField.size(), "\"version\": \"1.1.0\"");
        {
            std::ofstream manifestOutput(installedManifest, std::ios::binary | std::ios::trunc);
            manifestOutput << manifestText;
        }
        REQUIRE(inventory.Refresh().HasValue());
        const auto updated = std::ranges::find(inventory.Entries(), installed.Value(), &ExtensionInventoryEntry::packageId);
        REQUIRE(updated != inventory.Entries().end());
        REQUIRE(updated->runtimeActive);
        REQUIRE(updated->RestartRequired());
        REQUIRE(inventory.InstallFromDirectory(source).HasError());
    }

    TEST_CASE("External importer registration conflict leaves the catalog candidate unchanged", "[Extensions][Assets]") {
        using namespace Horo::Assets;
        AssetImporterCatalog catalog;
        REQUIRE(catalog
                    .Register(AssetImporterContribution{
                        .contributionId = "com.horo.examples.asset-importer-basic.raw",
                        .packageId = "existing.package",
                        .moduleId = "existing.module",
                        .moduleVersion = "1.0.0",
                        .version = "1.0.0",
                        .fileExtensions = {"existing"},
                        .assetTypes = {AssetTypeId::Parse("example.raw").Value()},
                        .strategy = std::make_shared<const ExistingImporter>(),
                    })
                    .HasValue());

        ExtensionManager manager{&catalog};
        auto loaded = manager.LoadExtension(fs::absolute(HORO_BASIC_EXTENSION_DIR).string());
        REQUIRE(loaded.HasError());
        REQUIRE(manager.GetLoadedExtensionIds().empty());

        auto published = catalog.Publish();
        REQUIRE(published.HasValue());
        const auto *retained = published.Value()->FindById("com.horo.examples.asset-importer-basic.raw");
        REQUIRE(retained != nullptr);
        REQUIRE(retained->packageId == "existing.package");
    }

    TEST_CASE_METHOD(ExtensionManagerTestFixture, "External asset importer loads, previews, reimports, and survives manager release",
                     "[Extensions][Assets]") {
        using namespace Horo::Assets;

        AssetImporterCatalog catalog;
        ExtensionManager manager{&catalog};
        const fs::path packagePath = fs::absolute(HORO_BASIC_EXTENSION_DIR);
        auto loaded = manager.LoadExtension(packagePath.string());
        REQUIRE(loaded.HasValue());
        REQUIRE(loaded.Value() == "com.horo.examples.asset-importer-basic");

        auto published = catalog.Publish();
        REQUIRE(published.HasValue());
        const auto snapshot = published.Value();
        const auto *contribution = snapshot->FindById("com.horo.examples.asset-importer-basic.raw");
        REQUIRE(contribution != nullptr);
        REQUIRE(contribution->packageId == "com.horo.examples.asset-importer-basic");
        REQUIRE(contribution->moduleId == "com.horo.examples.asset-importer-basic.native");
        REQUIRE(contribution->moduleVersion == "1.0.0");
        REQUIRE(contribution->version == "1.0.0");
        REQUIRE(contribution->settings.size() == 1);
        REQUIRE(contribution->settings[0].id == "invertPreview");
        REQUIRE(contribution->settings[0].includeInPresets);
        REQUIRE(contribution->previewProvider != nullptr);

        const std::vector<std::uint8_t> sourceBytes{7U, 8U, 9U};
        auto imported = contribution->strategy->Import(
            AssetImportInput{
                .sourceBytes = sourceBytes,
                .sourceExtension = "hraw",
                .settings = {false},
            },
            CancellationToken{});
        REQUIRE(imported.HasValue());
        REQUIRE(imported.Value().type.Value() == "example.raw");
        REQUIRE(imported.Value().editorPayload.size() == sourceBytes.size() + 5);

        auto preview = contribution->previewProvider->GeneratePreview(
            AssetPreviewInput{
                .editorPayload = imported.Value().editorPayload,
                .absoluteAssetPath = (tempDir / "preview.horoasset").string(),
                .assetType = imported.Value().type,
                .width = 16,
                .height = 12,
            },
            CancellationToken{});
        REQUIRE(preview.HasValue());
        REQUIRE(preview.Value().IsValid());

        manager.UnloadExtension(loaded.Value());
        REQUIRE(manager.GetLoadedExtensionIds().empty());
        auto afterRelease = contribution->strategy->Import(
            AssetImportInput{
                .sourceBytes = sourceBytes,
                .sourceExtension = "hraw",
                .settings = {false},
            },
            CancellationToken{});
        REQUIRE(afterRelease.HasValue());

        const fs::path projectRoot = tempDir / "project";
        const fs::path sourcePath = projectRoot / "source.hraw";
        const fs::path assetPath = projectRoot / "assets" / "sample.horoasset";
        const fs::path sidecarPath = assetPath.string() + ".horo";
        fs::create_directories(assetPath.parent_path());
        {
            std::ofstream source(sourcePath, std::ios::binary);
            source << "old";
            std::ofstream asset(assetPath, std::ios::binary);
            asset << "old-payload";
        }
        auto oldSource = ReadAssetImportSource(fs::absolute(sourcePath));
        REQUIRE(oldSource.HasValue());
        const AssetId assetId = AssetId::Parse("11112222-3333-4444-8555-666677778888").Value();
        AssetImportMetadata metadata{
            .assetId = assetId,
            .assetType = AssetTypeId::Parse("example.raw").Value(),
            .importerContributionId = contribution->contributionId,
            .importerVersion = "0.9.0",
            .importerPackageId = contribution->packageId,
            .importerModuleId = contribution->moduleId,
            .importerModuleVersion = "0.9.0",
            .absoluteSourcePath = fs::absolute(sourcePath),
            .sourceExtension = "hraw",
            .sourceHash = HashAssetImportSource(oldSource.Value()),
            .sourceByteSize = oldSource.Value().size(),
            .importSettings = {{"settings.invertPreview", "false"}},
            .lastImportReasons = {AssetImportReason::InitialImport},
            .importedAtUtc = CurrentImportTimestampUtc(),
        };
        auto serialized = SerializeAssetImportMetadata(metadata);
        REQUIRE(serialized.HasValue());
        {
            std::ofstream sidecar(sidecarPath, std::ios::binary);
            sidecar << serialized.Value();
            std::ofstream changedSource(sourcePath, std::ios::binary | std::ios::trunc);
            changedSource << "changed";
        }

        AssetRegistry registry;
        REQUIRE(RebuildAssetRegistry(registry, fs::absolute(projectRoot), AssetRegistryOpenMode::Edit).HasValue());
        NativeDurableFileSystem files;
        auto reimported = ReimportProjectAsset(
            AssetReimportRequest{
                .absoluteProjectRoot = fs::absolute(projectRoot),
                .absoluteAssetPath = fs::absolute(assetPath),
                .importerCatalog = snapshot.get(),
                .registry = &registry,
                .files = &files,
            },
            CancellationToken{});
        REQUIRE(reimported.HasValue());
        REQUIRE(reimported.Value().assetId == assetId);
        REQUIRE(reimported.Value().reasons.size() == 3);
        REQUIRE(reimported.Value().reasons[0] == AssetImportReason::SourceChanged);
        REQUIRE(reimported.Value().reasons[1] == AssetImportReason::ImporterChanged);
        REQUIRE(reimported.Value().reasons[2] == AssetImportReason::ModuleChanged);
    }
#endif

}  // namespace Horo::Extensions::Tests
