#include "RenderFrontendErrors.h"

namespace Horo::Render::FrontendErrors
{
    namespace
    {
        // Preserve the existing serialized domain until an explicit compatibility migration is approved.
        const ErrorDomainId Domain{"render.frontend"};
    } // namespace

    const ErrorCodeDescriptor AmbiguousPassWorkload{
        .domain = Domain,
        .code = ErrorCode{"render.frontend.ambiguous_pass_workload"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "Render pass workload is ambiguous.",
        .remediationHint = "Provide exactly one supported workload for the pass.",
        .retryable = false,
        .userActionable = false
    };

    const ErrorCodeDescriptor ExecutorChangeDuringFrame{
        .domain = Domain,
        .code = ErrorCode{"render.frontend.executor_change_during_frame"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "Render executor cannot change during an active frame.",
        .remediationHint = "Attach or detach executors between frames.",
        .retryable = false,
        .userActionable = false
    };

    const ErrorCodeDescriptor FrameAlreadyActive{
        .domain = Domain,
        .code = ErrorCode{"render.frontend.frame_already_active"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "A render frame is already active.",
        .remediationHint = "Complete or abort the active frame before beginning another.",
        .retryable = false,
        .userActionable = false
    };

    const ErrorCodeDescriptor FrameAlreadyExecuted{
        .domain = Domain,
        .code = ErrorCode{"render.frontend.frame_already_executed"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "The active render frame was already executed.",
        .remediationHint = "End the frame or begin a new frame before executing again.",
        .retryable = false,
        .userActionable = false
    };

    const ErrorCodeDescriptor FrameException{
        .domain = Domain,
        .code = ErrorCode{"render.frontend.frame_exception"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "Render frame execution raised an exception.",
        .remediationHint = "Inspect backend diagnostics and abort the failed frame.",
        .retryable = true,
        .userActionable = false
    };

    const ErrorCodeDescriptor FrameNotActive{
        .domain = Domain,
        .code = ErrorCode{"render.frontend.frame_not_active"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "No render frame is active.",
        .remediationHint = "Begin a frame before issuing this operation.",
        .retryable = false,
        .userActionable = false
    };

    const ErrorCodeDescriptor FrameNotExecuted{
        .domain = Domain,
        .code = ErrorCode{"render.frontend.frame_not_executed"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "The active render frame has not executed.",
        .remediationHint = "Execute the frame before ending it.",
        .retryable = false,
        .userActionable = false
    };

    const ErrorCodeDescriptor InitializeException{
        .domain = Domain,
        .code = ErrorCode{"render.frontend.initialize_exception"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "Renderer initialization raised an exception.",
        .remediationHint = "Inspect backend initialization diagnostics.",
        .retryable = true,
        .userActionable = false
    };

    const ErrorCodeDescriptor InvalidFrameToken{
        .domain = Domain,
        .code = ErrorCode{"render.frontend.invalid_frame_token"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "Render frame token is invalid or stale.",
        .remediationHint = "Use the token returned for the current active frame.",
        .retryable = false,
        .userActionable = false
    };

    const ErrorCodeDescriptor InvalidStaticMeshPass{
        .domain = Domain,
        .code = ErrorCode{"render.frontend.invalid_static_mesh_pass"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "Static mesh pass descriptor is invalid.",
        .remediationHint = "Provide a valid target, extent, camera, and scene workload.",
        .retryable = false,
        .userActionable = false
    };

    const ErrorCodeDescriptor InvalidTargetExtent{
        .domain = Domain,
        .code = ErrorCode{"render.frontend.invalid_target_extent"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "Render target extent is invalid.",
        .remediationHint = "Use a non-zero extent supported by the active backend.",
        .retryable = false,
        .userActionable = false
    };

    const ErrorCodeDescriptor ResizeDuringFrame{
        .domain = Domain,
        .code = ErrorCode{"render.frontend.resize_during_frame"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "Render target cannot resize during an active frame.",
        .remediationHint = "Resize the target between frames.",
        .retryable = false,
        .userActionable = false
    };

    const ErrorCodeDescriptor ResizeException{
        .domain = Domain,
        .code = ErrorCode{"render.frontend.resize_exception"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "Render target resize raised an exception.",
        .remediationHint = "Inspect backend resize diagnostics.",
        .retryable = true,
        .userActionable = false
    };

    const ErrorCodeDescriptor StaleRenderTarget{
        .domain = Domain,
        .code = ErrorCode{"render.frontend.stale_render_target"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "Render target handle is stale.",
        .remediationHint = "Acquire the current target handle before submitting work.",
        .retryable = false,
        .userActionable = false
    };

    const ErrorCodeDescriptor StaticMeshExecutorAlreadyAttached{
        .domain = Domain,
        .code = ErrorCode{"render.frontend.static_mesh_executor_already_attached"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "Static mesh executor is already attached.",
        .remediationHint = "Detach the current executor before attaching another.",
        .retryable = false,
        .userActionable = false
    };

    const ErrorCodeDescriptor StaticMeshExecutorMissing{
        .domain = Domain,
        .code = ErrorCode{"render.frontend.static_mesh_executor_missing"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "Static mesh executor is not attached.",
        .remediationHint = "Attach a compatible executor before submitting static mesh work.",
        .retryable = false,
        .userActionable = false
    };

    const ErrorCodeDescriptor TargetReleaseDuringFrame{
        .domain = Domain,
        .code = ErrorCode{"render.frontend.target_release_during_frame"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "Render target cannot be released during an active frame.",
        .remediationHint = "Release targets only after the active frame completes.",
        .retryable = false,
        .userActionable = false
    };
} // namespace Horo::Render::FrontendErrors
