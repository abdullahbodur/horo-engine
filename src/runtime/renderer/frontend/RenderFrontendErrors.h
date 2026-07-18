#pragma once

#include "Horo/Foundation/ErrorCode.h"

namespace Horo::Render::FrontendErrors {
    extern const ErrorCodeDescriptor AmbiguousPassWorkload;
    extern const ErrorCodeDescriptor ExecutorChangeDuringFrame;
    extern const ErrorCodeDescriptor FrameAlreadyActive;
    extern const ErrorCodeDescriptor FrameAlreadyExecuted;
    extern const ErrorCodeDescriptor FrameException;
    extern const ErrorCodeDescriptor FrameNotActive;
    extern const ErrorCodeDescriptor FrameNotExecuted;
    extern const ErrorCodeDescriptor InitializeException;
    extern const ErrorCodeDescriptor InvalidFrameToken;
    extern const ErrorCodeDescriptor InvalidStaticMeshPass;
    extern const ErrorCodeDescriptor InvalidTargetExtent;
    extern const ErrorCodeDescriptor ResizeDuringFrame;
    extern const ErrorCodeDescriptor ResizeException;
    extern const ErrorCodeDescriptor StaleRenderTarget;
    extern const ErrorCodeDescriptor StaticMeshExecutorAlreadyAttached;
    extern const ErrorCodeDescriptor StaticMeshExecutorMissing;
    extern const ErrorCodeDescriptor TargetReleaseDuringFrame;
} // namespace Horo::Render::FrontendErrors
