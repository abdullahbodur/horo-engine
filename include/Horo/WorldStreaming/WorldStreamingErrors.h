#pragma once

/**
 * @file WorldStreamingErrors.h
 * @brief Stable errors for world-partition identity and spatial contracts.
 */

#include "Horo/Foundation/ErrorCode.h"

namespace Horo::WorldStreaming::WorldStreamingErrors {
    /** @brief A world-partition identity uses its reserved invalid representation. */
    extern const ErrorCodeDescriptor IdentityInvalid;
    /** @brief A canonical serialized identity is malformed or contains a reserved value. */
    extern const ErrorCodeDescriptor SerializedIdentityInvalid;
    /** @brief A partition epoch or cell-attempt generation cannot advance without wrapping. */
    extern const ErrorCodeDescriptor GenerationExhausted;
}  // namespace Horo::WorldStreaming::WorldStreamingErrors
