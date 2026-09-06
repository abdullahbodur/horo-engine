#include "Horo/Runtime/Render/RenderAdapterErrors.h"

#include "RenderErrorDescriptor.h"

namespace Horo::Render::RenderAdapterErrors {
    namespace {
        const ErrorDomainId Domain{"render.adapter"};
    }  // namespace

    const ErrorCodeDescriptor AdapterUnavailable =
        Detail::MakeErrorDescriptor(Domain, "render.adapter.unavailable", ErrorSeverity::Error,
                                    "The explicitly selected adapter is unavailable.",
                                    "Inspect the preserved device diagnostic or select another adapter explicitly.", true);
    const ErrorCodeDescriptor DiscoveryStopped =
        Detail::MakeErrorDescriptor(Domain, "render.adapter.discovery_stopped", ErrorSeverity::Error,
                                    "Adapter discovery admission is closed.",
                                    "Create a new backend discovery owner after shutdown completes.");
    const ErrorCodeDescriptor InvalidDiscoveryRequest =
        Detail::MakeErrorDescriptor(Domain, "render.adapter.discovery_request_invalid", ErrorSeverity::Error,
                                    "Adapter discovery limits are invalid.", "Request between one and 64 adapter records.");
    const ErrorCodeDescriptor InvalidSelectionRequest =
        Detail::MakeErrorDescriptor(Domain, "render.adapter.selection_request_invalid", ErrorSeverity::Error,
                                    "Adapter selection constraints are malformed.",
                                    "Use a valid exact adapter identity and declared device kind.");
    const ErrorCodeDescriptor InvalidSnapshot =
        Detail::MakeErrorDescriptor(Domain, "render.adapter.snapshot_invalid", ErrorSeverity::Error,
                                    "Adapter discovery returned malformed or unordered facts.",
                                    "Reject the backend result and preserve its diagnostic for qualification.");
    const ErrorCodeDescriptor NoCompatibleAdapter =
        Detail::MakeErrorDescriptor(Domain, "render.adapter.no_compatible_adapter", ErrorSeverity::Error,
                                    "No discovered adapter satisfies the exact constraints.",
                                    "Change the explicit product constraints or select a compatible adapter.");
    const ErrorCodeDescriptor RequiredAdapterNotFound =
        Detail::MakeErrorDescriptor(Domain, "render.adapter.required_not_found", ErrorSeverity::Error,
                                    "The explicitly selected adapter was not discovered.",
                                    "Reconnect or install the requested adapter, or change selection explicitly.");
    const ErrorCodeDescriptor StaleDiscovery =
        Detail::MakeErrorDescriptor(Domain, "render.adapter.discovery_stale", ErrorSeverity::Error,
                                    "The adapter discovery revision is stale.",
                                    "Rediscover adapters and repeat explicit selection before device creation.", true);
}  // namespace Horo::Render::RenderAdapterErrors
