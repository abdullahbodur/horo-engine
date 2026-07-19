#include "RuntimeErrors.h"

namespace Horo::Runtime::RuntimeErrors
{
    namespace
    {
        constexpr auto kError = ErrorSeverity::Error;
        const ErrorDomainId kDomain{"horo.runtime"};
    } // namespace

    const ErrorCodeDescriptor InvalidSchedulerConfig{
        kDomain, ErrorCode{"runtime.scheduler.invalid_config"}, kError,
        "The frame scheduler configuration is invalid.",
        "Use positive fixed-step, frame-delta, and catch-up limits."
    };
    const ErrorCodeDescriptor InvalidLifecycleState{
        kDomain, ErrorCode{"runtime.lifecycle.invalid_state"}, kError,
        "The runtime lifecycle operation is invalid in its current state.",
        "Follow the documented lifecycle state transitions."
    };
    const ErrorCodeDescriptor NullParticipant{
        kDomain, ErrorCode{"runtime.lifecycle.null_participant"}, kError,
        "A null runtime participant cannot be registered.",
        "Supply an owned runtime lifecycle participant."
    };
    const ErrorCodeDescriptor Cancelled{
        kDomain, ErrorCode{"runtime.host.cancelled"}, ErrorSeverity::Info,
        "Runtime execution was cancelled.", "Complete orderly host shutdown."
    };
    const ErrorCodeDescriptor UnexpectedException{
        kDomain, ErrorCode{"runtime.lifecycle.unexpected_exception"}, ErrorSeverity::Critical,
        "A runtime participant threw an unexpected exception.", "Fix the participant to return a typed Result instead."
    };
    const ErrorCodeDescriptor PresentationPrerequisitesMissing{
        kDomain, ErrorCode{"runtime.scheduler.presentation_prerequisites_missing"}, ErrorSeverity::Critical,
        "Presentation was reached without successful extraction, execution, and GUI phases.",
        "Preserve canonical phase ordering and completion tracking."
    };
} // namespace Horo::Runtime::RuntimeErrors
