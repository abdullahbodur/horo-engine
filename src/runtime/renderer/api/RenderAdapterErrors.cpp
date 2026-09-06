#include "Horo/Runtime/Render/RenderAdapterErrors.h"

#include <string>
#include <string_view>
#include <utility>

namespace Horo::Render::RenderAdapterErrors {
    namespace {
        const ErrorDomainId Domain{"render.adapter"};

        /** @brief Builds one immutable descriptor in the render-adapter error domain. */
        ErrorCodeDescriptor Descriptor(std::string code, const ErrorSeverity severity, const std::string_view summary,
                                       const std::string_view remediation, const bool retryable = false) {
            return {Domain, ErrorCode{std::move(code)}, severity, summary, remediation, retryable, false};
        }
    }  // namespace

    const ErrorCodeDescriptor AdapterUnavailable =
        Descriptor("render.adapter.unavailable", ErrorSeverity::Error, "The explicitly selected adapter is unavailable.",
                   "Inspect the preserved device diagnostic or select another adapter explicitly.", true);
    const ErrorCodeDescriptor DiscoveryStopped =
        Descriptor("render.adapter.discovery_stopped", ErrorSeverity::Error, "Adapter discovery admission is closed.",
                   "Create a new backend discovery owner after shutdown completes.");
    const ErrorCodeDescriptor InvalidDiscoveryRequest =
        Descriptor("render.adapter.discovery_request_invalid", ErrorSeverity::Error, "Adapter discovery limits are invalid.",
                   "Request between one and 64 adapter records.");
    const ErrorCodeDescriptor InvalidSelectionRequest =
        Descriptor("render.adapter.selection_request_invalid", ErrorSeverity::Error, "Adapter selection constraints are malformed.",
                   "Use a valid exact adapter identity and declared device kind.");
    const ErrorCodeDescriptor InvalidSnapshot =
        Descriptor("render.adapter.snapshot_invalid", ErrorSeverity::Error, "Adapter discovery returned malformed or unordered facts.",
                   "Reject the backend result and preserve its diagnostic for qualification.");
    const ErrorCodeDescriptor NoCompatibleAdapter =
        Descriptor("render.adapter.no_compatible_adapter", ErrorSeverity::Error, "No discovered adapter satisfies the exact constraints.",
                   "Change the explicit product constraints or select a compatible adapter.");
    const ErrorCodeDescriptor RequiredAdapterNotFound =
        Descriptor("render.adapter.required_not_found", ErrorSeverity::Error, "The explicitly selected adapter was not discovered.",
                   "Reconnect or install the requested adapter, or change selection explicitly.");
    const ErrorCodeDescriptor StaleDiscovery =
        Descriptor("render.adapter.discovery_stale", ErrorSeverity::Error, "The adapter discovery revision is stale.",
                   "Rediscover adapters and repeat explicit selection before device creation.", true);
}  // namespace Horo::Render::RenderAdapterErrors
