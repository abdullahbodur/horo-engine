#include "PrimitiveMeshErrors.h"

namespace Horo::Runtime::PrimitiveErrors
{
    namespace
    {
        const ErrorDomainId Domain{"horo.runtime.scene.primitives"};
    } // namespace

    const ErrorCodeDescriptor CacheCapacityExceeded{
        .domain = Domain,
        .code = ErrorCode{"primitive.cache_capacity_exceeded"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "Primitive mesh cache capacity was exceeded.",
        .remediationHint = "Release active mesh leases or increase the configured cache budget.",
        .retryable = true,
        .userActionable = false
    };

    const ErrorCodeDescriptor CapacityExceeded{
        .domain = Domain,
        .code = ErrorCode{"primitive.capacity_exceeded"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "Primitive generation capacity was exceeded.",
        .remediationHint = "Reduce tessellation or generated mesh dimensions.",
        .retryable = false,
        .userActionable = false
    };

    const ErrorCodeDescriptor InvalidCapsuleHeight{
        .domain = Domain,
        .code = ErrorCode{"primitive.invalid_capsule_height"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "Capsule height is invalid.",
        .remediationHint = "Use a total height greater than or equal to the capsule diameter.",
        .retryable = false,
        .userActionable = false
    };

    const ErrorCodeDescriptor InvalidDimensions{
        .domain = Domain,
        .code = ErrorCode{"primitive.invalid_dimensions"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "Primitive dimensions are invalid.",
        .remediationHint = "Use finite positive dimensions.",
        .retryable = false,
        .userActionable = false
    };

    const ErrorCodeDescriptor InvalidGeneratedMesh{
        .domain = Domain,
        .code = ErrorCode{"primitive.invalid_generated_mesh"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "Primitive generation produced invalid mesh data.",
        .remediationHint = "Inspect the primitive generator and input descriptor.",
        .retryable = false,
        .userActionable = false
    };

    const ErrorCodeDescriptor InvalidRadius{
        .domain = Domain,
        .code = ErrorCode{"primitive.invalid_radius"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "Primitive radius is invalid.",
        .remediationHint = "Use a finite positive radius.",
        .retryable = false,
        .userActionable = false
    };

    const ErrorCodeDescriptor InvalidSize{
        .domain = Domain,
        .code = ErrorCode{"primitive.invalid_size"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "Primitive size is invalid.",
        .remediationHint = "Use finite positive size components.",
        .retryable = false,
        .userActionable = false
    };

    const ErrorCodeDescriptor InvalidTessellation{
        .domain = Domain,
        .code = ErrorCode{"primitive.invalid_tessellation"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "Primitive tessellation is invalid.",
        .remediationHint = "Use tessellation values within the supported range.",
        .retryable = false,
        .userActionable = false
    };

    const ErrorCodeDescriptor ParameterMismatch{
        .domain = Domain,
        .code = ErrorCode{"primitive.parameter_mismatch"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "Primitive type and parameter payload do not match.",
        .remediationHint = "Provide the parameter type required by the primitive descriptor.",
        .retryable = false,
        .userActionable = false
    };

    const ErrorCodeDescriptor UnsupportedVersion{
        .domain = Domain,
        .code = ErrorCode{"primitive.unsupported_version"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "Primitive mesh descriptor version is unsupported.",
        .remediationHint = "Migrate the descriptor to a supported version.",
        .retryable = false,
        .userActionable = false
    };
} // namespace Horo::Runtime::PrimitiveErrors
