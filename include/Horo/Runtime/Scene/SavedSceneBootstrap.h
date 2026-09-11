#pragma once

/**
 * @file SavedSceneBootstrap.h
 * @brief Path-independent saved-scene baseline admission and preparation.
 */

#include "Horo/Assets/AssetRegistry.h"
#include "Horo/Foundation/Sha256.h"
#include "Horo/Runtime/Save/SaveArchiveMetadata.h"
#include "Horo/Runtime/Scene/RuntimeSceneDefinition.h"

#include <optional>

namespace Horo::Runtime {
    class RuntimeSceneService;

    /** @brief Durable provenance for the save generation that requested a scene transition. */
    struct SavedSceneTransitionMetadata final {
        SaveGameSlotId slot;                   /**< Logical slot selected by the restore owner. */
        SlotGenerationId generation;           /**< Exact immutable publication being restored. */
        std::optional<SaveWorldId> priorWorld; /**< Previous logical world, when this crosses worlds. */

        /** @brief Checks required identities and optional prior-world evidence. @return Whether the metadata is valid. */
        [[nodiscard]] bool IsValid() const noexcept;
        [[nodiscard]] auto operator<=>(const SavedSceneTransitionMetadata &) const noexcept = default;
    };

    /** @brief Exact durable requirements for recreating a saved scene from cooked content. */
    struct SavedSceneBootstrapDescriptor final {
        SaveWorldId world;                        /**< Logical world being reconstructed. */
        SaveBaseSceneId baseScene;                /**< Path-independent cooked scene asset identity. */
        Assets::AssetTypeId expectedAssetType;    /**< Persisted type evidence; host policy remains authoritative. */
        SceneDefinitionId definition;             /**< Stable logical definition identity. */
        SceneDefinitionRevision revision;         /**< Exact compatible authored revision. */
        Sha256Digest contentDigest;               /**< Exact compatible cooked-content digest. */
        std::optional<SceneObjectId> spawnAnchor; /**< Stable authored spawn location, when required. */
        SavedSceneTransitionMetadata transition;  /**< Restore operation provenance. */

        /** @brief Checks complete typed identity, revision, digest, spawn, and transition evidence. @return Validity. */
        [[nodiscard]] bool IsValid() const noexcept;
        [[nodiscard]] auto operator<=>(const SavedSceneBootstrapDescriptor &) const noexcept = default;
    };

    /** @brief Owned preflight proof whose definition can enter the existing scene preparation path. */
    class PreparedSavedSceneBootstrap final {
    public:
        PreparedSavedSceneBootstrap(const PreparedSavedSceneBootstrap &) = delete;
        PreparedSavedSceneBootstrap &operator=(const PreparedSavedSceneBootstrap &) = delete;
        PreparedSavedSceneBootstrap(PreparedSavedSceneBootstrap &&) noexcept = default;
        PreparedSavedSceneBootstrap &operator=(PreparedSavedSceneBootstrap &&) noexcept = default;

        /** @brief Returns the validated durable requirements. @return Borrow valid for this prepared value's lifetime. */
        [[nodiscard]] const SavedSceneBootstrapDescriptor &Descriptor() const noexcept;
        /** @brief Returns the logical cooked asset resolved from the saved identity. @return Stable base scene AssetId. */
        [[nodiscard]] Assets::AssetId BaseSceneAsset() const noexcept;
        /** @brief Returns the exact asset-registry revision used for preflight. @return Non-zero pinned revision evidence. */
        [[nodiscard]] Assets::AssetRegistryRevision RegistryRevision() const noexcept;
        /** @brief Returns the owned immutable default scene to receive later saved overrides. @return Borrowed definition. */
        [[nodiscard]] const RuntimeSceneDefinition &Definition() const noexcept;
        /** @brief Queues the validated default scene through the normal lifecycle admission path. @param service Scene owner.
         * @return Queue admission result; active scene publication still occurs only at its lifecycle commit boundary. */
        [[nodiscard]] Result<void> Queue(RuntimeSceneService &service) const;

    private:
        friend Result<PreparedSavedSceneBootstrap> PrepareSavedSceneBootstrap(SavedSceneBootstrapDescriptor, const Assets::AssetTypeId &,
                                                                              const Assets::AssetRegistrySnapshot &, RuntimeSceneDefinition,
                                                                              const Sha256Digest &);

        /** @brief Owns validated preflight evidence and decoded authored defaults without retaining registry borrows. */
        PreparedSavedSceneBootstrap(SavedSceneBootstrapDescriptor descriptor, Assets::AssetId baseSceneAsset,
                                    Assets::AssetRegistryRevision registryRevision, RuntimeSceneDefinition definition) noexcept;

        SavedSceneBootstrapDescriptor descriptor_;
        Assets::AssetId baseSceneAsset_;
        Assets::AssetRegistryRevision registryRevision_;
        RuntimeSceneDefinition definition_;
    };

    /**
     * @brief Preflights one cooked scene baseline without mutating active or pending runtime-scene state.
     * @details Resolution uses only the saved logical identity and immutable registry snapshot. Authoring paths are never
     *          accepted. Exact definition, revision, digest, type, and spawn mismatches fail before Queue can be called.
     * @param descriptor Owned durable bootstrap requirements.
     * @param requiredSceneAssetType Trusted host policy for the cooked scene artifact type. Persisted type evidence
     *                               cannot weaken this requirement.
     * @param registry Immutable asset registry snapshot used only during this call.
     * @param definition Owned decoded default scene from the cooked artifact.
     * @param actualContentDigest Digest of the exact cooked bytes that produced @p definition.
     * @return Owned preparation proof, or a typed invalid/unavailable/incompatible/spawn error.
     */
    [[nodiscard]] Result<PreparedSavedSceneBootstrap> PrepareSavedSceneBootstrap(SavedSceneBootstrapDescriptor descriptor,
                                                                                 const Assets::AssetTypeId &requiredSceneAssetType,
                                                                                 const Assets::AssetRegistrySnapshot &registry,
                                                                                 RuntimeSceneDefinition definition,
                                                                                 const Sha256Digest &actualContentDigest);
}  // namespace Horo::Runtime
