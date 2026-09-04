#include "ExternalAssetImporter.h"

#include <array>
#include <catch2/catch_test_macros.hpp>

namespace Horo::Extensions::Tests {
    struct RegistrationFixture {
        int destroyed{};
        ExtensionManifest manifest;
        ExtensionModuleManifest owner{.id = "com.example.native", .version = "1.0.0"};
        HoroExtensionStringView extension{"raw", 3};
        HoroExtensionStringView assetType{"example.raw", 11};
        AssetImporterRegistrationSession session{.manifest = &manifest, .extensionModule = &owner};
        HoroAssetImporterDescriptor descriptor{.structSize = sizeof(HoroAssetImporterDescriptor),
                                               .abiVersion = HORO_ASSET_IMPORTER_ABI_VERSION,
                                               .contributionId = {"com.example.raw", 15},
                                               .contributionVersion = {"1.0.0", 5},
                                               .fileExtensions = &extension,
                                               .fileExtensionCount = 1,
                                               .assetTypes = &assetType,
                                               .assetTypeCount = 1,
                                               .targetExtension = {".horoasset", 10},
                                               .importerContext = &destroyed,
                                               .importAsset = [](void *, const HoroAssetImportRequest *,
                                                                 HoroAssetImportResponse *) -> HoroExtensionStatus {
            return HORO_EXTENSION_SUCCESS;
        },
                                               .destroyImporter = [](void *context) {
            ++*static_cast<int *>(context);
        }};

        RegistrationFixture() {
            manifest.id = "com.example.package";
            manifest.contributions.push_back({.type = "asset.importer", .id = "com.example.raw", .owningModule = owner.id});
        }
    };

    TEST_CASE_METHOD(RegistrationFixture, "Importer context transfers only after successful registration", "[Extensions][ABI]") {
        REQUIRE(RegisterExternalAssetImporter(&session, &descriptor) == HORO_EXTENSION_SUCCESS);
        REQUIRE(session.contributions.size() == 1);
        CHECK(destroyed == 0);
        descriptor.structSize = 1;
        CHECK(RegisterExternalAssetImporter(&session, &descriptor) == HORO_EXTENSION_ERROR_INVALID_ARGS);
        CHECK(session.failed);
        CHECK(destroyed == 0);
        session.contributions.clear();
        CHECK(destroyed == 1);

        using enum Assets::ImportSettingKind;
        const std::array kinds{std::pair{HORO_ASSET_IMPORT_SETTING_BOOLEAN, Boolean}, std::pair{HORO_ASSET_IMPORT_SETTING_INTEGER, Integer},
                               std::pair{HORO_ASSET_IMPORT_SETTING_FLOAT, Float}, std::pair{HORO_ASSET_IMPORT_SETTING_TEXT, Text},
                               std::pair{HORO_ASSET_IMPORT_SETTING_CHOICE, Choice}};
        for (const auto &[abiKind, expected] : kinds) {
            RegistrationFixture fixture;
            const HoroAssetImportSettingDescriptor setting{.id = {"setting", 7},
                                                           .labelKey = {"setting.label", 13},
                                                           .kind = static_cast<HoroAssetImportSettingKind>(abiKind),
                                                           .defaultValue = {.kind = static_cast<HoroAssetImportSettingKind>(abiKind)}};
            fixture.descriptor.settings = &setting;
            fixture.descriptor.settingCount = 1;
            REQUIRE(RegisterExternalAssetImporter(&fixture.session, &fixture.descriptor) == HORO_EXTENSION_SUCCESS);
            REQUIRE(fixture.session.contributions.front().settings.size() == 1);
            CHECK(fixture.session.contributions.front().settings.front().kind == expected);
        }

        const HoroAssetImportSettingDescriptor unknown{.id = {"setting", 7}, .labelKey = {"setting.label", 13}, .kind = 999};
        RegistrationFixture invalid;
        invalid.descriptor.settings = &unknown;
        invalid.descriptor.settingCount = 1;
        CHECK(RegisterExternalAssetImporter(&invalid.session, &invalid.descriptor) == HORO_EXTENSION_ERROR_INVALID_ARGS);
    }

    TEST_CASE("Malformed importer descriptors retain caller ownership", "[Extensions][ABI]") {
        using Mutate = void (*)(RegistrationFixture &);
        const std::array<Mutate, 13> invalid{[](RegistrationFixture &f) {
            f.descriptor.structSize = 1;
        }, [](RegistrationFixture &f) {
            f.descriptor.abiVersion = 99;
        }, [](RegistrationFixture &f) {
            f.descriptor.importAsset = nullptr;
        }, [](RegistrationFixture &f) {
            f.descriptor.destroyImporter = nullptr;
        }, [](RegistrationFixture &f) {
            f.descriptor.fileExtensionCount = 257;
        }, [](RegistrationFixture &f) {
            f.descriptor.contributionId = {};
        }, [](RegistrationFixture &f) {
            f.manifest.contributions.clear();
        }, [](RegistrationFixture &f) {
            f.descriptor.previewFallback = 255;
        }, [](RegistrationFixture &f) {
            f.extension = {"INVALID", 7};
        }, [](RegistrationFixture &f) {
            f.assetType = {};
        }, [](RegistrationFixture &f) {
            f.assetType = {"bad type", 8};
        }, [](RegistrationFixture &f) {
            f.descriptor.settingCount = 1;
        }, [](RegistrationFixture &f) {
            f.extension = {".raw", 4};
        }};
        for (const auto mutate : invalid) {
            RegistrationFixture fixture;
            mutate(fixture);
            CHECK(RegisterExternalAssetImporter(&fixture.session, &fixture.descriptor) == HORO_EXTENSION_ERROR_INVALID_ARGS);
            CHECK(fixture.session.failed);
            CHECK(fixture.session.contributions.empty());
            CHECK(fixture.destroyed == 0);
        }
        CHECK(RegisterExternalAssetImporter(nullptr, nullptr) == HORO_EXTENSION_ERROR_INVALID_ARGS);
    }
}  // namespace Horo::Extensions::Tests
