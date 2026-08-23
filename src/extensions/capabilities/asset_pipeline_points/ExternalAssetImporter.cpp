#include "ExternalAssetImporter.h"

#include "Horo/Extensions/ExtensionErrors.h"
#include "Horo/Foundation/Logging/Logger.h"

#include <algorithm>
#include <cctype>
#include <limits>
#include <new>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

namespace Horo::Extensions {
    namespace {
        constexpr std::uint32_t kMaxTextBytes = 4096;
        constexpr std::uint32_t kMaxListEntries = 256;
        constexpr std::uint64_t kMaxPayloadBytes = 1024ULL * 1024ULL * 1024ULL;
        constexpr std::uint32_t kMaxPreviewDimension = 4096;

        [[nodiscard]] bool CopyText(const HoroExtensionStringView view, std::string &output, const bool allowEmpty = false) {
            if (view.length > kMaxTextBytes || (view.data == nullptr && view.length != 0) || (!allowEmpty && view.length == 0))
                return false;
            output.assign(view.data != nullptr ? view.data : "", view.length);
            return true;
        }

        [[nodiscard]] bool IsLowerExtension(const std::string &value) {
            return !value.empty() && value.front() != '.' && std::ranges::all_of(value, [](const unsigned char character) {
                return std::isdigit(character) != 0 || std::islower(character) != 0 || character == '_' || character == '-';
            });
        }

        [[nodiscard]] bool ConvertValue(const HoroAssetImportSettingValue &source, Assets::ImportSettingValue &destination) {
            using enum Assets::ImportSettingKind;
            switch (source.kind) {
                case HORO_ASSET_IMPORT_SETTING_BOOLEAN:
                    destination = source.booleanValue != 0;
                    return true;
                case HORO_ASSET_IMPORT_SETTING_INTEGER:
                    destination = source.integerValue;
                    return true;
                case HORO_ASSET_IMPORT_SETTING_FLOAT:
                    destination = source.floatValue;
                    return true;
                case HORO_ASSET_IMPORT_SETTING_TEXT: {
                    std::string value;
                    if (!CopyText(source.textValue, value, true))
                        return false;
                    destination = std::move(value);
                    return true;
                }
                case HORO_ASSET_IMPORT_SETTING_CHOICE:
                    if (source.choiceIndex > std::numeric_limits<std::size_t>::max())
                        return false;
                    destination = static_cast<std::size_t>(source.choiceIndex);
                    return true;
                default:
                    return false;
            }
        }

        [[nodiscard]] bool ConvertKind(const HoroAssetImportSettingKind source, Assets::ImportSettingKind &destination) {
            using enum Assets::ImportSettingKind;
            switch (source) {
                case HORO_ASSET_IMPORT_SETTING_BOOLEAN:
                    destination = Boolean;
                    return true;
                case HORO_ASSET_IMPORT_SETTING_INTEGER:
                    destination = Integer;
                    return true;
                case HORO_ASSET_IMPORT_SETTING_FLOAT:
                    destination = Float;
                    return true;
                case HORO_ASSET_IMPORT_SETTING_TEXT:
                    destination = Text;
                    return true;
                case HORO_ASSET_IMPORT_SETTING_CHOICE:
                    destination = Choice;
                    return true;
                default:
                    return false;
            }
        }

        [[nodiscard]] HoroAssetImportSettingValue ToAbiValue(const Assets::ImportSettingValue &value) {
            HoroAssetImportSettingValue result{};
            std::visit([&result]<typename T>(const T &typed) {
                if constexpr (std::is_same_v<T, bool>) {
                    result.kind = HORO_ASSET_IMPORT_SETTING_BOOLEAN;
                    result.booleanValue = typed ? 1U : 0U;
                } else if constexpr (std::is_same_v<T, std::int64_t>) {
                    result.kind = HORO_ASSET_IMPORT_SETTING_INTEGER;
                    result.integerValue = typed;
                } else if constexpr (std::is_same_v<T, double>) {
                    result.kind = HORO_ASSET_IMPORT_SETTING_FLOAT;
                    result.floatValue = typed;
                } else if constexpr (std::is_same_v<T, std::string>) {
                    result.kind = HORO_ASSET_IMPORT_SETTING_TEXT;
                    result.textValue = {typed.data(), static_cast<std::uint32_t>(typed.size())};
                } else {
                    result.kind = HORO_ASSET_IMPORT_SETTING_CHOICE;
                    result.choiceIndex = typed;
                }
            }, value);
            return result;
        }

        // The opaque pointer type is fixed by the versioned C ABI callback signature.
        [[nodiscard]] uint8_t IsCancelled(const void *context) {  // NOSONAR(cpp:S5008)
            if (context == nullptr) {
                return 0U;
            }
            return static_cast<const CancellationToken *>(context)->IsCancellationRequested() ? 1U : 0U;
        }

        [[nodiscard]] HoroExtensionStatus ResizeVector(void *context, const std::uint64_t byteCount,
                                                       std::uint8_t **outBytes) {  // NOSONAR(cpp:S5008)
            if (context == nullptr || outBytes == nullptr || byteCount > kMaxPayloadBytes)
                return HORO_EXTENSION_ERROR_OUTPUT_REJECTED;
            try {
                auto &bytes = *static_cast<std::vector<std::uint8_t> *>(context);
                bytes.resize(static_cast<std::size_t>(byteCount));
                *outBytes = bytes.empty() ? nullptr : bytes.data();
                return HORO_EXTENSION_SUCCESS;
            } catch (const std::bad_alloc &) {
                return HORO_EXTENSION_ERROR_OUTPUT_REJECTED;
            } catch (const std::length_error &exception) {
                LOG_WARN("extensions.importer", "ResizeVector rejected an oversized payload: %s", exception.what());
                return HORO_EXTENSION_ERROR_OUTPUT_REJECTED;
            } catch (const std::exception &exception) {  // NOSONAR(cpp:S1181) C ABI exception barrier.
                LOG_WARN("extensions.importer", "ResizeVector failed: %s", exception.what());
                return HORO_EXTENSION_ERROR_OUTPUT_REJECTED;
            }
        }

        struct ExternalImporterInstance final {
            ExternalImporterInstance() = default;

            ~ExternalImporterInstance() {
                if (destroy != nullptr)
                    destroy(context);
            }

            ExternalImporterInstance(const ExternalImporterInstance &) = delete;
            ExternalImporterInstance &operator=(const ExternalImporterInstance &) = delete;

            ExternalImporterInstance(ExternalImporterInstance &&) = delete;
            ExternalImporterInstance &operator=(ExternalImporterInstance &&) = delete;

            std::shared_ptr<ExtensionModuleLifetime> lifetime;
            void *context{};
            HoroAssetImporterDestroyFunc destroy{};
            HoroAssetImportFunc importFn{};
            HoroAssetPreviewFunc preview{};
        };

        template <typename Invoker> [[nodiscard]] HoroExtensionStatus SafeInvoke(Invoker &&invoker, const char *operation) {
            try {
                return invoker();
            } catch (const std::runtime_error &exception) {  // NOSONAR(cpp:S1181)
                LOG_WARN("extensions.importer", "External %s threw runtime error: %s", operation, exception.what());
            } catch (const std::logic_error &exception) {  // NOSONAR(cpp:S1181)
                LOG_WARN("extensions.importer", "External %s threw logic error: %s", operation, exception.what());
            } catch (const std::bad_alloc &exception) {  // NOSONAR(cpp:S1181)
                LOG_WARN("extensions.importer", "External %s threw bad alloc: %s", operation, exception.what());
            } catch (const std::exception &exception) {  // NOSONAR(cpp:S1181) External code is an exception containment boundary.
                LOG_WARN("extensions.importer", "External %s threw exception: %s", operation, exception.what());
            } catch (...) {  // NOSONAR(cpp:S1181)
                LOG_WARN("extensions.importer", "External %s threw unknown exception.", operation);
            }
            return HORO_EXTENSION_ERROR_INIT_FAILED;
        }

        class ExternalAssetImporter final : public Assets::IAssetImporter {
        public:
            explicit ExternalAssetImporter(std::shared_ptr<ExternalImporterInstance> instance) : instance_(std::move(instance)) {}

            [[nodiscard]] Result<Assets::PreparedAssetImport> Import(const Assets::AssetImportInput &input,
                                                                     const CancellationToken &cancellation) const override {
                std::vector<HoroAssetImportSettingValue> settings;
                settings.reserve(input.settings.size());
                for (const auto &setting : input.settings)
                    settings.push_back(ToAbiValue(setting));

                Assets::PreparedAssetImport prepared;
                HoroAssetImportRequest request{
                    .structSize = sizeof(HoroAssetImportRequest),
                    .sourceBytes = input.sourceBytes.data(),
                    .sourceByteCount = input.sourceBytes.size(),
                    .sourceExtension = {input.sourceExtension.data(), static_cast<std::uint32_t>(input.sourceExtension.size())},
                    .settings = settings.data(),
                    .settingCount = static_cast<std::uint32_t>(settings.size()),
                    .cancellation = {&cancellation, IsCancelled},
                };
                HoroAssetImportResponse response{
                    .structSize = sizeof(HoroAssetImportResponse),
                    .assetType = {},
                    .editorPayload = {&prepared.editorPayload, ResizeVector},
                };
                if (const HoroExtensionStatus status = SafeInvoke(
                        [&instance = instance_, &request, &response] {
                    return instance->importFn(instance->context, &request, &response);
                }, "importer");

                    status != HORO_EXTENSION_SUCCESS) {
                    return Result<Assets::PreparedAssetImport>::Failure(
                        MakeError(ExtensionErrors::InvocationFailed, "External asset importer callback failed."));
                }

                std::string assetType;
                if (!CopyText(response.assetType, assetType))
                    return Result<Assets::PreparedAssetImport>::Failure(
                        MakeError(ExtensionErrors::InvocationFailed, "External importer returned an invalid asset type."));
                auto parsedType = Assets::AssetTypeId::Parse(assetType);
                if (parsedType.HasError() || prepared.editorPayload.empty())
                    return Result<Assets::PreparedAssetImport>::Failure(
                        MakeError(ExtensionErrors::InvocationFailed, "External importer returned invalid or empty output."));
                prepared.type = std::move(parsedType).Value();
                return Result<Assets::PreparedAssetImport>::Success(std::move(prepared));
            }

        private:
            std::shared_ptr<ExternalImporterInstance> instance_;
        };

        class ExternalAssetPreviewProvider final : public Assets::IAssetPreviewProvider {
        public:
            explicit ExternalAssetPreviewProvider(std::shared_ptr<ExternalImporterInstance> instance) : instance_(std::move(instance)) {}

            [[nodiscard]] Result<Assets::AssetPreviewImage> GeneratePreview(const Assets::AssetPreviewInput &input,
                                                                            const CancellationToken &cancellation) const override {
                if (input.width == 0 || input.height == 0 || input.width > kMaxPreviewDimension || input.height > kMaxPreviewDimension)
                    return Result<Assets::AssetPreviewImage>::Failure(
                        MakeError(ExtensionErrors::InvocationFailed, "External preview dimensions are invalid."));

                Assets::AssetPreviewImage image;
                HoroAssetPreviewRequest request{
                    .structSize = sizeof(HoroAssetPreviewRequest),
                    .editorPayload = input.editorPayload.data(),
                    .editorPayloadByteCount = input.editorPayload.size(),
                    .absoluteAssetPath = {input.absoluteAssetPath.data(), static_cast<std::uint32_t>(input.absoluteAssetPath.size())},
                    .assetType = {input.assetType.Value().data(), static_cast<std::uint32_t>(input.assetType.Value().size())},
                    .width = input.width,
                    .height = input.height,
                    .cancellation = {&cancellation, IsCancelled},
                };
                HoroAssetPreviewResponse response{
                    .structSize = sizeof(HoroAssetPreviewResponse),
                    .rgba8Pixels = {&image.pixels, ResizeVector},
                };
                const HoroExtensionStatus status = SafeInvoke([&instance = instance_, &request, &response] {
                    return instance->preview(instance->context, &request, &response);
                }, "preview");

                image.width = response.width;
                image.height = response.height;
                if (status != HORO_EXTENSION_SUCCESS || !image.IsValid())
                    return Result<Assets::AssetPreviewImage>::Failure(
                        MakeError(ExtensionErrors::InvocationFailed, "External preview callback returned invalid output."));
                return Result<Assets::AssetPreviewImage>::Success(std::move(image));
            }

        private:
            std::shared_ptr<ExternalImporterInstance> instance_;
        };

        [[nodiscard]] bool ConvertSetting(const HoroAssetImportSettingDescriptor &source, Assets::ImportSettingDescriptor &output) {
            if (!CopyText(source.id, output.id) || !CopyText(source.labelKey, output.labelKey) ||
                !CopyText(source.descriptionKey, output.descriptionKey, true) || !ConvertKind(source.kind, output.kind) ||
                !ConvertValue(source.defaultValue, output.defaultValue) || source.choiceCount > kMaxListEntries ||
                (source.choices == nullptr && source.choiceCount != 0))
                return false;
            if (source.hasMinimum != 0)
                output.minimum = source.minimum;
            if (source.hasMaximum != 0)
                output.maximum = source.maximum;
            output.includeInPresets = source.includeInPresets != 0;
            output.choices.reserve(source.choiceCount);
            for (std::uint32_t index = 0; index < source.choiceCount; ++index) {
                Assets::ImportSettingChoice choice;
                if (!CopyText(source.choices[index].id, choice.id) || !CopyText(source.choices[index].labelKey, choice.labelKey) ||
                    !ConvertValue(source.choices[index].value, choice.value))
                    return false;
                output.choices.push_back(std::move(choice));
            }
            return true;
        }
    }  // namespace

    ExtensionModuleLifetime::~ExtensionModuleLifetime() {
        if (loaded && unload != nullptr) {
            try {
                unload(&moduleApi);
            } catch (const std::runtime_error &exception) {
                LOG_WARN("extensions.importer", "Runtime error during module unload: %s", exception.what());
            } catch (const std::logic_error &exception) {
                LOG_WARN("extensions.importer", "Logic error during module unload: %s", exception.what());
            } catch (const std::bad_alloc &exception) {
                LOG_WARN("extensions.importer", "Bad alloc during module unload: %s", exception.what());
            } catch (const std::exception &exception) {  // NOSONAR(cpp:S1181)
                LOG_WARN("extensions.importer", "Exception during module unload: %s", exception.what());
            } catch (...) {  // NOSONAR(cpp:S1181)
                LOG_WARN("extensions.importer", "Unknown exception during module unload.");
            }
        }
    }

    HoroExtensionStatus RegisterExternalAssetImporter(void *hostContext,  // NOSONAR(cpp:S5008)
                                                      const HoroAssetImporterDescriptor *descriptor) noexcept {
        auto *session = static_cast<AssetImporterRegistrationSession *>(hostContext);
        if (session == nullptr || descriptor == nullptr || session->failed ||
            descriptor->structSize < sizeof(HoroAssetImporterDescriptor) || descriptor->abiVersion != HORO_ASSET_IMPORTER_ABI_VERSION ||
            descriptor->importAsset == nullptr || descriptor->fileExtensionCount == 0 || descriptor->fileExtensionCount > kMaxListEntries ||
            descriptor->assetTypeCount == 0 || descriptor->assetTypeCount > kMaxListEntries || descriptor->settingCount > kMaxListEntries ||
            descriptor->fileExtensions == nullptr || descriptor->assetTypes == nullptr ||
            (descriptor->settings == nullptr && descriptor->settingCount != 0)) {
            if (session != nullptr) {
                session->failed = true;
                session->error =
                    MakeError(ExtensionErrors::ContributionRejected, "External importer descriptor is incomplete or ABI-incompatible.");
            }
            return HORO_EXTENSION_ERROR_INVALID_ARGS;
        }

        try {
            Assets::AssetImporterContribution contribution;
            if (!CopyText(descriptor->contributionId, contribution.contributionId) ||
                !CopyText(descriptor->contributionVersion, contribution.version) ||
                !CopyText(descriptor->subfolderCategory, contribution.subfolderCategory, true) ||
                !CopyText(descriptor->targetExtension, contribution.targetExtension))
                throw std::invalid_argument{"invalid text"};

            if (const bool declared = std::ranges::any_of(session->manifest->contributions,
                                                          [&contribution, session](const ExtensionContributionManifest &candidate) {
                return candidate.type == "asset.importer" && candidate.id == contribution.contributionId &&
                       candidate.owningModule == session->extensionModule->id;
            });
                !declared) {
                throw std::invalid_argument{"undeclared contribution"};
            }

            contribution.packageId = session->manifest->id;
            contribution.moduleId = session->extensionModule->id;
            contribution.moduleVersion = session->extensionModule->version;
            contribution.supportsMetaSidecar = descriptor->supportsMetaSidecar != 0;
            if (descriptor->previewFallback > static_cast<std::uint8_t>(Assets::AssetPreviewFallback::Generic))
                throw std::invalid_argument{"invalid fallback"};
            contribution.previewFallback = static_cast<Assets::AssetPreviewFallback>(descriptor->previewFallback);

            contribution.fileExtensions.reserve(descriptor->fileExtensionCount);
            for (std::uint32_t index = 0; index < descriptor->fileExtensionCount; ++index) {
                std::string extension;
                if (!CopyText(descriptor->fileExtensions[index], extension) || !IsLowerExtension(extension))
                    throw std::invalid_argument{"invalid file extension"};
                contribution.fileExtensions.push_back(std::move(extension));
            }

            contribution.assetTypes.reserve(descriptor->assetTypeCount);
            for (std::uint32_t index = 0; index < descriptor->assetTypeCount; ++index) {
                std::string assetType;
                if (!CopyText(descriptor->assetTypes[index], assetType))
                    throw std::invalid_argument{"invalid asset type"};
                auto parsed = Assets::AssetTypeId::Parse(assetType);
                if (parsed.HasError())
                    throw std::invalid_argument{"invalid type"};
                contribution.assetTypes.push_back(std::move(parsed).Value());
            }
            contribution.settings.resize(descriptor->settingCount);
            for (std::uint32_t index = 0; index < descriptor->settingCount; ++index) {
                if (!ConvertSetting(descriptor->settings[index], contribution.settings[index]))
                    throw std::invalid_argument{"invalid setting"};
            }

            auto instance = std::make_shared<ExternalImporterInstance>();
            instance->lifetime = session->lifetime;
            instance->context = descriptor->importerContext;
            instance->destroy = descriptor->destroyImporter;
            instance->importFn = descriptor->importAsset;
            instance->preview = descriptor->generatePreview;
            contribution.strategy = std::make_shared<ExternalAssetImporter>(instance);
            if (instance->preview != nullptr)
                contribution.previewProvider = std::make_shared<ExternalAssetPreviewProvider>(instance);
            session->contributions.push_back(std::move(contribution));
            return HORO_EXTENSION_SUCCESS;
        } catch (...) {
            session->failed = true;
            session->error =
                MakeError(ExtensionErrors::ContributionRejected, "External importer descriptor exceeded bounds or contained invalid data.");
            return HORO_EXTENSION_ERROR_INVALID_ARGS;
        }
    }
}  // namespace Horo::Extensions
