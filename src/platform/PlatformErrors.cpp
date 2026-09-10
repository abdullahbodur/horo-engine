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

    const ErrorCodeDescriptor InvalidLifecycleConfiguration{
        .domain = Domain,
        .code = ErrorCode{"android_lifecycle.invalid_configuration"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "The Android lifecycle controller configuration is invalid.",
        .remediationHint = "Provide a valid owner thread and bounded queue capacity.",
        .retryable = false,
        .userActionable = false,
    };

    const ErrorCodeDescriptor InvalidLifecycleGeneration{
        .domain = Domain,
        .code = ErrorCode{"android_lifecycle.invalid_generation"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "An Android lifecycle observation is missing required generation evidence.",
        .remediationHint = "Tag native callbacks with the generation assigned by the host adapter.",
        .retryable = false,
        .userActionable = false,
    };

    const ErrorCodeDescriptor LifecycleGenerationExhausted{
        .domain = Domain,
        .code = ErrorCode{"android_lifecycle.generation_exhausted"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "An Android lifecycle generation counter is exhausted.",
        .remediationHint = "Reject further native resources and terminate this host generation safely.",
        .retryable = false,
        .userActionable = false,
    };

    const ErrorCodeDescriptor InvalidLifecycleTransition{
        .domain = Domain,
        .code = ErrorCode{"android_lifecycle.invalid_transition"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "The Android lifecycle observation is not legal from the committed state.",
        .remediationHint = "Preserve callback order or recreate the affected generation.",
        .retryable = false,
        .userActionable = false,
    };

    const ErrorCodeDescriptor StaleLifecycleGeneration{
        .domain = Domain,
        .code = ErrorCode{"android_lifecycle.stale_generation"},
        .defaultSeverity = ErrorSeverity::Warning,
        .summary = "The Android lifecycle observation refers to a retired generation.",
        .remediationHint = "Discard work and handles retained from the retired Activity or surface.",
        .retryable = false,
        .userActionable = false,
    };

    const ErrorCodeDescriptor LifecycleOwnerThreadRequired{
        .domain = Domain,
        .code = ErrorCode{"android_lifecycle.owner_thread_required"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "Android lifecycle state may be committed only by its application owner thread.",
        .remediationHint = "Enqueue the observation and drain it from the application owner thread.",
        .retryable = true,
        .userActionable = false,
    };

    const ErrorCodeDescriptor LifecycleQueueSaturated{
        .domain = Domain,
        .code = ErrorCode{"android_lifecycle.queue_saturated"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "The bounded Android lifecycle observation queue is full.",
        .remediationHint = "Drain observations at the next owner-thread lifecycle cutoff.",
        .retryable = true,
        .userActionable = false,
    };

    const ErrorCodeDescriptor LifecycleAdmissionClosed{
        .domain = Domain,
        .code = ErrorCode{"android_lifecycle.admission_closed"},
        .defaultSeverity = ErrorSeverity::Warning,
        .summary = "Android lifecycle callback admission is closed for final shutdown.",
        .remediationHint = "Do not enqueue callbacks after final process shutdown begins.",
        .retryable = false,
        .userActionable = false,
    };
}  // namespace Horo::PlatformErrors
