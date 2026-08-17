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

enum {
    HORO_EXTENSION_ABI_VERSION = 1,
    HORO_ASSET_IMPORTER_ABI_VERSION = 1,
};

typedef enum HoroExtensionStatus {
    HORO_EXTENSION_SUCCESS = 0,
    HORO_EXTENSION_ERROR_VERSION_MISMATCH = 1,
    HORO_EXTENSION_ERROR_INIT_FAILED = 2,
    HORO_EXTENSION_ERROR_INVALID_ARGS = 3,
    HORO_EXTENSION_ERROR_CANCELLED = 4,
    HORO_EXTENSION_ERROR_OUTPUT_REJECTED = 5,
} HoroExtensionStatus;

/** @brief Borrowed byte-counted UTF-8 text; it need not be null-terminated. */
typedef struct HoroExtensionStringView {
    const char *data;
    uint32_t length;
} HoroExtensionStringView;

/** @brief Host-owned byte output resized and filled by a module callback. */
typedef struct HoroExtensionByteSink {
    void *context;
    HoroExtensionStatus (*resize)(void *context, uint64_t byteCount, uint8_t **outBytes);
} HoroExtensionByteSink;

/** @brief Cooperative cancellation query valid only during one callback. */
typedef struct HoroExtensionCancellation {
    void *context;
    uint8_t (*isCancellationRequested)(void *context);
} HoroExtensionCancellation;

typedef enum HoroAssetImportSettingKind {
    HORO_ASSET_IMPORT_SETTING_BOOLEAN = 0,
    HORO_ASSET_IMPORT_SETTING_INTEGER = 1,
    HORO_ASSET_IMPORT_SETTING_FLOAT = 2,
    HORO_ASSET_IMPORT_SETTING_TEXT = 3,
    HORO_ASSET_IMPORT_SETTING_CHOICE = 4,
} HoroAssetImportSettingKind;

/** @brief One tagged import-setting value. */
typedef struct HoroAssetImportSettingValue {
    HoroAssetImportSettingKind kind;
    uint8_t booleanValue;
    int64_t integerValue;
    double floatValue;
    uint64_t choiceIndex;
    HoroExtensionStringView textValue;
} HoroAssetImportSettingValue;

/** @brief One declarative choice belonging to a choice setting. */
typedef struct HoroAssetImportSettingChoice {
    HoroExtensionStringView id;
    HoroExtensionStringView labelKey;
    HoroAssetImportSettingValue value;
} HoroAssetImportSettingChoice;

/** @brief One host-rendered declarative importer setting. */
typedef struct HoroAssetImportSettingDescriptor {
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
} HoroAssetImportSettingDescriptor;

/** @brief Borrowed input for one external importer invocation. */
typedef struct HoroAssetImportRequest {
    uint32_t structSize;
    const uint8_t *sourceBytes;
    uint64_t sourceByteCount;
    HoroExtensionStringView sourceExtension;
    const HoroAssetImportSettingValue *settings;
    uint32_t settingCount;
    HoroExtensionCancellation cancellation;
} HoroAssetImportRequest;

/** @brief Host-owned output surfaces filled by one external importer invocation. */
typedef struct HoroAssetImportResponse {
    uint32_t structSize;
    HoroExtensionStringView assetType;
    HoroExtensionByteSink editorPayload;
} HoroAssetImportResponse;

/** @brief Borrowed input for one rendering-neutral preview invocation. */
typedef struct HoroAssetPreviewRequest {
    uint32_t structSize;
    const uint8_t *editorPayload;
    uint64_t editorPayloadByteCount;
    HoroExtensionStringView absoluteAssetPath;
    HoroExtensionStringView assetType;
    uint32_t width;
    uint32_t height;
    HoroExtensionCancellation cancellation;
} HoroAssetPreviewRequest;

/** @brief Host-owned tightly packed RGBA8 preview output. */
typedef struct HoroAssetPreviewResponse {
    uint32_t structSize;
    uint32_t width;
    uint32_t height;
    HoroExtensionByteSink rgba8Pixels;
} HoroAssetPreviewResponse;

typedef HoroExtensionStatus (*HoroAssetImportFunc)(void *importerContext, const HoroAssetImportRequest *request,
                                                   HoroAssetImportResponse *response);
typedef HoroExtensionStatus (*HoroAssetPreviewFunc)(void *importerContext, const HoroAssetPreviewRequest *request,
                                                    HoroAssetPreviewResponse *response);
typedef void (*HoroAssetImporterDestroyFunc)(void *importerContext);

/** @brief Complete versioned descriptor registered at the asset.importer extension point. */
typedef struct HoroAssetImporterDescriptor {
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
} HoroAssetImporterDescriptor;

/** @brief Host callback used only while the module load transaction is active. */
typedef HoroExtensionStatus (*HoroRegisterAssetImporterFunc)(void *hostContext, const HoroAssetImporterDescriptor *descriptor);

typedef struct HoroExtensionHostApi {
    /** @brief Size of this struct for append-only ABI negotiation. */
    uint32_t structSize;
    uint32_t abiVersion;
    HoroExtensionStringView engineVersion;
    void *hostContext;
    HoroRegisterAssetImporterFunc registerAssetImporter;
} HoroExtensionHostApi;

typedef struct HoroExtensionModuleApi {
    /** @brief Size of this struct for append-only ABI negotiation. */
    uint32_t structSize;
    void *moduleContext;
    HoroExtensionStringView moduleId;
    HoroExtensionStringView moduleVersion;
} HoroExtensionModuleApi;

/**
 * @brief Standard native extension entry point.
 * @param host Host API valid only for the duration of this call.
 * @param outModule Module lifecycle state populated by the extension.
 * @return HORO_EXTENSION_SUCCESS when initialization and registrations succeed.
 */
typedef HoroExtensionStatus (*HoroExtensionLoadFunc)(const HoroExtensionHostApi *host, HoroExtensionModuleApi *outModule);

/**
 * @brief Optional native extension unload point.
 * @param module Module state originally returned by the load entry point.
 */
typedef void (*HoroExtensionUnloadFunc)(HoroExtensionModuleApi *module);
