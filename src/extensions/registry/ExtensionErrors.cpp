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
}  // namespace Horo::Extensions::ExtensionErrors
