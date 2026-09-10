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
}  // namespace Horo::PlatformErrors
