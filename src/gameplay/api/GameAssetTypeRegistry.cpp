#include "Horo/Gameplay/GameAssetTypeRegistry.h"

#include "GameAssetValidation.h"
#include "Horo/Gameplay/GameplayErrors.h"

#include <algorithm>

namespace Horo::Gameplay {
    namespace {
        template <typename Callback, typename Input, typename Output>
        [[nodiscard]] Result<Output> Invoke(Callback callback, void *userData, const Input &input, const CancellationToken &cancellation,
                                            const char *failureMessage) noexcept {
            try {
                return callback(userData, input, cancellation);
            } catch (...) {
                return Result<Output>::Failure(MakeError(GameplayErrors::GameAssetProcessingFailed, failureMessage));
            }
        }

        [[nodiscard]] bool HandlesExtension(const GameAssetTypeDescriptor &descriptor, const std::string_view extension) noexcept {
            return std::ranges::find(descriptor.sourceExtensions, extension) != descriptor.sourceExtensions.end();
        }

        [[nodiscard]] bool HandlesTarget(const GameAssetTypeDescriptor &descriptor, const std::string_view target) noexcept {
            return std::ranges::find(descriptor.cookTargets, target) != descriptor.cookTargets.end();
        }

        [[nodiscard]] Result<SerializedGameAsset> ValidateProcessedAsset(Result<SerializedGameAsset> processed,
                                                                         const GameAssetTypeDescriptor &descriptor) {
            if (processed.HasError())
                return Result<SerializedGameAsset>::Failure(processed.ErrorValue());
            SerializedGameAsset asset = std::move(processed).Value();
            if (const Result<void> valid = ValidateSerializedGameAsset(asset); valid.HasError())
                return Result<SerializedGameAsset>::Failure(valid.ErrorValue());
            if (asset.typeId != descriptor.typeId || asset.schemaVersion != descriptor.schemaVersion)
                return Result<SerializedGameAsset>::Failure(
                    MakeError(GameplayErrors::GameAssetProcessingFailed, "Gameplay asset callback returned the wrong identity or schema."));
            return Result<SerializedGameAsset>::Success(std::move(asset));
        }
    }  // namespace

    /** @copydoc GameAssetTypeRegistry::GameAssetTypeRegistry */
    GameAssetTypeRegistry::GameAssetTypeRegistry(std::string moduleId) : moduleId_(std::move(moduleId)) {}

    /** @copydoc GameAssetTypeRegistry::Register */
    Result<void> GameAssetTypeRegistry::Register(GameAssetTypeRegistration registration) {
        if (frozen_)
            return Result<void>::Failure(MakeError(GameplayErrors::GameAssetRegistryFrozen));
        if (registrations_.size() >= MaximumGameAssetTypes)
            return Result<void>::Failure(MakeError(GameplayErrors::InvalidGameAssetDescriptor));
        if (const Result<void> valid = Detail::ValidateGameAssetRegistration(registration, moduleId_); valid.HasError())
            return valid;
        if (Find(registration.descriptor.typeId) != nullptr)
            return Result<void>::Failure(MakeError(GameplayErrors::DuplicateGameAssetType));
        registrations_.push_back(std::move(registration));
        return Result<void>::Success();
    }

    /** @copydoc GameAssetTypeRegistry::Freeze */
    Result<void> GameAssetTypeRegistry::Freeze() {
        if (frozen_)
            return Result<void>::Success();
        std::ranges::sort(registrations_, {}, [](const GameAssetTypeRegistration &registration) {
            return registration.descriptor.typeId.Value();
        });
        frozen_ = true;
        return Result<void>::Success();
    }

    /** @copydoc GameAssetTypeRegistry::IsFrozen */
    bool GameAssetTypeRegistry::IsFrozen() const noexcept {
        return frozen_;
    }

    /** @copydoc GameAssetTypeRegistry::Registrations */
    std::span<const GameAssetTypeRegistration> GameAssetTypeRegistry::Registrations() const noexcept {
        return registrations_;
    }

    /** @copydoc GameAssetTypeRegistry::Find */
    const GameAssetTypeRegistration *GameAssetTypeRegistry::Find(const GameAssetTypeId &typeId) const noexcept {
        if (!frozen_) {
            const auto found = std::ranges::find(registrations_, typeId, [](const GameAssetTypeRegistration &registration) {
                return registration.descriptor.typeId;
            });
            return found == registrations_.end() ? nullptr : std::to_address(found);
        }
        const auto found = std::ranges::lower_bound(registrations_, typeId, {}, [](const GameAssetTypeRegistration &registration) {
            return registration.descriptor.typeId;
        });
        if (found == registrations_.end() || found->descriptor.typeId != typeId)
            return nullptr;
        return std::to_address(found);
    }

    /** @copydoc GameAssetTypeRegistry::Inspect */
    Result<GameAssetInspection> GameAssetTypeRegistry::Inspect(const SerializedGameAsset &asset) const {
        if (const Result<void> valid = ValidateSerializedGameAsset(asset); valid.HasError())
            return Result<GameAssetInspection>::Failure(valid.ErrorValue());
        const GameAssetTypeRegistration *registration = Find(asset.typeId);
        if (registration == nullptr)
            return Result<GameAssetInspection>::Success({.status = GameAssetInspectionStatus::MissingDescriptor});
        const GameAssetTypeDescriptor &descriptor = registration->descriptor;
        GameAssetInspectionStatus status = GameAssetInspectionStatus::Current;
        if (asset.schemaVersion < descriptor.schemaVersion)
            status = GameAssetInspectionStatus::OlderSchema;
        else if (asset.schemaVersion > descriptor.schemaVersion)
            status = GameAssetInspectionStatus::NewerSchema;
        return Result<GameAssetInspection>::Success({.status = status, .descriptor = &descriptor});
    }

    /** @copydoc GameAssetTypeRegistry::DescribeForEditor */
    Result<GameAssetEditorModel> GameAssetTypeRegistry::DescribeForEditor(const SerializedGameAsset &asset) const {
        auto inspected = Inspect(asset);
        if (inspected.HasError())
            return Result<GameAssetEditorModel>::Failure(inspected.ErrorValue());
        const GameAssetInspection inspection = inspected.Value();
        GameAssetEditorModel model{
            .typeId = asset.typeId,
            .schemaVersion = asset.schemaVersion,
            .status = inspection.status,
            .displayName = asset.typeId.Value(),
            .category = "Missing Gameplay Asset Type",
            .iconName = "asset-unknown",
            .payloadBytes = asset.payload.size(),
            .readOnly = true,
        };
        if (inspection.descriptor != nullptr) {
            model.displayName = inspection.descriptor->editor.displayName;
            model.category = inspection.descriptor->editor.category;
            model.iconName = inspection.descriptor->editor.iconName;
            model.fields = inspection.descriptor->editor.fields;
            model.readOnly = inspection.status != GameAssetInspectionStatus::Current;
        }
        return Result<GameAssetEditorModel>::Success(std::move(model));
    }

    /** @copydoc GameAssetTypeRegistry::Import */
    Result<SerializedGameAsset> GameAssetTypeRegistry::Import(const GameAssetTypeId &typeId, const GameAssetImportInput &input,
                                                              const CancellationToken &cancellation) const {
        if (!frozen_)
            return Result<SerializedGameAsset>::Failure(
                MakeError(GameplayErrors::InvalidGameAssetProcessingInput, "Freeze the gameplay asset registry before processing."));
        const GameAssetTypeRegistration *registration = Find(typeId);
        if (registration == nullptr)
            return Result<SerializedGameAsset>::Failure(MakeError(GameplayErrors::GameAssetHandlerUnavailable));
        if (!HandlesExtension(registration->descriptor, input.sourceExtension) || input.sourceBytes.size() > MaximumGameAssetPayloadBytes)
            return Result<SerializedGameAsset>::Failure(MakeError(GameplayErrors::InvalidGameAssetProcessingInput));
        return ValidateProcessedAsset(Invoke<decltype(registration->handler.importAsset), GameAssetImportInput,
                                             SerializedGameAsset>(registration->handler.importAsset, registration->handler.userData, input,
                                                                  cancellation, "Gameplay asset import callback threw an exception."),
                                      registration->descriptor);
    }

    /** @copydoc GameAssetTypeRegistry::Serialize */
    Result<SerializedGameAsset> GameAssetTypeRegistry::Serialize(const GameAssetTypeId &typeId, const GameAssetSerializationInput &input,
                                                                 const CancellationToken &cancellation) const {
        if (!frozen_)
            return Result<SerializedGameAsset>::Failure(
                MakeError(GameplayErrors::InvalidGameAssetProcessingInput, "Freeze the gameplay asset registry before processing."));
        const GameAssetTypeRegistration *registration = Find(typeId);
        if (registration == nullptr)
            return Result<SerializedGameAsset>::Failure(MakeError(GameplayErrors::GameAssetHandlerUnavailable));
        if (input.editorPayload.size() > MaximumGameAssetPayloadBytes ||
            (input.encoding != GameAssetPayloadEncoding::CanonicalJson && input.encoding != GameAssetPayloadEncoding::Binary))
            return Result<SerializedGameAsset>::Failure(MakeError(GameplayErrors::InvalidGameAssetProcessingInput));
        return ValidateProcessedAsset(Invoke<decltype(registration->handler.serializeAsset), GameAssetSerializationInput,
                                             SerializedGameAsset>(registration->handler.serializeAsset, registration->handler.userData,
                                                                  input, cancellation,
                                                                  "Gameplay asset serialization callback threw an exception."),
                                      registration->descriptor);
    }

    /** @copydoc GameAssetTypeRegistry::Cook */
    Result<std::vector<std::byte>> GameAssetTypeRegistry::Cook(const GameAssetCookInput &input,
                                                               const CancellationToken &cancellation) const {
        if (!frozen_)
            return Result<std::vector<std::byte>>::Failure(
                MakeError(GameplayErrors::InvalidGameAssetProcessingInput, "Freeze the gameplay asset registry before processing."));
        if (const Result<void> valid = ValidateSerializedGameAsset(input.asset); valid.HasError())
            return Result<std::vector<std::byte>>::Failure(valid.ErrorValue());
        const GameAssetTypeRegistration *registration = Find(input.asset.typeId);
        if (registration == nullptr)
            return Result<std::vector<std::byte>>::Failure(MakeError(GameplayErrors::GameAssetHandlerUnavailable));
        if (input.asset.schemaVersion != registration->descriptor.schemaVersion || !HandlesTarget(registration->descriptor, input.target))
            return Result<std::vector<std::byte>>::Failure(MakeError(GameplayErrors::InvalidGameAssetProcessingInput));
        auto cooked = Invoke<decltype(registration->handler.cookAsset), GameAssetCookInput,
                             std::vector<std::byte>>(registration->handler.cookAsset, registration->handler.userData, input, cancellation,
                                                     "Gameplay asset cook callback threw an exception.");
        if (cooked.HasValue() && cooked.Value().size() > MaximumGameAssetCookedBytes)
            return Result<std::vector<std::byte>>::Failure(MakeError(GameplayErrors::GameAssetProcessingFailed));
        return cooked;
    }
}  // namespace Horo::Gameplay
