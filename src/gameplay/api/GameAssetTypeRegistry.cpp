#include "Horo/Gameplay/GameAssetTypeRegistry.h"

#include "GameAssetValidation.h"
#include "Horo/Gameplay/GameplayErrors.h"

#include <algorithm>
#include <utility>

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

        template <typename Output, typename Action>
        [[nodiscard]] Result<Output> UseRegistration(Result<const GameAssetTypeRegistration *> resolved, Action &&action) {
            if (resolved.HasError())
                return Result<Output>::Failure(resolved.ErrorValue());
            return std::forward<Action>(action)(*resolved.Value());
        }

        [[nodiscard]] bool HandlesExtension(const GameAssetTypeDescriptor &descriptor, const std::string_view extension) noexcept {
            return std::ranges::find(descriptor.sourceExtensions, extension) != descriptor.sourceExtensions.end();
        }

        [[nodiscard]] bool HandlesTarget(const GameAssetTypeDescriptor &descriptor, const AssetCookTargetId &target) noexcept {
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
    GameAssetTypeRegistry::GameAssetTypeRegistry(std::string moduleId, const GameAssetProcessingLimits limits)
        : moduleId_(std::move(moduleId)), limits_{.maximumInputBytes = std::min(limits.maximumInputBytes, MaximumGameAssetPayloadBytes),
                                                  .maximumCookedBytes = std::min(limits.maximumCookedBytes, MaximumGameAssetCookedBytes)} {}

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
        const auto found = std::ranges::find(registrations_, typeId, [](const GameAssetTypeRegistration &registration) {
            return registration.descriptor.typeId;
        });
        return found == registrations_.end() ? nullptr : std::to_address(found);
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
        return Result<GameAssetInspection>::Success({.status = status, .descriptor = descriptor});
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
            .iconName = "asset-unknown",
            .payloadBytes = asset.payload.size(),
            .readOnly = true,
        };
        if (inspection.descriptor.has_value()) {
            model.displayName = inspection.descriptor->editor.displayName;
            model.category = inspection.descriptor->editor.category;
            model.iconName = inspection.descriptor->editor.iconName;
            model.fields = inspection.descriptor->editor.fields;
            model.readOnly = inspection.status != GameAssetInspectionStatus::Current;
            model.fallback = GameAssetEditorFallback::None;
        }
        return Result<GameAssetEditorModel>::Success(std::move(model));
    }

    /** @copydoc GameAssetTypeRegistry::GetRegistrationForProcessing */
    Result<const GameAssetTypeRegistration *> GameAssetTypeRegistry::GetRegistrationForProcessing(const GameAssetTypeId &typeId) const {
        if (!frozen_)
            return Result<const GameAssetTypeRegistration *>::Failure(
                MakeError(GameplayErrors::InvalidGameAssetProcessingInput, "Freeze the gameplay asset registry before processing."));
        const GameAssetTypeRegistration *registration = Find(typeId);
        if (registration == nullptr)
            return Result<const GameAssetTypeRegistration *>::Failure(MakeError(GameplayErrors::GameAssetHandlerUnavailable));
        return Result<const GameAssetTypeRegistration *>::Success(registration);
    }

    /** @copydoc GameAssetTypeRegistry::Import */
    Result<SerializedGameAsset> GameAssetTypeRegistry::Import(const GameAssetTypeId &typeId, const GameAssetImportInput &input,
                                                              const CancellationToken &cancellation) const {
        return UseRegistration<SerializedGameAsset>(GetRegistrationForProcessing(typeId), [&](const auto &registration) {
            if (!HandlesExtension(registration.descriptor, input.sourceExtension) || input.sourceBytes.size() > limits_.maximumInputBytes)
                return Result<SerializedGameAsset>::Failure(MakeError(GameplayErrors::InvalidGameAssetProcessingInput));
            return ValidateProcessedAsset(Invoke<decltype(registration.handler.importAsset), GameAssetImportInput,
                                                 SerializedGameAsset>(registration.handler.importAsset, registration.handler.userData,
                                                                      input, cancellation,
                                                                      "Gameplay asset import callback threw an exception."),
                                          registration.descriptor);
        });
    }

    /** @copydoc GameAssetTypeRegistry::Serialize */
    Result<SerializedGameAsset> GameAssetTypeRegistry::Serialize(const GameAssetTypeId &typeId, const GameAssetSerializationInput &input,
                                                                 const CancellationToken &cancellation) const {
        return UseRegistration<SerializedGameAsset>(GetRegistrationForProcessing(typeId), [&](const auto &registration) {
            if (input.editorPayload.size() > limits_.maximumInputBytes ||
                (input.encoding != GameAssetPayloadEncoding::CanonicalJson && input.encoding != GameAssetPayloadEncoding::Binary))
                return Result<SerializedGameAsset>::Failure(MakeError(GameplayErrors::InvalidGameAssetProcessingInput));
            return ValidateProcessedAsset(Invoke<decltype(registration.handler.serializeAsset), GameAssetSerializationInput,
                                                 SerializedGameAsset>(registration.handler.serializeAsset, registration.handler.userData,
                                                                      input, cancellation,
                                                                      "Gameplay asset serialization callback threw an exception."),
                                          registration.descriptor);
        });
    }

    /** @copydoc GameAssetTypeRegistry::Cook */
    Result<std::vector<std::byte>> GameAssetTypeRegistry::Cook(const GameAssetCookInput &input,
                                                               const CancellationToken &cancellation) const {
        if (const Result<void> valid = ValidateSerializedGameAsset(input.asset); valid.HasError())
            return Result<std::vector<std::byte>>::Failure(valid.ErrorValue());
        const auto resolved = GetRegistrationForProcessing(input.asset.typeId);
        if (resolved.HasError())
            return Result<std::vector<std::byte>>::Failure(resolved.ErrorValue());
        const GameAssetTypeRegistration *registration = resolved.Value();
        if (input.asset.schemaVersion != registration->descriptor.schemaVersion || !HandlesTarget(registration->descriptor, input.target))
            return Result<std::vector<std::byte>>::Failure(MakeError(GameplayErrors::InvalidGameAssetProcessingInput));
        auto cooked = Invoke<decltype(registration->handler.cookAsset), GameAssetCookInput,
                             std::vector<std::byte>>(registration->handler.cookAsset, registration->handler.userData, input, cancellation,
                                                     "Gameplay asset cook callback threw an exception.");
        if (cooked.HasValue() && cooked.Value().size() > limits_.maximumCookedBytes)
            return Result<std::vector<std::byte>>::Failure(MakeError(GameplayErrors::GameAssetProcessingFailed));
        return cooked;
    }
}  // namespace Horo::Gameplay
