#pragma once

/**
 * @file SaveReference.h
 * @brief Stable path-independent durable save references and flat reconciliation results.
 */

#include "Horo/Runtime/Save/SaveArchiveMetadata.h"
#include "Horo/Runtime/Save/SaveCanonicalCodec.h"

#include <cstdint>
#include <optional>
#include <variant>

namespace Horo::Runtime {
    struct SaveAssetIdentityTag;
    struct PersistentEntityIdentityTag;
    struct PrefabInstanceIdentityTag;

    /** @brief Stable Asset Registry identity persisted without a filesystem path or runtime handle. */
    using SaveAssetId = PersistentSaveIdentity<SaveAssetIdentityTag>;
    /** @brief Stable runtime entity identity persisted without an ECS slot or generation. */
    using PersistentEntityId = PersistentSaveIdentity<PersistentEntityIdentityTag>;
    /** @brief Stable identity of one durable prefab occurrence. */
    using SavePrefabInstanceId = PersistentSaveIdentity<PrefabInstanceIdentityTag>;

    /** @brief Stable asset target. */
    struct SaveAssetReference final {
        SaveAssetId asset;
    };

    /** @brief Stable authored scene target inside a logical world. */
    struct SaveSceneReference final {
        SaveWorldId world;
        SaveBaseSceneId scene;
    };

    /** @brief Stable entity target inside a logical world. */
    struct SaveEntityReference final {
        SaveWorldId world;
        PersistentEntityId entity;
    };

    /** @brief Stable provenance for an entity instantiated from a prefab asset. */
    struct SavePrefabProvenance final {
        SaveAssetId prefab;
        SavePrefabInstanceId instance;
    };

    /** @brief Stable participant target. */
    struct SaveParticipantReference final {
        SaveParticipantId participant;
    };

    /** @brief Stable cross-record target decoded independently from its referenced record. */
    struct SaveRecordReference final {
        SaveParticipantId participant;
        SaveRecordId record;
    };

    /** @brief Closed durable reference forms; monostate is the canonical null reference. */
    using SaveReferenceTarget = std::variant<std::monostate, SaveAssetReference, SaveSceneReference, SaveEntityReference,
                                             SavePrefabProvenance, SaveParticipantReference, SaveRecordReference>;

    /** @brief Flat post-decode reconciliation state that never recursively owns referenced records. */
    enum class SaveReferenceDisposition : std::uint8_t {
        Resolved = 0,
        Missing = 1,
        Remapped = 2,
        Deferred = 3
    };

    /** @brief Reconciliation evidence retaining the original target for missing/deferred diagnostics. */
    struct SaveReferenceResolution final {
        SaveReferenceTarget original;
        SaveReferenceDisposition disposition{SaveReferenceDisposition::Missing};
        std::optional<SaveReferenceTarget> replacement;
    };

    /** @brief Validates a durable reference without resolving external state. @param target Candidate target. @return Success or typed
     * error. */
    [[nodiscard]] Result<void> ValidateSaveReference(const SaveReferenceTarget &target);
    /** @brief Canonically encodes one flat durable reference. @param target Valid stable target. @param limits Codec bounds. @return Bytes
     * or field-aware failure. */
    [[nodiscard]] CanonicalCodecResult<std::vector<std::byte>> EncodeSaveReference(const SaveReferenceTarget &target,
                                                                                   CanonicalCodecLimits limits = {});
    /** @brief Decodes one complete durable reference without resolving it. @param bytes Complete encoded value. @param limits Codec bounds.
     * @return Target or field-aware failure. */
    [[nodiscard]] CanonicalCodecResult<SaveReferenceTarget> DecodeSaveReference(std::span<const std::byte> bytes,
                                                                                CanonicalCodecLimits limits = {});
    /** @brief Validates a separate reconciliation result. @param resolution Candidate result. @return Success or typed error. */
    [[nodiscard]] Result<void> ValidateSaveReferenceResolution(const SaveReferenceResolution &resolution);
}  // namespace Horo::Runtime
