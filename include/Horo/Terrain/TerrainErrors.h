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

    /**
     * @brief Returns every stable TerrainApi descriptor for module-registry contribution.
     * @return Bounded immutable descriptor references owned for process lifetime by TerrainApi.
     */
    [[nodiscard]] std::span<const ErrorCodeDescriptor *const> Descriptors() noexcept;
}  // namespace Horo::Terrain::TerrainErrors
