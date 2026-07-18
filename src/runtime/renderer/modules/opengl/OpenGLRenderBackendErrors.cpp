#include "OpenGLRenderBackendErrors.h"

namespace Horo::Render::OpenGLBackendErrors
{
namespace
{
const ErrorDomainId Domain{"horo.render.opengl"};
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
                                                        "OpenGL backend is already initialized.",
                                                        "Shut down the backend before initializing it again.");
const ErrorCodeDescriptor FrameActive = Describe("render.backend.frame_active", "An OpenGL frame is active.",
                                                 "Complete or abort the frame before this operation.");
const ErrorCodeDescriptor FrameAlreadyActive = Describe("render.backend.frame_already_active",
                                                        "An OpenGL frame is already active.",
                                                        "Complete or abort the frame before beginning another.");
const ErrorCodeDescriptor FrameTokenExhausted = Describe("render.backend.frame_token_exhausted",
                                                        "OpenGL frame token space was exhausted.",
                                                        "Restart the backend after queued work is retired.");
const ErrorCodeDescriptor FrameTokenMismatch = Describe("render.backend.frame_token_mismatch",
                                                       "OpenGL frame token does not match the active frame.",
                                                       "Use the token returned by the current BeginFrame call.");
const ErrorCodeDescriptor InvalidConfig = Describe("render.backend.invalid_config", "OpenGL configuration is invalid.",
                                                   "Use a supported frames-in-flight and presentation configuration.");
const ErrorCodeDescriptor InvalidExecutionPlan = Describe("render.backend.invalid_execution_plan",
                                                          "OpenGL execution plan is invalid.",
                                                          "Submit a valid ordered pass plan.");
const ErrorCodeDescriptor InvalidExtent = Describe("render.backend.invalid_extent", "OpenGL render extent is invalid.",
                                                   "Use a non-zero supported render extent.");
const ErrorCodeDescriptor InvalidFrameDescriptor = Describe("render.backend.invalid_frame_descriptor",
                                                            "OpenGL frame descriptor is invalid.",
                                                            "Provide a valid frame descriptor.");
const ErrorCodeDescriptor NoActiveFrame = Describe("render.backend.no_active_frame", "No OpenGL frame is active.",
                                                   "Begin a frame before this operation.");
const ErrorCodeDescriptor NotInitialized = Describe("render.backend.not_initialized", "OpenGL backend is not initialized.",
                                                    "Initialize the backend before use.");
const ErrorCodeDescriptor InvalidRegistration = Describe("render.opengl.invalid_registration",
                                                         "OpenGL backend registration is invalid.",
                                                         "Provide a valid OpenGL runtime and presentation provider.");
const ErrorCodeDescriptor PresentationInUse = Describe("render.opengl.presentation_in_use",
                                                       "OpenGL presentation is already in use.",
                                                       "Release the active presentation lease before creating another.", true);
const ErrorCodeDescriptor UnsupportedPassKind = Describe("render.opengl.unsupported_pass_kind",
                                                         "OpenGL render pass kind is unsupported.",
                                                         "Submit only pass kinds supported by OpenGL.");
} // namespace Horo::Render::OpenGLBackendErrors
