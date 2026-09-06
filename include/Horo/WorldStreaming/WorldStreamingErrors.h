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
    /** @brief A world-cell grid has zero/overflowing cell size, inverted bounds, or no LODs. */
    extern const ErrorCodeDescriptor QuantizationPolicyInvalid;
    /** @brief A coordinate cannot be translated relative to the grid origin without signed overflow. */
    extern const ErrorCodeDescriptor CoordinateOutOfRange;
    /** @brief The requested LOD is not declared by the grid policy. */
    extern const ErrorCodeDescriptor LodUnsupported;
    /** @brief The deterministic cell coordinate falls outside the inclusive manifest grid bounds. */
    extern const ErrorCodeDescriptor CellOutOfBounds;
}  // namespace Horo::WorldStreaming::WorldStreamingErrors
