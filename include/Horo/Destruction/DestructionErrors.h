#pragma once

/**
 * @file DestructionErrors.h
 * @brief Stable destruction identity failures independent of physics and rendering backends.
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
}  // namespace Horo::Destruction::DestructionErrors
