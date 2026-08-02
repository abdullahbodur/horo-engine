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

    const ErrorCodeDescriptor ProcessLaunchFailed{
        .domain = Domain,
        .code = ErrorCode{"process_launch_failed"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "The external process could not be started.",
        .remediationHint = "Verify that the executable and toolchain are installed and accessible.",
        .retryable = true,
        .userActionable = true,
    };

    const ErrorCodeDescriptor ProcessIoFailed{
        .domain = Domain,
        .code = ErrorCode{"process_io_failed"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "External process output could not be consumed.",
        .remediationHint = "Retry the operation and inspect the host platform diagnostics.",
        .retryable = true,
        .userActionable = false,
    };
} // namespace Horo::PlatformErrors
