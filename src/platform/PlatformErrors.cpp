#include "Horo/Platform/PlatformErrors.h"

namespace Horo::PlatformErrors
{
    const ErrorDomainId Domain{"horo.platform"};

    const ErrorCodeDescriptor InvalidFormat{
        .domain = Domain,
        .code = ErrorCode{"invalid_format"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "The dynamic library format is invalid or unsupported.",
        .remediationHint = "Ensure the plugin is compiled for this platform.",
        .retryable = false,
        .userActionable = true,
    };

    const ErrorCodeDescriptor NotFound{
        .domain = Domain,
        .code = ErrorCode{"not_found"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "The dynamic library could not be found.",
        .remediationHint = "Check if the file exists.",
        .retryable = false,
        .userActionable = true,
    };
} // namespace Horo::PlatformErrors
