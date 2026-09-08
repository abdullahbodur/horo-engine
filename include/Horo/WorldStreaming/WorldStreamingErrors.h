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
    /** @brief A partition epoch, cell-attempt generation, runtime-source revision, or authoring-page revision cannot advance. */
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
    /** @brief A cooked world-index manifest is incomplete or contains malformed metadata. */
    extern const ErrorCodeDescriptor CookedManifestInvalid;
    /** @brief A cooked world-index manifest exceeds a mandatory count or byte ceiling. */
    extern const ErrorCodeDescriptor CookedManifestCapacityExceeded;
    /** @brief Cooked metadata does not map one-to-one to the authoritative descriptor cells. */
    extern const ErrorCodeDescriptor CookedManifestIdentityConflict;
    /** @brief A cooked cell dependency is invalid, duplicated, self-referential, or absent from the manifest. */
    extern const ErrorCodeDescriptor CookedManifestDependencyInvalid;
    /** @brief A spatial-assignment request is empty, malformed, or has unordered/out-of-partition bounds. */
    extern const ErrorCodeDescriptor SpatialAssignmentInvalid;
    /** @brief A spatial-assignment request repeats one stable authored-object address. */
    extern const ErrorCodeDescriptor SpatialAssignmentIdentityConflict;
    /** @brief Spatial assignment exceeds an object-count, per-object-cell, or total-assignment ceiling. */
    extern const ErrorCodeDescriptor SpatialAssignmentCapacityExceeded;
    /** @brief A requested spatial-assignment layer is not declared by the authoritative descriptor. */
    extern const ErrorCodeDescriptor SpatialAssignmentUnsupported;
    /** @brief A quantized object intersects a cell absent from the authoritative descriptor. */
    extern const ErrorCodeDescriptor SpatialAssignmentCellUnavailable;
    /** @brief A dependency-plan request has malformed, unknown, self-referential, duplicated, or missing source data. */
    extern const ErrorCodeDescriptor DependencyPlanInvalid;
    /** @brief A dependency endpoint revision does not match the admitted spatial-assignment revision. */
    extern const ErrorCodeDescriptor DependencyPlanRevisionStale;
    /** @brief A dependency plan exceeds an edge, per-object, bundle-member, or soft-reference ceiling. */
    extern const ErrorCodeDescriptor DependencyPlanCapacityExceeded;
    /** @brief A hard dependency target is absent from the admitted spatial assignments. */
    extern const ErrorCodeDescriptor DependencyPlanHardTargetMissing;
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
    /** @brief A world-authoring contract, page descriptor, request, or authority snapshot is malformed. */
    extern const ErrorCodeDescriptor AuthoringContractInvalid;
    /** @brief The requested world-authoring contract schema version is unsupported. */
    extern const ErrorCodeDescriptor AuthoringVersionUnsupported;
    /** @brief The requested authoring granularity or collaboration authority is unsupported. */
    extern const ErrorCodeDescriptor AuthoringPolicyUnsupported;
    /** @brief The authoring request does not match the current page identity or partition. */
    extern const ErrorCodeDescriptor AuthoringIdentityConflict;
    /** @brief The authoring request carries a stale or non-successor page revision. */
    extern const ErrorCodeDescriptor AuthoringRevisionStale;
    /** @brief The bounded authoring-page capacity cannot admit another open page. */
    extern const ErrorCodeDescriptor AuthoringCapacityExceeded;
    /** @brief Authoring admission is closed because the owner is cancelling or shut down. */
    extern const ErrorCodeDescriptor AuthoringLifecycleUnavailable;
}  // namespace Horo::WorldStreaming::WorldStreamingErrors
