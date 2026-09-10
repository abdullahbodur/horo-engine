#include "Horo/Platform/PlatformErrors.h"

namespace Horo::PlatformErrors {
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

    const ErrorCodeDescriptor ConfigurationReadFailed{
        .domain = Domain,
        .code = ErrorCode{"configuration_read_failed"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "The configuration document could not be read.",
        .remediationHint = "Verify the path and file permissions.",
        .retryable = true,
        .userActionable = true,
    };

    const ErrorCodeDescriptor ConfigurationWriteFailed{
        .domain = Domain,
        .code = ErrorCode{"configuration_write_failed"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "The configuration document could not be published durably.",
        .remediationHint = "Verify storage availability and retry.",
        .retryable = true,
        .userActionable = true,
    };

    const ErrorCodeDescriptor ConfigurationFileTooLarge{
        .domain = Domain,
        .code = ErrorCode{"configuration_file_too_large"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "The configuration document exceeds its input limit.",
        .remediationHint = "Reduce the configuration document size.",
        .retryable = false,
        .userActionable = true,
    };
}  // namespace Horo::PlatformErrors
