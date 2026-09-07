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
    /** @brief A partition epoch, cell-attempt generation, or source revision cannot advance without wrapping. */
    extern const ErrorCodeDescriptor GenerationExhausted;
    /** @brief A world-cell grid has zero/overflowing cell size, inverted bounds, or no LODs. */
    extern const ErrorCodeDescriptor QuantizationPolicyInvalid;
    /** @brief A coordinate cannot be translated relative to the grid origin without signed overflow. */
    extern const ErrorCodeDescriptor CoordinateOutOfRange;
    /** @brief The requested LOD is not declared by the grid policy. */
    extern const ErrorCodeDescriptor LodUnsupported;
    /** @brief The deterministic cell coordinate falls outside the inclusive manifest grid bounds. */
    extern const ErrorCodeDescriptor CellOutOfBounds;
    /** @brief A world-partition descriptor is incomplete or contains malformed fields. */
    extern const ErrorCodeDescriptor PartitionDescriptorInvalid;
    /** @brief A world-partition descriptor uses an unsupported schema version. */
    extern const ErrorCodeDescriptor PartitionVersionUnsupported;
    /** @brief World content bounds are unordered, overflow the grid envelope, or lie outside it. */
    extern const ErrorCodeDescriptor PartitionBoundsInvalid;
    /** @brief A partition descriptor exceeds mandatory host storage limits. */
    extern const ErrorCodeDescriptor PartitionCapacityExceeded;
    /** @brief A partition descriptor repeats a layer or exact cell identity. */
    extern const ErrorCodeDescriptor PartitionIdentityConflict;
    /** @brief A streaming source descriptor or admission context is structurally invalid. */
    extern const ErrorCodeDescriptor SourceDescriptorInvalid;
    /** @brief A streaming source intent is not supported by this contract version. */
    extern const ErrorCodeDescriptor SourceIntentUnsupported;
    /** @brief A streaming source owner token no longer names the active owner lifetime. */
    extern const ErrorCodeDescriptor SourceOwnerStale;
    /** @brief A source update does not advance the currently admitted revision. */
    extern const ErrorCodeDescriptor SourceRevisionStale;
    /** @brief A new source cannot be admitted within the configured bounded capacity. */
    extern const ErrorCodeDescriptor SourceCapacityExceeded;
    /** @brief Source admission is closed because its owner is cancelling or shut down. */
    extern const ErrorCodeDescriptor SourceLifecycleUnavailable;
    /** @brief A source shape is malformed, unbounded, or violates its representation contract. */
    extern const ErrorCodeDescriptor SourceShapeInvalid;
    /** @brief The evaluating host does not support the requested source shape category. */
    extern const ErrorCodeDescriptor SourceShapeUnsupported;
    /** @brief A source desired state combines residency and retention inconsistently. */
    extern const ErrorCodeDescriptor SourceDesiredStateInvalid;
    /** @brief A source desired-state residency or retention value is not supported by this contract version. */
    extern const ErrorCodeDescriptor SourceDesiredStateUnsupported;
}  // namespace Horo::WorldStreaming::WorldStreamingErrors
