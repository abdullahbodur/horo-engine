#include "MetalRenderBackendErrors.h"

namespace Horo::Render::MetalBackendErrors
{
namespace
{
const ErrorDomainId Domain{"horo.render.metal"};
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
                                                        "Metal backend is already initialized.",
                                                        "Shut down the backend before initializing it again.");
const ErrorCodeDescriptor FrameActive = Describe("render.backend.frame_active", "A Metal frame is active.",
                                                 "Complete or abort the frame before this operation.");
const ErrorCodeDescriptor FrameAlreadyActive = Describe("render.backend.frame_already_active",
                                                        "A Metal frame is already active.",
                                                        "Complete or abort the frame before beginning another.");
const ErrorCodeDescriptor FrameTokenExhausted = Describe("render.backend.frame_token_exhausted",
                                                        "Metal frame token space was exhausted.",
                                                        "Restart the backend after queued work is retired.");
const ErrorCodeDescriptor FrameTokenMismatch = Describe("render.backend.frame_token_mismatch",
                                                       "Metal frame token does not match the active frame.",
                                                       "Use the token returned by the current BeginFrame call.");
const ErrorCodeDescriptor InvalidConfig = Describe("render.backend.invalid_config", "Metal configuration is invalid.",
                                                   "Use a supported frames-in-flight and presentation configuration.");
const ErrorCodeDescriptor InvalidExecutionPlan = Describe("render.backend.invalid_execution_plan",
                                                          "Metal execution plan is invalid.",
                                                          "Submit a valid ordered pass plan.");
const ErrorCodeDescriptor InvalidExtent = Describe("render.backend.invalid_extent", "Metal render extent is invalid.",
                                                   "Use a non-zero supported render extent.");
const ErrorCodeDescriptor InvalidFrameDescriptor = Describe("render.backend.invalid_frame_descriptor",
                                                            "Metal frame descriptor is invalid.",
                                                            "Provide a valid frame descriptor.");
const ErrorCodeDescriptor NoActiveFrame = Describe("render.backend.no_active_frame", "No Metal frame is active.",
                                                   "Begin a frame before this operation.");
const ErrorCodeDescriptor NotInitialized = Describe("render.backend.not_initialized", "Metal backend is not initialized.",
                                                    "Initialize the backend before use.");
const ErrorCodeDescriptor PresentationInUse = Describe("render.metal.presentation_in_use",
                                                       "Metal presentation is already in use.",
                                                       "Release the active presentation lease before creating another.", true);
const ErrorCodeDescriptor UnsupportedFramesInFlight = Describe("render.metal.unsupported_frames_in_flight",
                                                               "Metal frames-in-flight configuration is unsupported.",
                                                               "Use a frames-in-flight count supported by Metal.");
const ErrorCodeDescriptor UnsupportedPassKind = Describe("render.metal.unsupported_pass_kind",
                                                         "Metal render pass kind is unsupported.",
                                                         "Submit only pass kinds supported by Metal.");
} // namespace Horo::Render::MetalBackendErrors
