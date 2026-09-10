#pragma once

/**
 * @file DestructionErrors.h
 * @brief Stable destruction contract failures independent of physics and rendering backends.
 */

#include "Horo/Foundation/ErrorCode.h"

namespace Horo::Destruction::DestructionErrors {
    /** @brief A destruction identity contains a reserved or incomplete representation. */
    extern const ErrorCodeDescriptor IdentityInvalid;
    /** @brief An identity belongs to a different asset, world, or destructible owner. */
    extern const ErrorCodeDescriptor IdentityUnknown;
    /** @brief An identity belongs to a retired runtime generation. */
    extern const ErrorCodeDescriptor StaleGeneration;
    /** @brief An identity names a replaced fracture content version. */
    extern const ErrorCodeDescriptor StaleContent;
    /** @brief An identity names a semantic state revision that is no longer current. */
    extern const ErrorCodeDescriptor StaleRevision;
    /** @brief A generation cannot advance without reusing a previously issued value. */
    extern const ErrorCodeDescriptor GenerationExhausted;
    /** @brief A revision cannot advance without reusing a previously issued value. */
    extern const ErrorCodeDescriptor RevisionExhausted;
    /** @brief Canonical serialized destruction identity bytes are malformed. */
    extern const ErrorCodeDescriptor SerializedIdentityInvalid;
    /** @brief A destructible descriptor contains malformed or contradictory policy. */
    extern const ErrorCodeDescriptor DescriptorInvalid;
    /** @brief A feature tier value is unknown to this descriptor contract. */
    extern const ErrorCodeDescriptor TierInvalid;
    /** @brief A required feature is unavailable in the exact selected tier. */
    extern const ErrorCodeDescriptor FeatureUnsatisfied;
    /** @brief Runtime geometry generation was requested from the core pre-cooked contract. */
    extern const ErrorCodeDescriptor RuntimeGeometryUnsupported;
    /** @brief A descriptor limit is zero, contradictory, above its tier, or above an engine ceiling. */
    extern const ErrorCodeDescriptor LimitProfileInvalid;
    /** @brief Exact cooked counts or peak cost exceed the descriptor's admitted limits. */
    extern const ErrorCodeDescriptor LimitExceeded;
    /** @brief A descriptor belongs to a replaced immutable configuration publication. */
    extern const ErrorCodeDescriptor StaleConfiguration;
}  // namespace Horo::Destruction::DestructionErrors
