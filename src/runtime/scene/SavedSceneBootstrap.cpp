#include "Horo/Runtime/Scene/SavedSceneBootstrap.h"

#include "Horo/Runtime/Scene/RuntimeScene.h"
#include "RuntimeSceneErrors.h"

#include <algorithm>
#include <utility>

namespace Horo::Runtime {
    namespace {
        template <typename T> [[nodiscard]] Result<T> Failure(const ErrorCodeDescriptor &descriptor) {
            return Result<T>::Failure(MakeError(descriptor));
        }

        [[nodiscard]] bool HasDigestEvidence(const Sha256Digest &digest) noexcept {
            return std::ranges::any_of(digest.bytes, [](const std::uint8_t byte) {
                return byte != 0;
            });
        }
    }  // namespace

    /** @copydoc SavedSceneTransitionMetadata::IsValid */
    bool SavedSceneTransitionMetadata::IsValid() const noexcept {
        return slot.IsValid() && generation.IsValid() && (!priorWorld || priorWorld->IsValid());
    }

    /** @copydoc SavedSceneBootstrapDescriptor::IsValid */
    bool SavedSceneBootstrapDescriptor::IsValid() const noexcept {
        return world.IsValid() && baseScene.IsValid() && !expectedAssetType.Value().empty() && definition.IsValid() &&
               revision.value != 0 && HasDigestEvidence(contentDigest) && (!spawnAnchor || spawnAnchor->IsValid()) && transition.IsValid();
    }

    /** @copydoc PreparedSavedSceneBootstrap::PreparedSavedSceneBootstrap */
    PreparedSavedSceneBootstrap::PreparedSavedSceneBootstrap(SavedSceneBootstrapDescriptor descriptor, const Assets::AssetId baseSceneAsset,
                                                             const Assets::AssetRegistryRevision registryRevision,
                                                             RuntimeSceneDefinition definition) noexcept
        : descriptor_(std::move(descriptor)), baseSceneAsset_(baseSceneAsset), registryRevision_(registryRevision),
          definition_(std::move(definition)) {}

    /** @copydoc PreparedSavedSceneBootstrap::PreparedSavedSceneBootstrap */
    PreparedSavedSceneBootstrap::PreparedSavedSceneBootstrap(PreparedSavedSceneBootstrap &&other) noexcept
        : descriptor_(std::move(other.descriptor_)), baseSceneAsset_(other.baseSceneAsset_), registryRevision_(other.registryRevision_),
          definition_(std::move(other.definition_)), consumed_(other.consumed_) {
        other.consumed_ = true;
    }

    /** @copydoc PreparedSavedSceneBootstrap::operator= */
    PreparedSavedSceneBootstrap &PreparedSavedSceneBootstrap::operator=(PreparedSavedSceneBootstrap &&other) noexcept {
        if (this == &other)
            return *this;
        descriptor_ = std::move(other.descriptor_);
        baseSceneAsset_ = other.baseSceneAsset_;
        registryRevision_ = other.registryRevision_;
        definition_ = std::move(other.definition_);
        consumed_ = other.consumed_;
        other.consumed_ = true;
        return *this;
    }

    /** @copydoc PreparedSavedSceneBootstrap::Descriptor */
    const SavedSceneBootstrapDescriptor &PreparedSavedSceneBootstrap::Descriptor() const noexcept {
        return descriptor_;
    }

    /** @copydoc PreparedSavedSceneBootstrap::BaseSceneAsset */
    Assets::AssetId PreparedSavedSceneBootstrap::BaseSceneAsset() const noexcept {
        return baseSceneAsset_;
    }

    /** @copydoc PreparedSavedSceneBootstrap::RegistryRevision */
    Assets::AssetRegistryRevision PreparedSavedSceneBootstrap::RegistryRevision() const noexcept {
        return registryRevision_;
    }

    /** @copydoc PreparedSavedSceneBootstrap::Definition */
    const RuntimeSceneDefinition &PreparedSavedSceneBootstrap::Definition() const noexcept {
        return definition_;
    }

    /** @copydoc PreparedSavedSceneBootstrap::Queue */
    Result<void> PreparedSavedSceneBootstrap::Queue(RuntimeSceneService &service) && {
        if (consumed_)
            return Failure<void>(SceneErrors::SaveBootstrapInvalid);
        consumed_ = true;
        return service.QueuePreparation(std::move(definition_));
    }

    /** @copydoc PrepareSavedSceneBootstrap */
    Result<PreparedSavedSceneBootstrap> PrepareSavedSceneBootstrap(SavedSceneBootstrapDescriptor descriptor,
                                                                   const Assets::AssetTypeId &requiredSceneAssetType,
                                                                   const Assets::AssetRegistrySnapshot &registry,
                                                                   RuntimeSceneDefinition definition,
                                                                   const Sha256Digest &actualContentDigest) {
        if (!descriptor.IsValid() || requiredSceneAssetType.Value().empty() || registry.Revision().value == 0)
            return Failure<PreparedSavedSceneBootstrap>(SceneErrors::SaveBootstrapInvalid);

        const Assets::AssetId baseSceneAsset = Assets::AssetId::FromBytes(descriptor.baseScene.Bytes());
        const Assets::AssetRecord *record = registry.Find(baseSceneAsset);
        if (!record)
            return Failure<PreparedSavedSceneBootstrap>(SceneErrors::SaveBootstrapAssetUnavailable);
        if (descriptor.expectedAssetType != requiredSceneAssetType || record->type != requiredSceneAssetType ||
            definition.Id() != descriptor.definition || definition.Revision() != descriptor.revision ||
            actualContentDigest != descriptor.contentDigest)
            return Failure<PreparedSavedSceneBootstrap>(SceneErrors::SaveBootstrapIncompatible);

        if (descriptor.spawnAnchor) {
            const auto found = std::ranges::find(definition.Entities(), *descriptor.spawnAnchor, [](const RuntimeEntityDefinition &entity) {
                return entity.object;
            });
            if (found == definition.Entities().end())
                return Failure<PreparedSavedSceneBootstrap>(SceneErrors::SaveBootstrapSpawnMissing);
        }

        return Result<PreparedSavedSceneBootstrap>::Success(
            PreparedSavedSceneBootstrap{std::move(descriptor), baseSceneAsset, registry.Revision(), std::move(definition)});
    }
}  // namespace Horo::Runtime
