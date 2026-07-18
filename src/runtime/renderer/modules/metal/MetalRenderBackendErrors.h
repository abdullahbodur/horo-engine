#pragma once

#include "Horo/Foundation/ErrorCode.h"

namespace Horo::Render::MetalBackendErrors
{
extern const ErrorCodeDescriptor AlreadyInitialized;
extern const ErrorCodeDescriptor FrameActive;
extern const ErrorCodeDescriptor FrameAlreadyActive;
extern const ErrorCodeDescriptor FrameTokenExhausted;
extern const ErrorCodeDescriptor FrameTokenMismatch;
extern const ErrorCodeDescriptor InvalidConfig;
extern const ErrorCodeDescriptor InvalidExecutionPlan;
extern const ErrorCodeDescriptor InvalidExtent;
extern const ErrorCodeDescriptor InvalidFrameDescriptor;
extern const ErrorCodeDescriptor NoActiveFrame;
extern const ErrorCodeDescriptor NotInitialized;
extern const ErrorCodeDescriptor PresentationInUse;
extern const ErrorCodeDescriptor UnsupportedFramesInFlight;
extern const ErrorCodeDescriptor UnsupportedPassKind;
} // namespace Horo::Render::MetalBackendErrors
