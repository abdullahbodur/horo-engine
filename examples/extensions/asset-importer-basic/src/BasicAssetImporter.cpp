#include "Horo/Extensions/ExtensionAbi.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <memory>

namespace {
    constexpr char kModuleId[] = "com.horo.examples.asset-importer-basic.native";
    constexpr char kModuleVersion[] = "1.0.0";
    constexpr char kContributionId[] = "com.horo.examples.asset-importer-basic.raw";
    constexpr char kContributionVersion[] = "1.0.0";
    constexpr char kExtension[] = "hraw";
    constexpr char kAssetType[] = "example.raw";
    constexpr char kTargetExtension[] = ".horoasset";
    constexpr char kSettingId[] = "invertPreview";
    constexpr char kSettingLabel[] = "examples.asset_importer_basic.invert_preview";
    constexpr char kSettingDescription[] = "examples.asset_importer_basic.invert_preview.description";
    constexpr std::array<std::uint8_t, 5> kPayloadMagic{'H', 'R', 'A', 'W', 1U};

    template <std::size_t Size> constexpr HoroExtensionStringView Text(const char (&value)[Size]) {
        return {value, static_cast<std::uint32_t>(Size - 1)};
    }

    struct ImporterState {
        std::uint32_t invocationCount{};
    };

    struct ModuleState {
        bool loaded{true};
    };

    HoroExtensionStatus ImportAsset(ImporterState *importer, const HoroAssetImportRequest *request, HoroAssetImportResponse *response) {
        if (importer == nullptr || request == nullptr || response == nullptr || request->structSize < sizeof(HoroAssetImportRequest) ||
            response->structSize < sizeof(HoroAssetImportResponse) || response->editorPayload.resize == nullptr ||
            (request->sourceBytes == nullptr && request->sourceByteCount != 0))
            return HORO_EXTENSION_ERROR_INVALID_ARGS;
        if (request->cancellation.isCancellationRequested != nullptr &&
            request->cancellation.isCancellationRequested(request->cancellation.context) != 0)
            return HORO_EXTENSION_ERROR_CANCELLED;

        const std::uint64_t outputSize = kPayloadMagic.size() + request->sourceByteCount;
        std::uint8_t *output = nullptr;
        if (response->editorPayload.resize(response->editorPayload.context, outputSize, &output) != HORO_EXTENSION_SUCCESS ||
            output == nullptr)
            return HORO_EXTENSION_ERROR_OUTPUT_REJECTED;

        std::memcpy(output, kPayloadMagic.data(), kPayloadMagic.size());
        if (request->sourceByteCount != 0)
            std::memcpy(output + kPayloadMagic.size(), request->sourceBytes, request->sourceByteCount);
        response->assetType = Text(kAssetType);
        ++importer->invocationCount;
        return HORO_EXTENSION_SUCCESS;
    }

    HoroExtensionStatus GeneratePreview(ImporterState * /*importer*/, const HoroAssetPreviewRequest *request,
                                        HoroAssetPreviewResponse *response) {
        if (request == nullptr || response == nullptr || request->structSize < sizeof(HoroAssetPreviewRequest) ||
            response->structSize < sizeof(HoroAssetPreviewResponse) || response->rgba8Pixels.resize == nullptr || request->width == 0 ||
            request->height == 0)
            return HORO_EXTENSION_ERROR_INVALID_ARGS;
        if (request->cancellation.isCancellationRequested != nullptr &&
            request->cancellation.isCancellationRequested(request->cancellation.context) != 0)
            return HORO_EXTENSION_ERROR_CANCELLED;

        const std::uint64_t byteCount = static_cast<std::uint64_t>(request->width) * request->height * 4U;
        std::uint8_t *pixels = nullptr;
        if (response->rgba8Pixels.resize(response->rgba8Pixels.context, byteCount, &pixels) != HORO_EXTENSION_SUCCESS || pixels == nullptr)
            return HORO_EXTENSION_ERROR_OUTPUT_REJECTED;

        bool inverted = false;
        if (request->editorPayloadByteCount > kPayloadMagic.size())
            inverted = request->editorPayload[kPayloadMagic.size()] == 0U;
        for (std::uint32_t y = 0; y < request->height; ++y) {
            for (std::uint32_t x = 0; x < request->width; ++x) {
                const bool bright = ((x / 8U) + (y / 8U)) % 2U == 0U;
                const std::uint8_t value = (bright != inverted) ? 220U : 48U;
                const std::size_t offset = (static_cast<std::size_t>(y) * request->width + x) * 4U;
                pixels[offset + 0] = value;
                pixels[offset + 1] = static_cast<std::uint8_t>(128U + value / 3U);
                pixels[offset + 2] = static_cast<std::uint8_t>(255U - value / 2U);
                pixels[offset + 3] = 255U;
            }
        }
        response->width = request->width;
        response->height = request->height;
        return HORO_EXTENSION_SUCCESS;
    }

    void DestroyImporter(ImporterState *importer) {
        const std::unique_ptr<ImporterState> scopedImporter{importer};
    }
}  // namespace

HORO_EXTENSION_EXPORT HoroExtensionStatus horo_extension_load(const HoroExtensionHostApi *host, HoroExtensionModuleApi *outModule) {
    if (host == nullptr || outModule == nullptr || host->structSize < sizeof(HoroExtensionHostApi) ||
        outModule->structSize < sizeof(HoroExtensionModuleApi) || host->abiVersion != HORO_EXTENSION_ABI_VERSION ||
        host->registerAssetImporter == nullptr)
        return HORO_EXTENSION_ERROR_VERSION_MISMATCH;

    auto importer = std::make_unique<ImporterState>();
    auto moduleState = std::make_unique<ModuleState>();

    constexpr HoroExtensionStringView extensions[]{Text(kExtension)};
    constexpr HoroExtensionStringView assetTypes[]{Text(kAssetType)};
    HoroAssetImportSettingDescriptor setting{
        .id = Text(kSettingId),
        .labelKey = Text(kSettingLabel),
        .descriptionKey = Text(kSettingDescription),
        .kind = HORO_ASSET_IMPORT_SETTING_BOOLEAN,
        .defaultValue =
            {
                .kind = HORO_ASSET_IMPORT_SETTING_BOOLEAN,
                .booleanValue = 0,
            },
        .includeInPresets = 1,
    };
    if (const HoroAssetImporterDescriptor descriptor{
            .structSize = sizeof(HoroAssetImporterDescriptor),
            .abiVersion = HORO_ASSET_IMPORTER_ABI_VERSION,
            .contributionId = Text(kContributionId),
            .contributionVersion = Text(kContributionVersion),
            .fileExtensions = extensions,
            .fileExtensionCount = 1,
            .assetTypes = assetTypes,
            .assetTypeCount = 1,
            .subfolderCategory = {},
            .targetExtension = Text(kTargetExtension),
            .supportsMetaSidecar = 1,
            .previewFallback = 4,
            .settings = &setting,
            .settingCount = 1,
            .importerContext = importer.get(),
            .importAsset =
                [](auto *context, const HoroAssetImportRequest *req, HoroAssetImportResponse *resp) {
        return ImportAsset(static_cast<ImporterState *>(context), req, resp);
    },
            .generatePreview =
                [](auto *context, const HoroAssetPreviewRequest *req, HoroAssetPreviewResponse *resp) {
        return GeneratePreview(static_cast<ImporterState *>(context), req, resp);
    },
            .destroyImporter =
                [](auto *context) {
        DestroyImporter(static_cast<ImporterState *>(context));
    },
        };
        host->registerAssetImporter(host->hostContext, &descriptor) != HORO_EXTENSION_SUCCESS) {
        return HORO_EXTENSION_ERROR_INIT_FAILED;
    }

    outModule->moduleContext = moduleState.release();
    outModule->moduleId = Text(kModuleId);
    outModule->moduleVersion = Text(kModuleVersion);
    importer.release();
    return HORO_EXTENSION_SUCCESS;
}

HORO_EXTENSION_EXPORT void horo_extension_unload(HoroExtensionModuleApi *moduleApi) {
    if (moduleApi != nullptr) {
        const std::unique_ptr<ModuleState> scopedModule{static_cast<ModuleState *>(moduleApi->moduleContext)};
        moduleApi->moduleContext = nullptr;
    }
}
