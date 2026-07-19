#include "MetalRenderBackendErrors.h"

namespace Horo::Render::MetalBackendErrors
{
    namespace
    {
        const ErrorDomainId Domain{"horo.render.metal"};
    } // namespace

    const ErrorCodeDescriptor AlreadyInitialized{
        .domain = Domain,
        .code = ErrorCode{"render.backend.already_initialized"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "Metal backend is already initialized.",
        .remediationHint = "Shut down the backend before initializing it again.",
        .retryable = false,
        .userActionable = false
    };

    const ErrorCodeDescriptor FrameActive{
        .domain = Domain,
        .code = ErrorCode{"render.backend.frame_active"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "A Metal frame is active.",
        .remediationHint = "Complete or abort the frame before this operation.",
        .retryable = false,
        .userActionable = false
    };

    const ErrorCodeDescriptor FrameAlreadyActive{
        .domain = Domain,
        .code = ErrorCode{"render.backend.frame_already_active"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "A Metal frame is already active.",
        .remediationHint = "Complete or abort the frame before beginning another.",
        .retryable = false,
        .userActionable = false
    };

    const ErrorCodeDescriptor FrameTokenExhausted{
        .domain = Domain,
        .code = ErrorCode{"render.backend.frame_token_exhausted"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "Metal frame token space was exhausted.",
        .remediationHint = "Restart the backend after queued work is retired.",
        .retryable = false,
        .userActionable = false
    };

    const ErrorCodeDescriptor FrameTokenMismatch{
        .domain = Domain,
        .code = ErrorCode{"render.backend.frame_token_mismatch"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "Metal frame token does not match the active frame.",
        .remediationHint = "Use the token returned by the current BeginFrame call.",
        .retryable = false,
        .userActionable = false
    };

    const ErrorCodeDescriptor InvalidConfig{
        .domain = Domain,
        .code = ErrorCode{"render.backend.invalid_config"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "Metal configuration is invalid.",
        .remediationHint = "Use a supported frames-in-flight and presentation configuration.",
        .retryable = false,
        .userActionable = false
    };

    const ErrorCodeDescriptor InvalidExecutionPlan{
        .domain = Domain,
        .code = ErrorCode{"render.backend.invalid_execution_plan"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "Metal execution plan is invalid.",
        .remediationHint = "Submit a valid ordered pass plan.",
        .retryable = false,
        .userActionable = false
    };

    const ErrorCodeDescriptor InvalidExtent{
        .domain = Domain,
        .code = ErrorCode{"render.backend.invalid_extent"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "Metal render extent is invalid.",
        .remediationHint = "Use a non-zero supported render extent.",
        .retryable = false,
        .userActionable = false
    };

    const ErrorCodeDescriptor InvalidFrameDescriptor{
        .domain = Domain,
        .code = ErrorCode{"render.backend.invalid_frame_descriptor"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "Metal frame descriptor is invalid.",
        .remediationHint = "Provide a valid frame descriptor.",
        .retryable = false,
        .userActionable = false
    };

    const ErrorCodeDescriptor NoActiveFrame{
        .domain = Domain,
        .code = ErrorCode{"render.backend.no_active_frame"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "No Metal frame is active.",
        .remediationHint = "Begin a frame before this operation.",
        .retryable = false,
        .userActionable = false
    };

    const ErrorCodeDescriptor NotInitialized{
        .domain = Domain,
        .code = ErrorCode{"render.backend.not_initialized"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "Metal backend is not initialized.",
        .remediationHint = "Initialize the backend before use.",
        .retryable = false,
        .userActionable = false
    };

    const ErrorCodeDescriptor PresentationInUse{
        .domain = Domain,
        .code = ErrorCode{"render.metal.presentation_in_use"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "Metal presentation is already in use.",
        .remediationHint = "Release the active presentation lease before creating another.",
        .retryable = true,
        .userActionable = false
    };

    const ErrorCodeDescriptor UnsupportedFramesInFlight{
        .domain = Domain,
        .code = ErrorCode{"render.metal.unsupported_frames_in_flight"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "Metal frames-in-flight configuration is unsupported.",
        .remediationHint = "Use a frames-in-flight count supported by Metal.",
        .retryable = false,
        .userActionable = false
    };

    const ErrorCodeDescriptor UnsupportedPassKind{
        .domain = Domain,
        .code = ErrorCode{"render.metal.unsupported_pass_kind"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "Metal render pass kind is unsupported.",
        .remediationHint = "Submit only pass kinds supported by Metal.",
        .retryable = false,
        .userActionable = false
    };
} // namespace Horo::Render::MetalBackendErrors
