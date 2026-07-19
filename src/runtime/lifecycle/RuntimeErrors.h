#pragma once

#include "Horo/Foundation/ErrorCode.h"

namespace Horo::Runtime::RuntimeErrors
{
    extern const ErrorCodeDescriptor InvalidSchedulerConfig;
    extern const ErrorCodeDescriptor InvalidLifecycleState;
    extern const ErrorCodeDescriptor NullParticipant;
    extern const ErrorCodeDescriptor Cancelled;
    extern const ErrorCodeDescriptor UnexpectedException;
    extern const ErrorCodeDescriptor PresentationPrerequisitesMissing;
} // namespace Horo::Runtime::RuntimeErrors
