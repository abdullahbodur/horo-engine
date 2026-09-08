#include "Horo/WorldStreaming/WorldStreamingErrors.h"

namespace Horo::WorldStreaming::WorldStreamingErrors {
    namespace {
        const ErrorDomainId Domain{"horo.world_streaming"};

        /** @brief Builds an immutable descriptor in the world-streaming error domain. */
        [[nodiscard]] ErrorCodeDescriptor Describe(const char *code, const ErrorSeverity severity, const char *summary,
                                                   const char *remediationHint, const bool userActionable) {
            return {
                .domain = Domain,
                .code = ErrorCode{code},
                .defaultSeverity = severity,
                .summary = summary,
                .remediationHint = remediationHint,
                .retryable = false,
                .userActionable = userActionable,
            };
        }
    }  // namespace

    const ErrorCodeDescriptor IdentityInvalid =
        Describe("world_streaming.identity.invalid", ErrorSeverity::Error,
                 "A world-partition identity uses its reserved invalid representation.",
                 "Use an identity issued by the manifest, authoring boundary, or active partition authority.", true);
    const ErrorCodeDescriptor SerializedIdentityInvalid =
        Describe("world_streaming.identity.serialized_invalid", ErrorSeverity::Error,
                 "A serialized world-partition identity is malformed or reserved.",
                 "Rebuild the world index or source descriptor with canonical little-endian identity bytes.", true);
    const ErrorCodeDescriptor GenerationExhausted =
        Describe("world_streaming.generation.exhausted", ErrorSeverity::Critical,
                 "A world-partition epoch, cell generation, runtime-source revision, or authoring-page revision cannot advance.",
                 "Retire the exhausted incarnation, slot, source, or page; never wrap an issued world-streaming counter.", false);
    const ErrorCodeDescriptor CellOperationInvalid =
        Describe("world_streaming.cell_operation.invalid", ErrorSeverity::Error,
                 "A cell operation has a malformed identity, fence, or initial representation.",
                 "Use a non-zero operation identity and the exact valid fence issued for the cell attempt.", true);
    const ErrorCodeDescriptor CellOperationStale =
        Describe("world_streaming.cell_operation.stale", ErrorSeverity::Warning,
                 "A command or completion does not name the exact cell operation and fence.",
                 "Discard stale publication while routing retirement acknowledgement to its matching retained operation.", false);
    const ErrorCodeDescriptor CellOperationUnsupported =
        Describe("world_streaming.cell_operation.unsupported", ErrorSeverity::Error,
                 "A cell operation transition value is unsupported by this contract version.",
                 "Use one of the declared typed cell-operation transitions.", true);
    const ErrorCodeDescriptor CellOperationTransitionInvalid =
        Describe("world_streaming.cell_operation.transition_invalid", ErrorSeverity::Error,
                 "A known cell operation transition is not legal from the current phase.",
                 "Follow the queued, admitted, preparing, activating, retiring, and terminal lifecycle order.", false);
    const ErrorCodeDescriptor QuantizationPolicyInvalid =
        Describe("world_streaming.quantization.policy_invalid", ErrorSeverity::Error,
                 "The world-cell quantization policy has invalid size, bounds, or LOD limits.",
                 "Use a positive millimeter cell size, ordered int32 grid bounds, and at least one supported LOD.", true);
    const ErrorCodeDescriptor CoordinateOutOfRange =
        Describe("world_streaming.quantization.coordinate_out_of_range", ErrorSeverity::Error,
                 "The world coordinate cannot be translated into the grid's signed 64-bit relative frame.",
                 "Use a grid origin and world coordinate whose exact millimeter difference is representable.", true);
    const ErrorCodeDescriptor LodUnsupported = Describe("world_streaming.quantization.lod_unsupported", ErrorSeverity::Error,
                                                        "The requested world-cell LOD is not declared by the grid policy.",
                                                        "Request a LOD below the validated manifest lodLevels value.", true);
    const ErrorCodeDescriptor CellOutOfBounds =
        Describe("world_streaming.quantization.cell_out_of_bounds", ErrorSeverity::Error,
                 "The quantized world cell is outside the manifest's inclusive grid bounds.",
                 "Reject the spatial request or use a validated partition whose bounds contain the coordinate.", false);
    const ErrorCodeDescriptor PartitionDescriptorInvalid =
        Describe("world_streaming.partition.descriptor_invalid", ErrorSeverity::Error,
                 "A world-partition descriptor is incomplete or contains malformed fields.",
                 "Provide a valid partition identity, layers, cells, package references and versioned field values.", true);
    const ErrorCodeDescriptor PartitionVersionUnsupported =
        Describe("world_streaming.partition.version_unsupported", ErrorSeverity::Error,
                 "The world-partition descriptor schema version is unsupported.",
                 "Migrate the world index to the exact schema version supported by this runtime.", true);
    const ErrorCodeDescriptor PartitionBoundsInvalid =
        Describe("world_streaming.partition.bounds_invalid", ErrorSeverity::Error,
                 "World content bounds are unordered or outside the representable grid envelope.",
                 "Use ordered exact bounds fully contained by the validated level-zero partition grid.", true);
    const ErrorCodeDescriptor PartitionCapacityExceeded =
        Describe("world_streaming.partition.capacity_exceeded", ErrorSeverity::Error,
                 "The world-partition descriptor exceeds a mandatory host storage limit.",
                 "Reduce manifest layer, cell, or layer-name data, or choose an explicitly larger supported limit.", true);
    const ErrorCodeDescriptor PartitionIdentityConflict =
        Describe("world_streaming.partition.identity_conflict", ErrorSeverity::Error,
                 "The world-partition descriptor repeats a layer or exact cell identity.",
                 "Remove duplicate identities and regenerate the canonical world index.", true);
    const ErrorCodeDescriptor CookedManifestInvalid =
        Describe("world_streaming.cooked_manifest.invalid", ErrorSeverity::Error,
                 "A cooked world-index manifest is incomplete or contains malformed cell metadata.",
                 "Provide one non-empty cooked record for every validated partition cell.", true);
    const ErrorCodeDescriptor CookedManifestCapacityExceeded =
        Describe("world_streaming.cooked_manifest.capacity_exceeded", ErrorSeverity::Error,
                 "A cooked world-index manifest exceeds a mandatory count or byte ceiling.",
                 "Reduce cell metadata or choose an explicitly larger supported manifest limit.", true);
    const ErrorCodeDescriptor CookedManifestIdentityConflict =
        Describe("world_streaming.cooked_manifest.identity_conflict", ErrorSeverity::Error,
                 "Cooked cell metadata does not map one-to-one to the authoritative partition cells.",
                 "Regenerate the cooked index with exactly one record for each descriptor cell.", true);
    const ErrorCodeDescriptor CookedManifestDependencyInvalid =
        Describe("world_streaming.cooked_manifest.dependency_invalid", ErrorSeverity::Error,
                 "A cooked cell dependency is invalid, duplicated, self-referential, or absent from the manifest.",
                 "Emit unique required cell identities that belong to the same cooked partition.", true);
    const ErrorCodeDescriptor SpatialAssignmentInvalid =
        Describe("world_streaming.spatial_assignment.invalid", ErrorSeverity::Error,
                 "A spatial-assignment request is empty, malformed, or outside the partition content bounds.",
                 "Provide valid page, object, revision, layer and LOD data with ordered canonical bounds.", true);
    const ErrorCodeDescriptor SpatialAssignmentIdentityConflict =
        Describe("world_streaming.spatial_assignment.identity_conflict", ErrorSeverity::Error,
                 "A spatial-assignment request repeats one stable authored-object address.",
                 "Submit exactly one immutable revision for each page-scoped authored object.", true);
    const ErrorCodeDescriptor SpatialAssignmentCapacityExceeded =
        Describe("world_streaming.spatial_assignment.capacity_exceeded", ErrorSeverity::Error,
                 "Spatial assignment exceeds a mandatory object or cell-count ceiling.",
                 "Reduce authored-object coverage or choose explicitly larger supported cook limits.", true);
    const ErrorCodeDescriptor SpatialAssignmentUnsupported =
        Describe("world_streaming.spatial_assignment.unsupported", ErrorSeverity::Error,
                 "A spatial-assignment request names a layer absent from the partition descriptor.",
                 "Assign the object to a layer declared by the authoritative partition descriptor.", true);
    const ErrorCodeDescriptor SpatialAssignmentCellUnavailable =
        Describe("world_streaming.spatial_assignment.cell_unavailable", ErrorSeverity::Error,
                 "A quantized authored object intersects a cell absent from the partition descriptor.",
                 "Declare every intersected cell or apply an explicit spanning-object cook policy.", true);
    const ErrorCodeDescriptor SpanningObjectPlanInvalid =
        Describe("world_streaming.spanning_object.invalid", ErrorSeverity::Error,
                 "A spanning-object plan or directive is malformed, missing, or applied to a direct object.",
                 "Provide one valid explicit directive only for each object exceeding the direct-cell threshold.", true);
    const ErrorCodeDescriptor SpanningObjectPlanIdentityConflict =
        Describe("world_streaming.spanning_object.identity_conflict", ErrorSeverity::Error,
                 "A spanning-object directive is duplicated or names an object absent from spatial assignment.",
                 "Provide at most one directive for each exact object admitted by the spatial-assignment result.", true);
    const ErrorCodeDescriptor SpanningObjectPlanRevisionStale =
        Describe("world_streaming.spanning_object.revision_stale", ErrorSeverity::Warning,
                 "A spanning-object directive does not match the admitted immutable object revision.",
                 "Rebuild policy directives from the exact spatial-assignment snapshot being cooked.", false);
    const ErrorCodeDescriptor SpanningObjectPlanUnsupported =
        Describe("world_streaming.spanning_object.unsupported", ErrorSeverity::Error,
                 "A spanning-object directive uses an unsupported cook policy.",
                 "Use explicit single-cell ownership, per-cell splitting, or non-spatial placement.", true);
    const ErrorCodeDescriptor SpanningObjectPlanCapacityExceeded =
        Describe("world_streaming.spanning_object.capacity_exceeded", ErrorSeverity::Error,
                 "A spanning-object plan exceeds a mandatory object or placement-cell ceiling.",
                 "Reduce object coverage or choose explicitly larger supported cook limits.", true);
    const ErrorCodeDescriptor SpanningObjectPlanOwnerUnavailable =
        Describe("world_streaming.spanning_object.owner_unavailable", ErrorSeverity::Error,
                 "The explicit single-cell owner is not covered by the source spatial assignment.",
                 "Choose one canonical cell from the exact object's spatial-assignment coverage.", true);
    const ErrorCodeDescriptor DependencyPlanInvalid =
        Describe("world_streaming.dependency_plan.invalid", ErrorSeverity::Error,
                 "A dependency-plan request contains malformed, duplicated, self-referential, or missing source data.",
                 "Provide unique directed edges from admitted authored objects with valid exact endpoints.", true);
    const ErrorCodeDescriptor DependencyPlanRevisionStale =
        Describe("world_streaming.dependency_plan.revision_stale", ErrorSeverity::Warning,
                 "A dependency endpoint revision does not match the admitted spatial-assignment revision.",
                 "Rebuild the authored graph from the same immutable object revisions used for spatial assignment.", false);
    const ErrorCodeDescriptor DependencyPlanCapacityExceeded =
        Describe("world_streaming.dependency_plan.capacity_exceeded", ErrorSeverity::Error,
                 "A dependency plan exceeds a mandatory edge, bundle, or reference ceiling.",
                 "Reduce graph density or choose explicitly larger supported cook limits.", true);
    const ErrorCodeDescriptor DependencyPlanHardTargetMissing =
        Describe("world_streaming.dependency_plan.hard_target_missing", ErrorSeverity::Error,
                 "A hard dependency target is absent from the admitted spatial assignments.",
                 "Include the exact target revision in spatial assignment or change the authored edge to a soft reference.", true);
    const ErrorCodeDescriptor SourceDescriptorInvalid =
        Describe("world_streaming.source.descriptor_invalid", ErrorSeverity::Error,
                 "A streaming source descriptor or admission context is structurally invalid.",
                 "Provide valid source, owner and revision identities with a finite non-negative priority.", true);
    const ErrorCodeDescriptor SourceIntentUnsupported =
        Describe("world_streaming.source.intent_unsupported", ErrorSeverity::Error,
                 "The streaming source intent is not supported by this contract version.",
                 "Use a declared camera, gameplay, network-relevance or preload intent.", true);
    const ErrorCodeDescriptor SourceOwnerStale =
        Describe("world_streaming.source.owner_stale", ErrorSeverity::Warning,
                 "The streaming source owner token no longer names the active owner lifetime.",
                 "Discard the stale request and resolve the current partition owner token before retrying.", false);
    const ErrorCodeDescriptor SourceRevisionStale =
        Describe("world_streaming.source.revision_stale", ErrorSeverity::Warning,
                 "The streaming source update does not advance the admitted revision.",
                 "Issue a strictly newer non-wrapping revision for the same stable source identity.", false);
    const ErrorCodeDescriptor SourceCapacityExceeded =
        Describe("world_streaming.source.capacity_exceeded", ErrorSeverity::Error,
                 "The bounded streaming source capacity cannot admit another identity.",
                 "Release an existing source or increase the host-configured source capacity.", false);
    const ErrorCodeDescriptor SourceLifecycleUnavailable =
        Describe("world_streaming.source.lifecycle_unavailable", ErrorSeverity::Warning,
                 "The streaming source owner is cancelling or closed to new admission.",
                 "Finish owner retirement or submit the source to a new active owner lifetime.", false);
    const ErrorCodeDescriptor SourceShapeInvalid =
        Describe("world_streaming.source.shape_invalid", ErrorSeverity::Error,
                 "A streaming source shape is malformed or exceeds the bounded evaluation contract.",
                 "Use ordered finite geometry within the declared source range limits.", true);
    const ErrorCodeDescriptor SourceShapeUnsupported =
        Describe("world_streaming.source.shape_unsupported", ErrorSeverity::Error,
                 "The evaluating host does not support the requested streaming source shape.",
                 "Enable the shape capability or submit a supported bounded source shape.", true);
    const ErrorCodeDescriptor SourceDesiredStateInvalid =
        Describe("world_streaming.source.desired_state_invalid", ErrorSeverity::Error,
                 "A streaming source desired state combines residency and retention inconsistently.",
                 "Use releasable retention for Unloaded, or request Loaded or Activated before pinning.", true);
    const ErrorCodeDescriptor SourceDesiredStateUnsupported =
        Describe("world_streaming.source.desired_state_unsupported", ErrorSeverity::Error,
                 "A streaming source desired-state value is not supported by this contract version.",
                 "Use Unloaded, Loaded or Activated residency with Releasable or Pinned retention.", true);
    const ErrorCodeDescriptor SourceReductionInvalid =
        Describe("world_streaming.source_reduction.invalid", ErrorSeverity::Error,
                 "A desired-state reduction context or mandatory limit is malformed.",
                 "Provide a valid partition incarnation and cell with a positive contributor ceiling.", true);
    const ErrorCodeDescriptor SourceReductionIdentityConflict =
        Describe("world_streaming.source_reduction.identity_conflict", ErrorSeverity::Error,
                 "A desired-state reduction repeats one stable source identity.",
                 "Present exactly one admitted immutable revision for each contributing source.", true);
    const ErrorCodeDescriptor SourceReductionCapacityExceeded =
        Describe("world_streaming.source_reduction.capacity_exceeded", ErrorSeverity::Error,
                 "A desired-state reduction exceeds its bounded contributor ceiling.",
                 "Reduce overlapping source demand or increase the host-configured per-cell contributor limit.", false);
    const ErrorCodeDescriptor AuthoringContractInvalid =
        Describe("world_streaming.authoring.contract_invalid", ErrorSeverity::Error,
                 "A world-authoring contract, page request, or authority snapshot is malformed.",
                 "Provide valid partition, page asset and revision identities with consistent bounded owner state.", true);
    const ErrorCodeDescriptor AuthoringVersionUnsupported =
        Describe("world_streaming.authoring.version_unsupported", ErrorSeverity::Error,
                 "The world-authoring contract schema version is unsupported.",
                 "Migrate the authoring contract to the exact version supported by this editor.", true);
    const ErrorCodeDescriptor AuthoringPolicyUnsupported =
        Describe("world_streaming.authoring.policy_unsupported", ErrorSeverity::Error,
                 "The requested authoring granularity or collaboration authority is unsupported.",
                 "Use spatial authoring pages with revision-checked publication.", true);
    const ErrorCodeDescriptor AuthoringIdentityConflict =
        Describe("world_streaming.authoring.identity_conflict", ErrorSeverity::Error,
                 "The authoring request does not match the active page identity or partition.",
                 "Resolve the exact partition and stable page asset before submitting the request again.", false);
    const ErrorCodeDescriptor AuthoringRevisionStale =
        Describe("world_streaming.authoring.revision_stale", ErrorSeverity::Warning,
                 "The authoring request does not replace the expected immutable page revision.",
                 "Reload the current page head and publish its exact non-wrapping successor revision.", false);
    const ErrorCodeDescriptor AuthoringCapacityExceeded =
        Describe("world_streaming.authoring.capacity_exceeded", ErrorSeverity::Error,
                 "The authoring owner cannot admit another page within its configured capacity.",
                 "Close an existing authoring page or select a larger supported owner capacity.", false);
    const ErrorCodeDescriptor AuthoringLifecycleUnavailable =
        Describe("world_streaming.authoring.lifecycle_unavailable", ErrorSeverity::Warning,
                 "The authoring owner is cancelling or closed to page admission.",
                 "Finish retirement or submit the page to a new active authoring owner.", false);
}  // namespace Horo::WorldStreaming::WorldStreamingErrors
