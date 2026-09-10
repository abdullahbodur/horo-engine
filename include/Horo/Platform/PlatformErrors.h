#pragma once

#include "Horo/Foundation/ErrorCode.h"

namespace Horo::PlatformErrors {
    extern const ErrorCodeDescriptor InvalidFormat;
    extern const ErrorCodeDescriptor NotFound;
    extern const ErrorCodeDescriptor ProcessLaunchFailed;
    extern const ErrorCodeDescriptor ProcessIoFailed;
    extern const ErrorCodeDescriptor ConfigurationReadFailed;
    extern const ErrorCodeDescriptor ConfigurationWriteFailed;
    extern const ErrorCodeDescriptor ConfigurationFileTooLarge;
    extern const ErrorCodeDescriptor InvalidLifecycleConfiguration;
    extern const ErrorCodeDescriptor InvalidLifecycleGeneration;
    extern const ErrorCodeDescriptor LifecycleGenerationExhausted;
    extern const ErrorCodeDescriptor InvalidLifecycleTransition;
    extern const ErrorCodeDescriptor StaleLifecycleGeneration;
    extern const ErrorCodeDescriptor LifecycleOwnerThreadRequired;
    extern const ErrorCodeDescriptor LifecycleQueueSaturated;
    extern const ErrorCodeDescriptor LifecycleAdmissionClosed;
}  // namespace Horo::PlatformErrors
