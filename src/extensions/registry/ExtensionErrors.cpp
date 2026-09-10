#include "Horo/Extensions/ExtensionErrors.h"

namespace Horo::Extensions::ExtensionErrors {
    const ErrorDomainId Domain{"horo.extensions"};

    const ErrorCodeDescriptor InvalidManifest{
        .domain = Domain,
        .code = ErrorCode{"invalid_manifest"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "The extension manifest is invalid or malformed.",
        .remediationHint = "Check the extension.json file for syntax errors or missing required fields.",
        .retryable = false,
        .userActionable = true,
    };

    const ErrorCodeDescriptor LoadFailed{
        .domain = Domain,
        .code = ErrorCode{"load_failed"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "Failed to load the extension dynamic library.",
        .remediationHint = "Ensure the extension is compiled for the current platform and all dependencies are present.",
        .retryable = false,
        .userActionable = true,
    };

    const ErrorCodeDescriptor MissingEntryPoint{
        .domain = Domain,
        .code = ErrorCode{"missing_entry_point"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "The extension is missing the required horo_extension_load entry point.",
        .remediationHint = "Verify that the extension exports horo_extension_load using HORO_EXTENSION_EXPORT.",
        .retryable = false,
        .userActionable = true,
    };

    const ErrorCodeDescriptor ContributionRejected{
        .domain = Domain,
        .code = ErrorCode{"contribution_rejected"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "The extension contribution was rejected.",
        .remediationHint = "Check contribution identities, ABI versions, descriptors, and conflicts.",
        .retryable = false,
        .userActionable = true,
    };

    const ErrorCodeDescriptor InvocationFailed{
        .domain = Domain,
        .code = ErrorCode{"invocation_failed"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "An extension callback failed.",
        .remediationHint = "Inspect the extension diagnostics and verify its input and version compatibility.",
        .retryable = false,
        .userActionable = true,
    };

    const ErrorCodeDescriptor ModuleResolutionFailed{
        .domain = Domain,
        .code = ErrorCode{"module_resolution_failed"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "The extension package module graph could not be resolved.",
        .remediationHint = "Check module roles, local dependencies, service imports, export versions, and cycles.",
        .retryable = false,
        .userActionable = true,
    };

    const ErrorCodeDescriptor LifecycleTransitionInvalid{
        .domain = Domain,
        .code = ErrorCode{"lifecycle_transition_invalid"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "The extension lifecycle transition is invalid for the current state or authority.",
        .remediationHint = "Refresh lifecycle state and submit a legal transition through its owning service.",
        .retryable = false,
        .userActionable = false,
    };

    const ErrorCodeDescriptor LifecycleRevisionStale{
        .domain = Domain,
        .code = ErrorCode{"lifecycle_revision_stale"},
        .defaultSeverity = ErrorSeverity::Warning,
        .summary = "The extension lifecycle state changed before the requested transition could commit.",
        .remediationHint = "Refresh the current lifecycle revision before retrying the transition.",
        .retryable = true,
        .userActionable = false,
    };

    const ErrorCodeDescriptor LifecycleCapacityExceeded{
        .domain = Domain,
        .code = ErrorCode{"lifecycle_capacity_exceeded"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "The bounded extension lifecycle state or audit capacity was exhausted.",
        .remediationHint = "Retire archived audit state or begin a fresh package lifecycle generation.",
        .retryable = false,
        .userActionable = false,
    };
}  // namespace Horo::Extensions::ExtensionErrors
