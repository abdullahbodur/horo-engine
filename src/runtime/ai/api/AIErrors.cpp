#include "Horo/AI/AIErrors.h"

namespace Horo::AI::AIErrors {
    namespace {
        const ErrorDomainId AiDomain{"horo.ai"};
    }

    const ErrorCodeDescriptor IdentityInvalid{
        .domain = AiDomain,
        .code = ErrorCode{"ai.identity.invalid"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "The gameplay-AI identity uses its reserved invalid representation.",
        .remediationHint = "Use a non-zero identity issued by the owning authoring boundary.",
        .retryable = false,
        .userActionable = true,
    };
    const ErrorCodeDescriptor DescriptorConflict{
        .domain = AiDomain,
        .code = ErrorCode{"ai.descriptor.conflict"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "Gameplay-AI descriptors collide in one persistent identity domain.",
        .remediationHint = "Issue a distinct stable identity for each descriptor and duplicated authored object.",
        .retryable = false,
        .userActionable = true,
    };
    const ErrorCodeDescriptor DescriptorLimitExceeded{
        .domain = AiDomain,
        .code = ErrorCode{"ai.descriptor.limit_exceeded"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "The gameplay-AI descriptor identity set exceeds its validation bound.",
        .remediationHint = "Partition the authored contribution or reduce its descriptor count before activation.",
        .retryable = false,
        .userActionable = true,
    };
    const ErrorCodeDescriptor HandleInvalid{
        .domain = AiDomain,
        .code = ErrorCode{"ai.handle.invalid"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "The gameplay-AI runtime handle is malformed, foreign, or stale.",
        .remediationHint = "Resolve the persistent binding again against the active SceneRuntime incarnation.",
        .retryable = false,
        .userActionable = false,
    };
    const ErrorCodeDescriptor GenerationExhausted{
        .domain = AiDomain,
        .code = ErrorCode{"ai.generation.exhausted"},
        .defaultSeverity = ErrorSeverity::Critical,
        .summary = "The gameplay-AI runtime slot generation range is exhausted.",
        .remediationHint = "Retire the exhausted slot; never wrap or reuse an issued runtime generation.",
        .retryable = false,
        .userActionable = false,
    };
}  // namespace Horo::AI::AIErrors
