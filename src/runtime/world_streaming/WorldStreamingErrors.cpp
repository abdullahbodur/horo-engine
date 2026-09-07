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
