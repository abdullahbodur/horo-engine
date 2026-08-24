#pragma once

/**
 * @file ExtensionAbi.h
 * @brief Versioned C ABI shared by native extension modules and the Horo host.
 *
 * All text and arrays are borrowed for the duration of the call that receives
 * them. Modules and hosts must not pass C++ objects, exceptions, or allocator
 * ownership across this boundary.
 */

#include <stdint.h>

#ifdef _WIN32
#ifdef __cplusplus
#define HORO_EXTENSION_EXPORT extern "C" __declspec(dllexport)
#else
#define HORO_EXTENSION_EXPORT __declspec(dllexport)
#endif
#else
#ifdef __cplusplus
#define HORO_EXTENSION_EXPORT extern "C" __attribute__((visibility("default")))
#else
#define HORO_EXTENSION_EXPORT __attribute__((visibility("default")))
#endif
#endif

enum {  // NOSONAR(cpp:S3642) C ABI constant group
    HORO_EXTENSION_ABI_VERSION = 1,
    HORO_ASSET_IMPORTER_ABI_VERSION = 1,
};

enum HoroExtensionStatus {  // NOSONAR(cpp:S3642) C ABI enumeration
    HORO_EXTENSION_SUCCESS = 0,
    HORO_EXTENSION_ERROR_VERSION_MISMATCH = 1,
    HORO_EXTENSION_ERROR_INIT_FAILED = 2,
    HORO_EXTENSION_ERROR_INVALID_ARGS = 3,
    HORO_EXTENSION_ERROR_CANCELLED = 4,
    HORO_EXTENSION_ERROR_OUTPUT_REJECTED = 5,
};
using HoroExtensionStatus = enum HoroExtensionStatus;

/** @brief Borrowed byte-counted UTF-8 text; it need not be null-terminated. */
struct HoroExtensionStringView {
    const char *data;
    uint32_t length;
};
using HoroExtensionStringView = struct HoroExtensionStringView;

/** @brief Host-owned byte output resized and filled by a module callback. */
struct HoroExtensionByteSink {
    void *context;
    HoroExtensionStatus (*resize)(void *context, uint64_t byteCount, uint8_t **outBytes);
};
using HoroExtensionByteSink = struct HoroExtensionByteSink;

/** @brief Cooperative cancellation query valid only during one callback. */
struct HoroExtensionCancellation {
    const void *context;
    uint8_t (*isCancellationRequested)(const void *context);
};
using HoroExtensionCancellation = struct HoroExtensionCancellation;

// Unscoped enumerators are part of the source-compatible C ABI surface.
enum HoroAssetImportSettingKind {  // NOSONAR(cpp:S3642)
    HORO_ASSET_IMPORT_SETTING_BOOLEAN = 0,
    HORO_ASSET_IMPORT_SETTING_INTEGER = 1,
    HORO_ASSET_IMPORT_SETTING_FLOAT = 2,
    HORO_ASSET_IMPORT_SETTING_TEXT = 3,
    HORO_ASSET_IMPORT_SETTING_CHOICE = 4,
};
using HoroAssetImportSettingKind = enum HoroAssetImportSettingKind;

/** @brief One tagged import-setting value. */
struct HoroAssetImportSettingValue {
    HoroAssetImportSettingKind kind;
    uint8_t booleanValue;
    int64_t integerValue;
    double floatValue;
    uint64_t choiceIndex;
    HoroExtensionStringView textValue;
};
using HoroAssetImportSettingValue = struct HoroAssetImportSettingValue;

/** @brief One declarative choice belonging to a choice setting. */
struct HoroAssetImportSettingChoice {
    HoroExtensionStringView id;
    HoroExtensionStringView labelKey;
    HoroAssetImportSettingValue value;
};
using HoroAssetImportSettingChoice = struct HoroAssetImportSettingChoice;

/** @brief One host-rendered declarative importer setting. */
struct HoroAssetImportSettingDescriptor {
    HoroExtensionStringView id;
    HoroExtensionStringView labelKey;
    HoroExtensionStringView descriptionKey;
    HoroAssetImportSettingKind kind;
    HoroAssetImportSettingValue defaultValue;
    uint8_t hasMinimum;
    uint8_t hasMaximum;
    double minimum;
    double maximum;
    const HoroAssetImportSettingChoice *choices;
    uint32_t choiceCount;
    uint8_t includeInPresets;
};
using HoroAssetImportSettingDescriptor = struct HoroAssetImportSettingDescriptor;

/** @brief Borrowed input for one external importer invocation. */
struct HoroAssetImportRequest {
    uint32_t structSize;
    const uint8_t *sourceBytes;
    uint64_t sourceByteCount;
    HoroExtensionStringView sourceExtension;
    const HoroAssetImportSettingValue *settings;
    uint32_t settingCount;
    HoroExtensionCancellation cancellation;
};
using HoroAssetImportRequest = struct HoroAssetImportRequest;

/** @brief Host-owned output surfaces filled by one external importer invocation. */
struct HoroAssetImportResponse {
    uint32_t structSize;
    HoroExtensionStringView assetType;
    HoroExtensionByteSink editorPayload;
};
using HoroAssetImportResponse = struct HoroAssetImportResponse;

/** @brief Borrowed input for one rendering-neutral preview invocation. */
struct HoroAssetPreviewRequest {
    uint32_t structSize;
    const uint8_t *editorPayload;
    uint64_t editorPayloadByteCount;
    HoroExtensionStringView absoluteAssetPath;
    HoroExtensionStringView assetType;
    uint32_t width;
    uint32_t height;
    HoroExtensionCancellation cancellation;
};
using HoroAssetPreviewRequest = struct HoroAssetPreviewRequest;

/** @brief Host-owned tightly packed RGBA8 preview output. */
struct HoroAssetPreviewResponse {
    uint32_t structSize;
    uint32_t width;
    uint32_t height;
    HoroExtensionByteSink rgba8Pixels;
};
using HoroAssetPreviewResponse = struct HoroAssetPreviewResponse;

using HoroAssetImportFunc = HoroExtensionStatus (*)(void *importerContext, const HoroAssetImportRequest *request,
                                                    HoroAssetImportResponse *response);
using HoroAssetPreviewFunc = HoroExtensionStatus (*)(void *importerContext, const HoroAssetPreviewRequest *request,
                                                     HoroAssetPreviewResponse *response);
using HoroAssetImporterDestroyFunc = void (*)(void *importerContext);

/** @brief Complete versioned descriptor registered at the asset.importer extension point. */
struct HoroAssetImporterDescriptor {
    uint32_t structSize;
    uint32_t abiVersion;
    HoroExtensionStringView contributionId;
    HoroExtensionStringView contributionVersion;
    const HoroExtensionStringView *fileExtensions;
    uint32_t fileExtensionCount;
    const HoroExtensionStringView *assetTypes;
    uint32_t assetTypeCount;
    HoroExtensionStringView subfolderCategory;
    HoroExtensionStringView targetExtension;
    uint8_t supportsMetaSidecar;
    uint8_t previewFallback;
    const HoroAssetImportSettingDescriptor *settings;
    uint32_t settingCount;
    void *importerContext;
    HoroAssetImportFunc importAsset;
    HoroAssetPreviewFunc generatePreview;
    HoroAssetImporterDestroyFunc destroyImporter;
};
using HoroAssetImporterDescriptor = struct HoroAssetImporterDescriptor;

/** @brief Host callback used only while the module load transaction is active. */
using HoroRegisterAssetImporterFunc = HoroExtensionStatus (*)(void *hostContext, const HoroAssetImporterDescriptor *descriptor);

struct HoroExtensionHostApi {
    /** @brief Size of this struct for append-only ABI negotiation. */
    uint32_t structSize;
    uint32_t abiVersion;
    HoroExtensionStringView engineVersion;
    void *hostContext;
    HoroRegisterAssetImporterFunc registerAssetImporter;
};
using HoroExtensionHostApi = struct HoroExtensionHostApi;

struct HoroExtensionModuleApi {
    /** @brief Size of this struct for append-only ABI negotiation. */
    uint32_t structSize;
    void *moduleContext;
    HoroExtensionStringView moduleId;
    HoroExtensionStringView moduleVersion;
};
using HoroExtensionModuleApi = struct HoroExtensionModuleApi;

/**
 * @brief Standard native extension entry point.
 * @param host Host API valid only for the duration of this call.
 * @param outModule Module lifecycle state populated by the extension.
 * @return HORO_EXTENSION_SUCCESS when initialization and registrations succeed.
 */
using HoroExtensionLoadFunc = HoroExtensionStatus (*)(const HoroExtensionHostApi *host, HoroExtensionModuleApi *outModule);

/**
 * @brief Optional native extension unload point.
 * @param extensionModule Module state originally returned by the load entry point.
 */
using HoroExtensionUnloadFunc = void (*)(HoroExtensionModuleApi *extensionModule);
