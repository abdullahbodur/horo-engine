#pragma once

/**
 * @file WorldStreamingErrors.h
 * @brief Stable errors for world-partition identity and spatial contracts.
 */

#include "Horo/Foundation/ErrorCode.h"

namespace Horo::WorldStreaming::WorldStreamingErrors {
    /** @brief An origin-frame binding or externally supplied local coordinate is malformed. */
    extern const ErrorCodeDescriptor OriginFrameInvalid;
    /** @brief A local coordinate, candidate, or lease belongs to a non-active origin generation. */
    extern const ErrorCodeDescriptor OriginFrameStale;
    /** @brief A global/local conversion exceeds the supported local frame or signed global range. */
    extern const ErrorCodeDescriptor OriginFrameRangeExceeded;
    /** @brief An externally supplied local coordinate cannot preserve canonical millimeter precision. */
    extern const ErrorCodeDescriptor OriginFramePrecisionLoss;
    /** @brief An origin-frame operation is unavailable because no candidate exists or the owner is closed. */
    extern const ErrorCodeDescriptor OriginFrameLifecycleUnavailable;
    /** @brief Storage required for an origin-frame owner or replacement lease could not be allocated. */
    extern const ErrorCodeDescriptor OriginFrameStorageUnavailable;
    /** @brief A World Streaming diagnostic snapshot, decision row or aggregate queue fact is malformed. */
    extern const ErrorCodeDescriptor DiagnosticProjectionInvalid;
    /** @brief A diagnostic row names a foreign owner, partition epoch, operation or revision. */
    extern const ErrorCodeDescriptor DiagnosticProjectionStale;
    /** @brief A diagnostic lifecycle, cell-state, event or failure value is unsupported. */
    extern const ErrorCodeDescriptor DiagnosticProjectionUnsupported;
    /** @brief A diagnostic snapshot repeats a stable source, cell, failure, event, sequence or context identity. */
    extern const ErrorCodeDescriptor DiagnosticProjectionIdentityConflict;
    /** @brief A diagnostic snapshot exceeds a configured or implementation-owned bound. */
    extern const ErrorCodeDescriptor DiagnosticProjectionCapacityExceeded;
    /** @brief A cell-state ledger, owner, fence, or record is malformed. */
    extern const ErrorCodeDescriptor CellStateInvalid;
    /** @brief No tracked residency record exists for the exact mounted cell attempt. */
    extern const ErrorCodeDescriptor CellStateUnresolved;
    /** @brief A cell-state request names a foreign owner, epoch, cell, or non-successor generation. */
    extern const ErrorCodeDescriptor CellStateStale;
    /** @brief A canonical operation phase/outcome cannot be projected by this residency contract version. */
    extern const ErrorCodeDescriptor CellStateUnsupported;
    /** @brief A projected residency transition is illegal from the current canonical state. */
    extern const ErrorCodeDescriptor CellStateTransitionInvalid;
    /** @brief A fresh or overlapping cell attempt cannot fit the ledger's mandatory tracked-attempt ceiling. */
    extern const ErrorCodeDescriptor CellStateCapacityExceeded;
    /** @brief A draining or closed cell-state ledger rejects new loading or publication. */
    extern const ErrorCodeDescriptor CellStateLifecycleUnavailable;
    /** @brief A runtime composition omits or malforms its explicit owner, scheduler, core service, or adapter facts. */
    extern const ErrorCodeDescriptor RuntimeCompositionInvalid;
    /** @brief Runtime composition service identities are duplicated. */
    extern const ErrorCodeDescriptor RuntimeCompositionIdentityConflict;
    /** @brief Runtime composition bindings exceed their mandatory feature-adapter ceiling. */
    extern const ErrorCodeDescriptor RuntimeCompositionCapacityExceeded;
    /** @brief A runtime composition command names a foreign owner or stale/non-successor revision. */
    extern const ErrorCodeDescriptor RuntimeCompositionRevisionStale;
    /** @brief Runtime composition replacement or cancellation is unavailable while draining, closed, or retaining work. */
    extern const ErrorCodeDescriptor RuntimeCompositionLifecycleUnavailable;
    /** @brief A fallback provider descriptor has malformed ownership, revision, mode-specific or cell data. */
    extern const ErrorCodeDescriptor FallbackProviderInvalid;
    /** @brief A fallback provider mode is unknown to this contract version. */
    extern const ErrorCodeDescriptor FallbackProviderUnsupported;
    /** @brief A single-cell fallback cannot fit the caller's mandatory published-cell ceiling. */
    extern const ErrorCodeDescriptor FallbackProviderCapacityExceeded;
    /** @brief A fallback operation names a foreign owner or non-successor configuration revision. */
    extern const ErrorCodeDescriptor FallbackProviderStale;
    /** @brief A cancelling or closed fallback provider rejects replacement or cancellation admission. */
    extern const ErrorCodeDescriptor FallbackProviderLifecycleUnavailable;
    /** @brief A world-partition identity uses its reserved invalid representation. */
    extern const ErrorCodeDescriptor IdentityInvalid;
    /** @brief A canonical serialized identity is malformed or contains a reserved value. */
    extern const ErrorCodeDescriptor SerializedIdentityInvalid;
    /** @brief A partition epoch, cell-attempt generation, runtime-source revision, or authoring-page revision cannot advance. */
    extern const ErrorCodeDescriptor GenerationExhausted;
    /** @brief A cell operation has a malformed identity, fence, or initial representation. */
    extern const ErrorCodeDescriptor CellOperationInvalid;
    /** @brief A completion or command does not name the exact cell operation and fence. */
    extern const ErrorCodeDescriptor CellOperationStale;
    /** @brief A cell operation transition value is unknown to this contract version. */
    extern const ErrorCodeDescriptor CellOperationUnsupported;
    /** @brief A known cell operation transition is not legal from the current phase. */
    extern const ErrorCodeDescriptor CellOperationTransitionInvalid;
    /** @brief A scheduler admission ledger, request, or reservation is malformed. */
    extern const ErrorCodeDescriptor SchedulerAdmissionInvalid;
    /** @brief Scheduler operation count or generic capacity cannot be reserved within configured ceilings. */
    extern const ErrorCodeDescriptor SchedulerCapacityExceeded;
    /** @brief A scheduler operation already owns a reservation in this ledger. */
    extern const ErrorCodeDescriptor SchedulerReservationConflict;
    /** @brief A scheduler command does not name the exact owner-scoped reservation and operation fence. */
    extern const ErrorCodeDescriptor SchedulerReservationStale;
    /** @brief Scheduler admission is draining, closed, or waiting for an operation to retire. */
    extern const ErrorCodeDescriptor SchedulerLifecycleUnavailable;
    /** @brief A multidimensional budget vector, policy, request, or evaluation context is malformed. */
    extern const ErrorCodeDescriptor BudgetModelInvalid;
    /** @brief A budget vector or policy names a resource dimension unsupported by this contract version. */
    extern const ErrorCodeDescriptor BudgetDimensionUnsupported;
    /** @brief A budget policy or usage sample revision no longer matches current authority state. */
    extern const ErrorCodeDescriptor BudgetRevisionStale;
    /** @brief A budget sample has malformed window timing. */
    extern const ErrorCodeDescriptor BudgetSampleInvalid;
    /** @brief A budget sample belongs to an earlier completed sampling window. */
    extern const ErrorCodeDescriptor BudgetSampleStale;
    /** @brief Projected usage overflows or exceeds one independent hard resource limit. */
    extern const ErrorCodeDescriptor BudgetCapacityExceeded;
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
    /** @brief A spanning-object plan or directive is malformed, missing, or applied below its threshold. */
    extern const ErrorCodeDescriptor SpanningObjectPlanInvalid;
    /** @brief A spanning-object directive repeats or names an object absent from spatial assignment. */
    extern const ErrorCodeDescriptor SpanningObjectPlanIdentityConflict;
    /** @brief A spanning-object directive does not match the assigned immutable object revision. */
    extern const ErrorCodeDescriptor SpanningObjectPlanRevisionStale;
    /** @brief A spanning-object directive uses an unknown policy value. */
    extern const ErrorCodeDescriptor SpanningObjectPlanUnsupported;
    /** @brief A spanning-object plan exceeds its object or aggregate placement-cell ceiling. */
    extern const ErrorCodeDescriptor SpanningObjectPlanCapacityExceeded;
    /** @brief An explicit single-cell owner is not covered by the source spatial assignment. */
    extern const ErrorCodeDescriptor SpanningObjectPlanOwnerUnavailable;
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
    /** @brief A desired-state reduction context or mandatory limit is malformed. */
    extern const ErrorCodeDescriptor SourceReductionInvalid;
    /** @brief A desired-state reduction repeats one stable source identity. */
    extern const ErrorCodeDescriptor SourceReductionIdentityConflict;
    /** @brief A desired-state reduction exceeds its bounded contributor ceiling. */
    extern const ErrorCodeDescriptor SourceReductionCapacityExceeded;
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
    /** @brief A spatial-object descriptor, request, or owner snapshot is structurally invalid. */
    extern const ErrorCodeDescriptor SpatialObjectDescriptorInvalid;
    /** @brief The spatial-object descriptor schema version is unsupported. */
    extern const ErrorCodeDescriptor SpatialObjectVersionUnsupported;
    /** @brief The spatial-object placement class is unsupported by this contract version. */
    extern const ErrorCodeDescriptor SpatialObjectPlacementUnsupported;
    /** @brief A replacement does not name the currently admitted authored-object identity. */
    extern const ErrorCodeDescriptor SpatialObjectIdentityConflict;
    /** @brief A replacement carries a stale, missing, or non-successor authoring revision. */
    extern const ErrorCodeDescriptor SpatialObjectRevisionStale;
    /** @brief A new spatial-object descriptor exceeds the bounded owner capacity. */
    extern const ErrorCodeDescriptor SpatialObjectCapacityExceeded;
    /** @brief Spatial-object admission is closed because its owner is cancelling or shut down. */
    extern const ErrorCodeDescriptor SpatialObjectLifecycleUnavailable;
}  // namespace Horo::WorldStreaming::WorldStreamingErrors
