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

    const ErrorCodeDescriptor OriginFrameInvalid =
        Describe("world_streaming.origin_frame.invalid", ErrorSeverity::Error,
                 "An origin-frame binding or externally supplied local coordinate is malformed.",
                 "Provide non-zero typed frame identities and finite millimeter-aligned local coordinates.", true);
    const ErrorCodeDescriptor OriginFrameStale =
        Describe("world_streaming.origin_frame.stale", ErrorSeverity::Warning,
                 "An origin-frame candidate, local coordinate, or lease no longer names the active generation.",
                 "Capture the current origin-frame publication and regenerate the local coordinate before retrying.", false);
    const ErrorCodeDescriptor OriginFrameRangeExceeded =
        Describe("world_streaming.origin_frame.range_exceeded", ErrorSeverity::Error,
                 "A global/local conversion exceeds the supported local frame or signed global coordinate range.",
                 "Rebase to a nearer canonical origin and keep every local axis inside the declared half-extent.", false);
    const ErrorCodeDescriptor OriginFramePrecisionLoss =
        Describe("world_streaming.origin_frame.precision_loss", ErrorSeverity::Error,
                 "An externally supplied local coordinate cannot preserve canonical millimeter precision.",
                 "Provide finite local meters aligned to canonical millimeters or derive the value from an origin frame.", true);
    const ErrorCodeDescriptor OriginFrameLifecycleUnavailable =
        Describe("world_streaming.origin_frame.lifecycle_unavailable", ErrorSeverity::Warning,
                 "The origin-frame owner is closed or has no staged replacement to publish.",
                 "Use the active owner lifecycle and stage a validated successor before publication.", false);
    const ErrorCodeDescriptor OriginFrameStorageUnavailable =
        Describe("world_streaming.origin_frame.storage_unavailable", ErrorSeverity::Error,
                 "Storage required for an origin-frame owner or replacement lease is unavailable.",
                 "Release retained frame leases or retry at a later owner safe point.", false);

    const ErrorCodeDescriptor DiagnosticProjectionInvalid =
        Describe("world_streaming.diagnostic_projection.invalid", ErrorSeverity::Error,
                 "A World Streaming diagnostic snapshot contains malformed or incoherent authority or decision facts.",
                 "Capture one complete authority-safe-point projection with valid owner, queue, budget and typed decision facts.", true);
    const ErrorCodeDescriptor DiagnosticProjectionStale =
        Describe("world_streaming.diagnostic_projection.stale", ErrorSeverity::Warning,
                 "A World Streaming diagnostic row no longer belongs to the captured authority or snapshot revision.",
                 "Discard the stale row and capture the current owner, partition epoch, fence and revisions together.", false);
    const ErrorCodeDescriptor DiagnosticProjectionUnsupported =
        Describe("world_streaming.diagnostic_projection.unsupported", ErrorSeverity::Error,
                 "A World Streaming diagnostic projection contains an unsupported typed state, decision, severity or reason.",
                 "Use only the lifecycle, residency, decision and terminal-reason values declared by this contract version.", true);
    const ErrorCodeDescriptor DiagnosticProjectionIdentityConflict =
        Describe("world_streaming.diagnostic_projection.identity_conflict", ErrorSeverity::Error,
                 "A World Streaming diagnostic projection repeats a stable source, cell, failure, event, sequence or context identity.",
                 "Publish unique canonical rows and context keys for each captured authority decision.", true);
    const ErrorCodeDescriptor DiagnosticProjectionCapacityExceeded =
        Describe("world_streaming.diagnostic_projection.capacity_exceeded", ErrorSeverity::Warning,
                 "A World Streaming diagnostic projection exceeds an admitted bounded row or queue capacity.",
                 "Reduce captured rows or configure a larger supported diagnostic ceiling before capture.", false);

    const ErrorCodeDescriptor CellStateInvalid =
        Describe("world_streaming.cell_state.invalid", ErrorSeverity::Error,
                 "A World Streaming cell-state ledger, owner, fence, or record is malformed.",
                 "Use the exact valid partition owner, operation handle and bounded ledger configuration.", true);
    const ErrorCodeDescriptor CellStateUnresolved =
        Describe("world_streaming.cell_state.unresolved", ErrorSeverity::Info,
                 "No residency record exists for the exact mounted cell attempt.",
                 "Resolve the manifest cell and admit a fenced load operation before querying runtime residency.", false);
    const ErrorCodeDescriptor CellStateStale =
        Describe("world_streaming.cell_state.stale", ErrorSeverity::Warning,
                 "A cell-state request names foreign ownership or a stale attempt generation.",
                 "Route the canonical operation to its exact partition authority and retained generation.", false);
    const ErrorCodeDescriptor CellStateUnsupported =
        Describe("world_streaming.cell_state.unsupported", ErrorSeverity::Error,
                 "A cell operation phase or outcome cannot be projected onto canonical residency.",
                 "Use a canonical admitted operation snapshot and keep provider barriers separate from residency.", true);
    const ErrorCodeDescriptor CellStateTransitionInvalid =
        Describe("world_streaming.cell_state.transition_invalid", ErrorSeverity::Error,
                 "A projected cell residency transition is illegal from the current state.",
                 "Follow the fenced Unloaded, Loading, Resident, Active, Evicting and Failed lifecycle.", false);
    const ErrorCodeDescriptor CellStateCapacityExceeded =
        Describe("world_streaming.cell_state.capacity_exceeded", ErrorSeverity::Warning,
                 "The cell-state ledger cannot retain another current or retiring attempt.",
                 "Finish exact retirement acknowledgement or explicitly admit a larger bounded ledger before mounting.", false);
    const ErrorCodeDescriptor CellStateLifecycleUnavailable =
        Describe("world_streaming.cell_state.lifecycle_unavailable", ErrorSeverity::Warning,
                 "The cell-state ledger no longer accepts loading or publication.",
                 "During shutdown, reconcile only retirement and cleanup-complete terminal snapshots.", false);
    const ErrorCodeDescriptor RuntimeCompositionInvalid =
        Describe("world_streaming.runtime_composition.invalid", ErrorSeverity::Error, "A World Streaming runtime composition is malformed.",
                 "Provide one explicit planner, asset provider and Scene runtime plus bounded feature adapters and a valid scheduler.",
                 true);
    const ErrorCodeDescriptor RuntimeCompositionIdentityConflict =
        Describe("world_streaming.runtime_composition.identity_conflict", ErrorSeverity::Error,
                 "A World Streaming runtime composition repeats a service identity.",
                 "Assign every borrowed service binding one unique stable identity.", true);
    const ErrorCodeDescriptor RuntimeCompositionCapacityExceeded =
        Describe("world_streaming.runtime_composition.capacity_exceeded", ErrorSeverity::Error,
                 "World Streaming runtime service bindings exceed their configured ceiling.",
                 "Reduce feature adapters or choose an explicitly larger supported ceiling before composition.", true);
    const ErrorCodeDescriptor RuntimeCompositionRevisionStale =
        Describe("world_streaming.runtime_composition.revision_stale", ErrorSeverity::Warning,
                 "A World Streaming runtime composition command names stale ownership or revision facts.",
                 "Capture the active owner and revision before lookup, replacement, cancellation or shutdown.", false);
    const ErrorCodeDescriptor RuntimeCompositionLifecycleUnavailable =
        Describe("world_streaming.runtime_composition.lifecycle_unavailable", ErrorSeverity::Warning,
                 "The World Streaming runtime composition lifecycle cannot accept this operation.",
                 "Finish retained scheduler work before replacement, or create a new composition after shutdown.", false);

    const ErrorCodeDescriptor FallbackProviderInvalid =
        Describe("world_streaming.fallback_provider.invalid", ErrorSeverity::Error,
                 "A fallback streaming provider descriptor is malformed.",
                 "Provide one valid owner/revision and exactly one cell for SingleCell or no cell for Null.", true);
    const ErrorCodeDescriptor FallbackProviderUnsupported =
        Describe("world_streaming.fallback_provider.unsupported", ErrorSeverity::Error,
                 "The fallback streaming provider mode is unsupported.", "Select the declared SingleCell or Null composition explicitly.",
                 true);
    const ErrorCodeDescriptor FallbackProviderCapacityExceeded =
        Describe("world_streaming.fallback_provider.capacity_exceeded", ErrorSeverity::Error,
                 "The configured fallback provider cannot publish its single cell within the mandatory ceiling.",
                 "Allow one published cell or select the explicit Null composition.", true);
    const ErrorCodeDescriptor FallbackProviderStale =
        Describe("world_streaming.fallback_provider.stale", ErrorSeverity::Warning,
                 "A fallback provider operation names a stale owner or configuration revision.",
                 "Capture the current owner lifetime and issue a strictly newer replacement revision.", false);
    const ErrorCodeDescriptor FallbackProviderLifecycleUnavailable =
        Describe("world_streaming.fallback_provider.lifecycle_unavailable", ErrorSeverity::Warning,
                 "The fallback provider lifecycle no longer accepts this operation.",
                 "Finish shutdown or create a new provider for the next owner lifetime.", false);

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
    const ErrorCodeDescriptor SchedulerAdmissionInvalid =
        Describe("world_streaming.scheduler.admission_invalid", ErrorSeverity::Error,
                 "A scheduler admission ledger, request, or reservation is malformed.",
                 "Use a valid ledger owner, positive bounded limits, and a valid queued operation with a positive capacity charge.", true);
    const ErrorCodeDescriptor SchedulerCapacityExceeded =
        Describe("world_streaming.scheduler.capacity_exceeded", ErrorSeverity::Warning,
                 "The scheduler cannot reserve the requested operation count or generic capacity.",
                 "Wait for an admitted operation to reach acknowledged terminal retirement before retrying.", false);
    const ErrorCodeDescriptor SchedulerReservationConflict =
        Describe("world_streaming.scheduler.reservation_conflict", ErrorSeverity::Error,
                 "The cell operation already owns a reservation in this scheduler ledger.",
                 "Reuse the retained reservation instead of admitting the exact operation twice.", false);
    const ErrorCodeDescriptor SchedulerReservationStale =
        Describe("world_streaming.scheduler.reservation_stale", ErrorSeverity::Warning,
                 "A scheduler command does not name the exact owner-scoped reservation and operation fence.",
                 "Route the command to the owning ledger with its exact reservation token.", false);
    const ErrorCodeDescriptor SchedulerLifecycleUnavailable =
        Describe("world_streaming.scheduler.lifecycle_unavailable", ErrorSeverity::Warning,
                 "Scheduler admission is draining, closed, or waiting for an operation to retire.",
                 "Do not admit during shutdown and retain capacity until exact canonical operations become terminal.", false);
    const ErrorCodeDescriptor BudgetModelInvalid =
        Describe("world_streaming.budget.model_invalid", ErrorSeverity::Error,
                 "A multidimensional budget vector, policy, request, or evaluation context is malformed.",
                 "Provide every supported dimension exactly once with valid limits, revisions, timing, and positive requested work.", true);
    const ErrorCodeDescriptor BudgetDimensionUnsupported =
        Describe("world_streaming.budget.dimension_unsupported", ErrorSeverity::Error,
                 "A budget vector or policy names an unsupported resource dimension.",
                 "Use exactly the typed dimensions declared by this world-streaming contract version.", true);
    const ErrorCodeDescriptor BudgetRevisionStale =
        Describe("world_streaming.budget.revision_stale", ErrorSeverity::Warning,
                 "A budget policy or usage sample revision no longer matches current authority state.",
                 "Capture the current immutable policy and usage sample before retrying evaluation.", false);
    const ErrorCodeDescriptor BudgetSampleInvalid =
        Describe("world_streaming.budget.sample_invalid", ErrorSeverity::Error,
                 "A budget usage sample has malformed monotonic window timing.",
                 "Use a non-negative window start and an observation inside the policy's positive half-open sampling window.", true);
    const ErrorCodeDescriptor BudgetSampleStale =
        Describe("world_streaming.budget.sample_stale", ErrorSeverity::Warning,
                 "A budget usage sample belongs to an earlier completed sampling window.",
                 "Capture a current deterministic usage sample before evaluating new work.", false);
    const ErrorCodeDescriptor BudgetCapacityExceeded =
        Describe("world_streaming.budget.capacity_exceeded", ErrorSeverity::Warning,
                 "Projected usage overflows or exceeds an independent hard resource limit.",
                 "Defer work, retire charged resources, or select a validated policy that can contain the request.", false);
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
