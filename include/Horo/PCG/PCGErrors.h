#pragma once

/**
 * @file PCGErrors.h
 * @brief Stable procedural-generation identity failures independent of runtime and target backends.
 */

#include "Horo/Foundation/ErrorCode.h"

namespace Horo::PCG::PCGErrors {
    /** @brief A PCG identity contains a reserved value. */
    extern const ErrorCodeDescriptor IdentityInvalid;
    /** @brief A PCG identity belongs to a different graph or logical owner. */
    extern const ErrorCodeDescriptor IdentityUnknown;
    /** @brief A PCG identity belongs to a retired graph revision or execution. */
    extern const ErrorCodeDescriptor IdentityStale;
    /** @brief A graph revision cannot advance without reusing a previously issued value. */
    extern const ErrorCodeDescriptor RevisionExhausted;
    /** @brief Canonical serialized PCG identity bytes contain a reserved value. */
    extern const ErrorCodeDescriptor SerializedIdentityInvalid;
}  // namespace Horo::PCG::PCGErrors
