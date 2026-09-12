#pragma once

/**
 * @file GameAssetTypeRegistry.h
 * @brief Transactional game-owned asset registry, processing, and missing-code inspection.
 */

#include "Horo/Gameplay/GameAsset.h"

#include <optional>
#include <span>
#include <string>
#include <vector>

namespace Horo::Gameplay {
    /** @brief Compatibility state of authored asset bytes against the active gameplay module. */
    enum class GameAssetInspectionStatus : std::uint8_t {
        Current,
        MissingDescriptor,
        OlderSchema,
        NewerSchema,
    };

    /** @brief Immutable compatibility result that never changes the persistent payload. */
    struct GameAssetInspection {
        GameAssetInspectionStatus status{GameAssetInspectionStatus::MissingDescriptor}; /**< Compatibility classification. */
        std::optional<GameAssetTypeDescriptor> descriptor;                              /**< Owning descriptor snapshot, when present. */
    };

    /** @brief Semantic presentation state resolved to localized copy by the editor host. */
    enum class GameAssetEditorFallback : std::uint8_t {
        None,
        MissingDescriptor,
    };

    /** @brief Generic editor projection available even when gameplay code is missing. */
    struct GameAssetEditorModel {
        GameAssetTypeId typeId;
        std::uint32_t schemaVersion{};
        GameAssetInspectionStatus status{GameAssetInspectionStatus::MissingDescriptor};
        std::string displayName;
        std::string category;
        std::string iconName;
        std::vector<GameAssetEditorFieldDescriptor> fields;
        std::size_t payloadBytes{};
        bool readOnly{true};
        GameAssetEditorFallback fallback{GameAssetEditorFallback::MissingDescriptor}; /**< Host-localized fallback state. */
    };

    /** @brief Explicit host processing bounds for game-owned asset callbacks. */
    struct GameAssetProcessingLimits {
        std::size_t maximumInputBytes{MaximumGameAssetPayloadBytes}; /**< Maximum borrowed import or editor input. */
        std::size_t maximumCookedBytes{MaximumGameAssetCookedBytes}; /**< Maximum callback-produced cook payload. */
    };

    /** @brief Host-owned game asset registry frozen before module startup or asset processing. */
    class GameAssetTypeRegistry final {
    public:
        /**
         * @brief Creates an open registry restricted to one project module namespace.
         * @param moduleId Stable project module namespace owning every registered type.
         * @param limits Host-enforced processing input and output bounds.
         */
        explicit GameAssetTypeRegistry(std::string moduleId, GameAssetProcessingLimits limits = {});

        /**
         * @brief Copies one asset descriptor and its exact-generation callbacks into the transaction.
         * @param registration Complete metadata and processing binding.
         * @return Success or a typed validation, duplicate, or lifecycle error.
         */
        [[nodiscard]] Result<void> Register(GameAssetTypeRegistration registration);
        /** @brief Sorts the complete descriptor snapshot and prevents further registration. */
        [[nodiscard]] Result<void> Freeze();
        /** @brief Reports whether registration is closed. */
        [[nodiscard]] bool IsFrozen() const noexcept;
        /** @brief Returns registrations in deterministic type-ID order after freeze. */
        [[nodiscard]] std::span<const GameAssetTypeRegistration> Registrations() const noexcept;
        /** @brief Finds one registration by stable identity. */
        [[nodiscard]] const GameAssetTypeRegistration *Find(const GameAssetTypeId &typeId) const noexcept;

        /**
         * @brief Inspects compatibility without invoking project code or changing opaque bytes.
         * @param asset Persistent authored asset.
         * @return Current, missing, older, or newer schema status.
         */
        [[nodiscard]] Result<GameAssetInspection> Inspect(const SerializedGameAsset &asset) const;
        /**
         * @brief Builds a rendering-neutral editor model with a read-only missing-code fallback.
         * @param asset Persistent authored asset.
         * @return Editor metadata that retains identity, schema, and payload-size evidence.
         */
        [[nodiscard]] Result<GameAssetEditorModel> DescribeForEditor(const SerializedGameAsset &asset) const;

        /**
         * @brief Invokes the registered importer and validates its persistent output.
         * @param typeId Exact registered type selected by host policy.
         * @param input Bounded borrowed source bytes and declared extension.
         * @param cancellation Generation-scoped cancellation token.
         * @return Current persistent envelope or a typed processing error.
         */
        [[nodiscard]] Result<SerializedGameAsset> Import(const GameAssetTypeId &typeId, const GameAssetImportInput &input,
                                                         const CancellationToken &cancellation) const;
        /**
         * @brief Invokes the registered serializer and validates its persistent output.
         * @param typeId Exact registered type being edited.
         * @param input Bounded borrowed generic editor payload.
         * @param cancellation Generation-scoped cancellation token.
         * @return Current persistent envelope or a typed processing error.
         */
        [[nodiscard]] Result<SerializedGameAsset> Serialize(const GameAssetTypeId &typeId, const GameAssetSerializationInput &input,
                                                            const CancellationToken &cancellation) const;
        /**
         * @brief Invokes the registered cooker for a current authored payload and declared target.
         * @param input Authored envelope and canonical cook target.
         * @param cancellation Generation-scoped cancellation token.
         * @return Bounded cooked bytes or a typed processing error.
         */
        [[nodiscard]] Result<std::vector<std::byte>> Cook(const GameAssetCookInput &input, const CancellationToken &cancellation) const;

    private:
        /** @brief Resolves a frozen processing registration or returns the typed lifecycle/availability error. */
        [[nodiscard]] Result<const GameAssetTypeRegistration *> GetRegistrationForProcessing(const GameAssetTypeId &typeId) const;

        std::string moduleId_;
        std::vector<GameAssetTypeRegistration> registrations_;
        GameAssetProcessingLimits limits_;
        bool frozen_{false};
    };
}  // namespace Horo::Gameplay
