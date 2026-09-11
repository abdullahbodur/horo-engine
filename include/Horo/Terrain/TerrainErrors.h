#pragma once

/**
 * @file TerrainErrors.h
 * @brief Stable backend-neutral TerrainApi failure identities.
 */

#include "Horo/Foundation/ErrorCode.h"

#include <span>

namespace Horo::Terrain::TerrainErrors {
    /** @brief A stable or runtime terrain identity uses a reserved representation. */
    extern const ErrorCodeDescriptor IdentityInvalid;
    /** @brief Persisted terrain identity bytes cannot represent the requested typed identity. */
    extern const ErrorCodeDescriptor SerializedIdentityInvalid;
    /** @brief Canonical authored identity derivation input is empty or exceeds its hard bound. */
    extern const ErrorCodeDescriptor DerivationInvalid;
    /** @brief A bounded identity catalog contains a duplicate in one typed domain. */
    extern const ErrorCodeDescriptor IdentityConflict;
    /** @brief A runtime identity names a different owner or slot. */
    extern const ErrorCodeDescriptor IdentityUnknown;
    /** @brief A runtime identity belongs to a replaced generation. */
    extern const ErrorCodeDescriptor GenerationStale;
    /** @brief A non-wrapping terrain revision or runtime generation cannot advance. */
    extern const ErrorCodeDescriptor GenerationExhausted;
    /** @brief A terrain identity catalog exceeds the validation ceiling. */
    extern const ErrorCodeDescriptor CapacityExceeded;
    /** @brief The terrain runtime is closing or closed and no longer admits identity access. */
    extern const ErrorCodeDescriptor LifecycleUnavailable;
    /** @brief A shared Terrain/Foliage descriptor is malformed or internally inconsistent. */
    extern const ErrorCodeDescriptor DescriptorInvalid;
    /** @brief A provider-neutral Terrain feature tier value is outside the closed vocabulary. */
    extern const ErrorCodeDescriptor TierInvalid;
    /** @brief The exact requested Terrain tier is unavailable in the captured host/content plan. */
    extern const ErrorCodeDescriptor TierUnsupported;
    /** @brief A project Terrain limit profile is empty, inconsistent, or exceeds its tier ceiling. */
    extern const ErrorCodeDescriptor LimitProfileInvalid;
    /** @brief Dataset counts, bytes, or work exceed the captured project limits. */
    extern const ErrorCodeDescriptor LimitExceeded;
    /** @brief A descriptor admission references an outdated content, configuration, capability, or bounds revision. */
    extern const ErrorCodeDescriptor RevisionStale;
    /** @brief Insert or replacement state is incomplete or contradicts the current publication. */
    extern const ErrorCodeDescriptor ReplacementInvalid;

    /**
     * @brief Returns every stable TerrainApi descriptor for module-registry contribution.
     * @return Bounded immutable descriptor references owned for process lifetime by TerrainApi.
     */
    [[nodiscard]] std::span<const ErrorCodeDescriptor *const> Descriptors() noexcept;
}  // namespace Horo::Terrain::TerrainErrors
