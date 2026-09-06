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
}  // namespace Horo::Navigation::NavigationErrors
