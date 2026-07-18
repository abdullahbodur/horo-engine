#include "NullRenderBackendErrors.h"

namespace Horo::Render::NullBackendErrors
{
namespace
{
const ErrorDomainId Domain{"horo.render"};
[[nodiscard]] ErrorCodeDescriptor Describe(const char *code, const char *summary, const char *remediation,
                                           const bool retryable = false)
{
    return {.domain = Domain,
            .code = ErrorCode{code},
            .defaultSeverity = ErrorSeverity::Error,
            .summary = summary,
            .remediationHint = remediation,
            .retryable = retryable,
            .userActionable = false};
}
} // namespace

const ErrorCodeDescriptor AlreadyInitialized = Describe("render.backend.already_initialized",
                                                        "Renderer backend is already initialized.",
                                                        "Shut down the backend before initializing it again.");
const ErrorCodeDescriptor FrameActive = Describe("render.backend.frame_active", "A renderer frame is active.",
                                                 "Complete or abort the frame before this operation.");
const ErrorCodeDescriptor FrameAlreadyActive = Describe("render.backend.frame_already_active",
                                                        "A renderer frame is already active.",
                                                        "Complete or abort the frame before beginning another.");
const ErrorCodeDescriptor FrameTokenExhausted = Describe("render.backend.frame_token_exhausted",
                                                        "Renderer frame token space was exhausted.",
                                                        "Restart the backend after all queued GPU work is retired.");
const ErrorCodeDescriptor FrameTokenMismatch = Describe("render.backend.frame_token_mismatch",
                                                       "Renderer frame token does not match the active frame.",
                                                       "Use the token returned by the current BeginFrame call.");
const ErrorCodeDescriptor InvalidConfig = Describe("render.backend.invalid_config", "Renderer configuration is invalid.",
                                                   "Use a supported frames-in-flight and presentation configuration.");
const ErrorCodeDescriptor InvalidExecutionPlan = Describe("render.backend.invalid_execution_plan",
                                                          "Renderer execution plan is invalid.",
                                                          "Submit a valid ordered pass plan.");
const ErrorCodeDescriptor InvalidExtent = Describe("render.backend.invalid_extent", "Render extent is invalid.",
                                                   "Use a non-zero supported render extent.");
const ErrorCodeDescriptor InvalidFrameDescriptor = Describe("render.backend.invalid_frame_descriptor",
                                                            "Renderer frame descriptor is invalid.",
                                                            "Provide a valid frame descriptor.");
const ErrorCodeDescriptor NoActiveFrame = Describe("render.backend.no_active_frame", "No renderer frame is active.",
                                                   "Begin a frame before this operation.");
const ErrorCodeDescriptor NotInitialized = Describe("render.backend.not_initialized", "Renderer backend is not initialized.",
                                                    "Initialize the backend before use.");
const ErrorCodeDescriptor UnsupportedPassKind = Describe("render.backend.unsupported_pass_kind",
                                                         "Render pass kind is unsupported.",
                                                         "Submit only pass kinds supported by the backend.");
const ErrorCodeDescriptor PresentationUnsupported = Describe("render.null.presentation_unsupported",
                                                             "Null renderer does not support presentation.",
                                                             "Use a presentation-capable backend or disable presentation.");
} // namespace Horo::Render::NullBackendErrors
