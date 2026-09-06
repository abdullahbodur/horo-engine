#include "Horo/Navigation/NavigationErrors.h"

namespace Horo::Navigation::NavigationErrors {
    namespace {
        const ErrorDomainId NavigationDomain{"horo.navigation"};
    }

    const ErrorCodeDescriptor IdentityInvalid{
        .domain = NavigationDomain,
        .code = ErrorCode{"navigation.identity.invalid"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "The navigation identity uses its reserved invalid representation.",
        .remediationHint = "Use a non-zero identity issued by the owning navigation or authoring boundary.",
        .retryable = false,
        .userActionable = false,
    };
    const ErrorCodeDescriptor InvalidHandle{
        .domain = NavigationDomain,
        .code = ErrorCode{"navigation.handle.invalid"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "The navigation handle is malformed, foreign, or stale.",
        .remediationHint = "Resolve the stable binding again against the active navigation world and topology generation.",
        .retryable = false,
        .userActionable = false,
    };
    const ErrorCodeDescriptor GenerationExhausted{
        .domain = NavigationDomain,
        .code = ErrorCode{"navigation.generation.exhausted"},
        .defaultSeverity = ErrorSeverity::Critical,
        .summary = "The navigation identity generation range is exhausted.",
        .remediationHint = "Retire the exhausted slot or world; never wrap or reuse an issued navigation generation.",
        .retryable = false,
        .userActionable = false,
    };
    const ErrorCodeDescriptor CapabilityDescriptorInvalid{
        .domain = NavigationDomain,
        .code = ErrorCode{"navigation.capability.descriptor_invalid"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "The navigation capability evidence or bounded query requirement is invalid.",
        .remediationHint = "Use the supported contract version, typed identities, and finite positive query limits.",
        .retryable = false,
        .userActionable = true,
    };
    const ErrorCodeDescriptor CapabilityStale{
        .domain = NavigationDomain,
        .code = ErrorCode{"navigation.capability.stale"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "Navigation capability evidence changed during query admission.",
        .remediationHint = "Capture the current provider snapshot and repeat complete query admission.",
        .retryable = true,
        .userActionable = false,
    };
    const ErrorCodeDescriptor OperationUnsupported{
        .domain = NavigationDomain,
        .code = ErrorCode{"navigation.operation.unsupported"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "The selected provider does not support the requested navigation operation and quality.",
        .remediationHint = "Select an explicitly supported query quality or compose a provider with the required capability.",
        .retryable = false,
        .userActionable = true,
    };
    const ErrorCodeDescriptor CapabilityUnavailable{
        .domain = NavigationDomain,
        .code = ErrorCode{"navigation.capability.unavailable"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "A required navigation capability is not currently available.",
        .remediationHint = "Restore the selected provider or wait for its owner to publish available capability evidence.",
        .retryable = true,
        .userActionable = false,
    };
    const ErrorCodeDescriptor QueryLimitExceeded{
        .domain = NavigationDomain,
        .code = ErrorCode{"navigation.query.limit_exceeded"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "The navigation query exceeds a finite provider work, output, or distance limit.",
        .remediationHint = "Reduce the requested bounds or select a provider and project profile declaring sufficient capacity.",
        .retryable = false,
        .userActionable = true,
    };
    const ErrorCodeDescriptor AdmissionRejected{
        .domain = NavigationDomain,
        .code = ErrorCode{"navigation.query.admission_rejected"},
        .defaultSeverity = ErrorSeverity::Warning,
        .summary = "Navigation admission rejected the request before creating a handle or job.",
        .remediationHint = "Retry within policy after queue or result-record capacity becomes available.",
        .retryable = true,
        .userActionable = false,
    };
    const ErrorCodeDescriptor QueryCancelled{
        .domain = NavigationDomain,
        .code = ErrorCode{"navigation.query.cancelled"},
        .defaultSeverity = ErrorSeverity::Info,
        .summary = "The navigation query was cancelled before publication.",
        .remediationHint = "Submit a new request only if the owning world and caller still require the result.",
        .retryable = true,
        .userActionable = false,
    };
    const ErrorCodeDescriptor StaleSnapshot{
        .domain = NavigationDomain,
        .code = ErrorCode{"navigation.query.stale_snapshot"},
        .defaultSeverity = ErrorSeverity::Info,
        .summary = "The navigation result was computed from an old topology generation.",
        .remediationHint = "Revalidate the request against the active topology and resubmit within its bounded policy.",
        .retryable = true,
        .userActionable = false,
    };
    const ErrorCodeDescriptor NoNavigationData{
        .domain = NavigationDomain,
        .code = ErrorCode{"navigation.data.unavailable"},
        .defaultSeverity = ErrorSeverity::Warning,
        .summary = "The required navigation world or coverage is unavailable.",
        .remediationHint = "Activate or stream the required navigation data; do not treat missing coverage as a proven no-path result.",
        .retryable = true,
        .userActionable = false,
    };
    const ErrorCodeDescriptor ProviderFailed{
        .domain = NavigationDomain,
        .code = ErrorCode{"navigation.provider.failed"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "The selected navigation provider failed while executing admitted work.",
        .remediationHint = "Inspect the normalized provider category and Horo diagnostics before retrying or replacing the provider.",
        .retryable = false,
        .userActionable = true,
    };
    const ErrorCodeDescriptor OutcomeDescriptorInvalid{
        .domain = NavigationDomain,
        .code = ErrorCode{"navigation.outcome.descriptor_invalid"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "Navigation terminal outcome evidence is invalid.",
        .remediationHint = "Publish outcomes only through typed factories with valid bounded provenance and coverage evidence.",
        .retryable = false,
        .userActionable = false,
    };
    const ErrorCodeDescriptor InvalidWorld{
        .domain = NavigationDomain,
        .code = ErrorCode{"navigation.query.invalid_world"},
        .defaultSeverity = ErrorSeverity::Info,
        .summary = "The accepted navigation request belongs to an invalid or replaced world.",
        .remediationHint = "Resolve the active navigation-world incarnation and submit a new request if still required.",
        .retryable = true,
        .userActionable = false,
    };
    const ErrorCodeDescriptor CapacityExceeded{
        .domain = NavigationDomain,
        .code = ErrorCode{"navigation.query.capacity_exceeded"},
        .defaultSeverity = ErrorSeverity::Warning,
        .summary = "Accepted navigation work exceeded a declared execution or publication capacity.",
        .remediationHint = "Reduce query work or retry under a profile with sufficient bounded scratch, result, or retry capacity.",
        .retryable = true,
        .userActionable = true,
    };
    const ErrorCodeDescriptor AreaDescriptorInvalid{
        .domain = NavigationDomain,
        .code = ErrorCode{"navigation.area.descriptor_invalid"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "A navigation area descriptor is invalid.",
        .remediationHint = "Use stable non-zero identities, typed source provenance, and a finite non-negative traversal cost.",
        .retryable = false,
        .userActionable = true,
    };
    const ErrorCodeDescriptor FilterDescriptorInvalid{
        .domain = NavigationDomain,
        .code = ErrorCode{"navigation.filter.descriptor_invalid"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "A navigation query-filter descriptor is invalid.",
        .remediationHint = "Use stable identities, typed source provenance, and finite non-negative area cost overrides.",
        .retryable = false,
        .userActionable = true,
    };
    const ErrorCodeDescriptor DescriptorConflict{
        .domain = NavigationDomain,
        .code = ErrorCode{"navigation.registry.descriptor_conflict"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "Navigation descriptors collide in one registry identity domain.",
        .remediationHint =
            "Assign unique stable area, filter, and per-filter override identities across project and package contributions.",
        .retryable = false,
        .userActionable = true,
    };
    const ErrorCodeDescriptor AreaUnknown{
        .domain = NavigationDomain,
        .code = ErrorCode{"navigation.area.unknown"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "The requested navigation area identity is not registered.",
        .remediationHint = "Restore the exact project or package area descriptor; do not substitute a default area.",
        .retryable = false,
        .userActionable = true,
    };
    const ErrorCodeDescriptor FilterUnknown{
        .domain = NavigationDomain,
        .code = ErrorCode{"navigation.filter.unknown"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "The requested navigation query-filter identity is not registered.",
        .remediationHint = "Restore the exact project or package filter descriptor; do not substitute a default filter.",
        .retryable = false,
        .userActionable = true,
    };
}  // namespace Horo::Navigation::NavigationErrors
