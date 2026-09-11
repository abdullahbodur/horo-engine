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

    const ErrorCodeDescriptor CapabilityAdmissionInvalid{
        .domain = Domain,
        .code = ErrorCode{"capability_admission_invalid"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "The extension capability admission input is invalid.",
        .remediationHint = "Provide canonical bounded identities and a consistent host policy snapshot.",
        .retryable = false,
        .userActionable = false,
    };

    const ErrorCodeDescriptor PermissionDenied{
        .domain = Domain,
        .code = ErrorCode{"permission_denied"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "The extension permission request was denied.",
        .remediationHint = "Review the extension request and approve only the required permission through host policy.",
        .retryable = false,
        .userActionable = true,
    };

    const ErrorCodeDescriptor CapabilityUnavailable{
        .domain = Domain,
        .code = ErrorCode{"capability_unavailable"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "The requested application capability is unavailable.",
        .remediationHint = "Use a host composition that exposes the declared capability.",
        .retryable = false,
        .userActionable = true,
    };

    const ErrorCodeDescriptor CapabilityRevoked{
        .domain = Domain,
        .code = ErrorCode{"capability_revoked"},
        .defaultSeverity = ErrorSeverity::Warning,
        .summary = "The extension capability handle is no longer active.",
        .remediationHint = "Stop callback dispatch and acquire a handle from the current activation generation.",
        .retryable = false,
        .userActionable = false,
    };

    const ErrorCodeDescriptor CapabilityRegistryInvalid{
        .domain = Domain,
        .code = ErrorCode{"capability_registry_invalid"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "The application capability registry input is invalid.",
        .remediationHint = "Provide canonical provider identity, version, generation, and version range values.",
        .retryable = false,
        .userActionable = false,
    };

    const ErrorCodeDescriptor CapabilityRegistryDuplicate{
        .domain = Domain,
        .code = ErrorCode{"capability_registry_duplicate"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "The application capability provider is already registered.",
        .remediationHint = "Register only one provider for each exact capability contract version.",
        .retryable = false,
        .userActionable = false,
    };

    const ErrorCodeDescriptor CapabilityVersionIncompatible{
        .domain = Domain,
        .code = ErrorCode{"capability_version_incompatible"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "No compatible application capability provider version is available.",
        .remediationHint = "Use a supported contract version or install a compatible provider.",
        .retryable = false,
        .userActionable = true,
    };

    const ErrorCodeDescriptor CapabilityRegistryCapacityExceeded{
        .domain = Domain,
        .code = ErrorCode{"capability_registry_capacity_exceeded"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "The application capability registry capacity was exceeded.",
        .remediationHint = "Reduce the explicitly composed provider set.",
        .retryable = false,
        .userActionable = false,
    };

    const ErrorCodeDescriptor CapabilityRegistryShutdown{
        .domain = Domain,
        .code = ErrorCode{"capability_registry_shutdown"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "The application capability registry is shutting down.",
        .remediationHint = "Do not register or resolve providers after host shutdown begins.",
        .retryable = false,
        .userActionable = false,
    };
}  // namespace Horo::Extensions::ExtensionErrors
