#include "RenderBackendRegistryErrors.h"

namespace Horo::Render::RegistryErrors
{
    namespace
    {
        const ErrorDomainId Domain{"horo.render"};
    } // namespace

    const ErrorCodeDescriptor BackendNotFound{
        .domain = Domain,
        .code = ErrorCode{"render.registry.backend_not_found"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "Renderer backend is not registered.",
        .remediationHint = "Register the requested backend before creating it.",
        .retryable = false,
        .userActionable = false
    };

    const ErrorCodeDescriptor DuplicateBackend{
        .domain = Domain,
        .code = ErrorCode{"render.registry.duplicate_backend"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "Renderer backend ID is already registered.",
        .remediationHint = "Use one provider for each stable backend ID.",
        .retryable = false,
        .userActionable = false
    };

    const ErrorCodeDescriptor InvalidDescriptor{
        .domain = Domain,
        .code = ErrorCode{"render.registry.invalid_descriptor"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "Renderer backend descriptor is invalid.",
        .remediationHint = "Provide a valid ID, display name, and provider.",
        .retryable = false,
        .userActionable = false
    };

    const ErrorCodeDescriptor NotSealed{
        .domain = Domain,
        .code = ErrorCode{"render.registry.not_sealed"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "Renderer backend registry is not sealed.",
        .remediationHint = "Seal backend registration before runtime selection.",
        .retryable = false,
        .userActionable = false
    };

    const ErrorCodeDescriptor ProviderException{
        .domain = Domain,
        .code = ErrorCode{"render.registry.provider_exception"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "Renderer backend provider threw an exception.",
        .remediationHint = "Inspect the provider implementation and diagnostics.",
        .retryable = true,
        .userActionable = false
    };

    const ErrorCodeDescriptor ProviderReturnedNull{
        .domain = Domain,
        .code = ErrorCode{"render.registry.provider_returned_null"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "Renderer backend provider returned no backend.",
        .remediationHint = "Return a valid backend instance from the registered provider.",
        .retryable = false,
        .userActionable = false
    };

    const ErrorCodeDescriptor Sealed{
        .domain = Domain,
        .code = ErrorCode{"render.registry.sealed"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "Renderer backend registry is already sealed.",
        .remediationHint = "Register all backends before sealing the registry.",
        .retryable = false,
        .userActionable = false
    };
} // namespace Horo::Render::RegistryErrors
