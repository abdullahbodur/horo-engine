#include "Horo/WorldStreaming/WorldStreamingErrors.h"

namespace Horo::WorldStreaming::WorldStreamingErrors {
    namespace {
        const ErrorDomainId Domain{"horo.world_streaming"};
    }

    const ErrorCodeDescriptor IdentityInvalid{
        .domain = Domain,
        .code = ErrorCode{"world_streaming.identity.invalid"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "A world-partition identity uses its reserved invalid representation.",
        .remediationHint = "Use an identity issued by the manifest, authoring boundary, or active partition authority.",
        .retryable = false,
        .userActionable = true,
    };
    const ErrorCodeDescriptor SerializedIdentityInvalid{
        .domain = Domain,
        .code = ErrorCode{"world_streaming.identity.serialized_invalid"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "A serialized world-partition identity is malformed or reserved.",
        .remediationHint = "Rebuild the world index or source descriptor with canonical little-endian identity bytes.",
        .retryable = false,
        .userActionable = true,
    };
    const ErrorCodeDescriptor GenerationExhausted{
        .domain = Domain,
        .code = ErrorCode{"world_streaming.generation.exhausted"},
        .defaultSeverity = ErrorSeverity::Critical,
        .summary = "A world-partition epoch or cell generation cannot advance without wrapping.",
        .remediationHint = "Retire the exhausted incarnation or slot; never wrap or reuse an issued streaming generation.",
        .retryable = false,
        .userActionable = false,
    };
    const ErrorCodeDescriptor QuantizationPolicyInvalid{
        .domain = Domain,
        .code = ErrorCode{"world_streaming.quantization.policy_invalid"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "The world-cell quantization policy has invalid size, bounds, or LOD limits.",
        .remediationHint = "Use a positive millimeter cell size, ordered int32 grid bounds, and at least one supported LOD.",
        .retryable = false,
        .userActionable = true,
    };
    const ErrorCodeDescriptor CoordinateOutOfRange{
        .domain = Domain,
        .code = ErrorCode{"world_streaming.quantization.coordinate_out_of_range"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "The world coordinate cannot be translated into the grid's signed 64-bit relative frame.",
        .remediationHint = "Use a grid origin and world coordinate whose exact millimeter difference is representable.",
        .retryable = false,
        .userActionable = true,
    };
    const ErrorCodeDescriptor LodUnsupported{
        .domain = Domain,
        .code = ErrorCode{"world_streaming.quantization.lod_unsupported"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "The requested world-cell LOD is not declared by the grid policy.",
        .remediationHint = "Request a LOD below the validated manifest lodLevels value.",
        .retryable = false,
        .userActionable = true,
    };
    const ErrorCodeDescriptor CellOutOfBounds{
        .domain = Domain,
        .code = ErrorCode{"world_streaming.quantization.cell_out_of_bounds"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "The quantized world cell is outside the manifest's inclusive grid bounds.",
        .remediationHint = "Reject the spatial request or use a validated partition whose bounds contain the coordinate.",
        .retryable = false,
        .userActionable = false,
    };
}  // namespace Horo::WorldStreaming::WorldStreamingErrors
