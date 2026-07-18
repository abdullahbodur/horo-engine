#include "RenderFrontendErrors.h"

namespace Horo::Render::FrontendErrors {
    namespace {
        // Preserve the existing serialized domain until an explicit compatibility migration is approved.
        const ErrorDomainId Domain{"render.frontend"};

        [[nodiscard]] ErrorCodeDescriptor Describe(const char *code, const char *summary, const char *remediation,
                                                   const bool retryable = false) {
            return ErrorCodeDescriptor{
                .domain = Domain,
                .code = ErrorCode{code},
                .defaultSeverity = ErrorSeverity::Error,
                .summary = summary,
                .remediationHint = remediation,
                .retryable = retryable,
                .userActionable = false
            };
        }
    } // namespace

    const ErrorCodeDescriptor AmbiguousPassWorkload =
            Describe("render.frontend.ambiguous_pass_workload", "Render pass workload is ambiguous.",
                     "Provide exactly one supported workload for the pass.");
    const ErrorCodeDescriptor ExecutorChangeDuringFrame =
            Describe("render.frontend.executor_change_during_frame",
                     "Render executor cannot change during an active frame.",
                     "Attach or detach executors between frames.");
    const ErrorCodeDescriptor FrameAlreadyActive =
            Describe("render.frontend.frame_already_active", "A render frame is already active.",
                     "Complete or abort the active frame before beginning another.");
    const ErrorCodeDescriptor FrameAlreadyExecuted =
            Describe("render.frontend.frame_already_executed", "The active render frame was already executed.",
                     "End the frame or begin a new frame before executing again.");
    const ErrorCodeDescriptor FrameException =
            Describe("render.frontend.frame_exception", "Render frame execution raised an exception.",
                     "Inspect backend diagnostics and abort the failed frame.", true);
    const ErrorCodeDescriptor FrameNotActive =
            Describe("render.frontend.frame_not_active", "No render frame is active.",
                     "Begin a frame before issuing this operation.");
    const ErrorCodeDescriptor FrameNotExecuted =
            Describe("render.frontend.frame_not_executed", "The active render frame has not executed.",
                     "Execute the frame before ending it.");
    const ErrorCodeDescriptor InitializeException =
            Describe("render.frontend.initialize_exception", "Renderer initialization raised an exception.",
                     "Inspect backend initialization diagnostics.", true);
    const ErrorCodeDescriptor InvalidFrameToken =
            Describe("render.frontend.invalid_frame_token", "Render frame token is invalid or stale.",
                     "Use the token returned for the current active frame.");
    const ErrorCodeDescriptor InvalidStaticMeshPass =
            Describe("render.frontend.invalid_static_mesh_pass", "Static mesh pass descriptor is invalid.",
                     "Provide a valid target, extent, camera, and scene workload.");
    const ErrorCodeDescriptor InvalidTargetExtent =
            Describe("render.frontend.invalid_target_extent", "Render target extent is invalid.",
                     "Use a non-zero extent supported by the active backend.");
    const ErrorCodeDescriptor ResizeDuringFrame =
            Describe("render.frontend.resize_during_frame", "Render target cannot resize during an active frame.",
                     "Resize the target between frames.");
    const ErrorCodeDescriptor ResizeException =
            Describe("render.frontend.resize_exception", "Render target resize raised an exception.",
                     "Inspect backend resize diagnostics.", true);
    const ErrorCodeDescriptor StaleRenderTarget =
            Describe("render.frontend.stale_render_target", "Render target handle is stale.",
                     "Acquire the current target handle before submitting work.");
    const ErrorCodeDescriptor StaticMeshExecutorAlreadyAttached =
            Describe("render.frontend.static_mesh_executor_already_attached",
                     "Static mesh executor is already attached.",
                     "Detach the current executor before attaching another.");
    const ErrorCodeDescriptor StaticMeshExecutorMissing =
            Describe("render.frontend.static_mesh_executor_missing", "Static mesh executor is not attached.",
                     "Attach a compatible executor before submitting static mesh work.");
    const ErrorCodeDescriptor TargetReleaseDuringFrame =
            Describe("render.frontend.target_release_during_frame",
                     "Render target cannot be released during an active frame.",
                     "Release targets only after the active frame completes.");
} // namespace Horo::Render::FrontendErrors
