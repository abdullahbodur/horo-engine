#include "Horo/WorldStreaming/WorldStreamingErrors.h"

namespace Horo::WorldStreaming::WorldStreamingErrors {
    namespace {
        const ErrorDomainId Domain{"horo.world_streaming"};
    }

    const ErrorCodeDescriptor IdentityInvalid{
        .domain = Domain,
        .code = ErrorCode{"world_streaming.identity.invalid"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "A world-partition identity uses its reserved invalid representation.",
        .remediationHint = "Use an identity issued by the manifest, authoring boundary, or active partition authority.",
        .retryable = false,
        .userActionable = true,
    };
    const ErrorCodeDescriptor SerializedIdentityInvalid{
        .domain = Domain,
        .code = ErrorCode{"world_streaming.identity.serialized_invalid"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "A serialized world-partition identity is malformed or reserved.",
        .remediationHint = "Rebuild the world index or source descriptor with canonical little-endian identity bytes.",
        .retryable = false,
        .userActionable = true,
    };
    const ErrorCodeDescriptor GenerationExhausted{
        .domain = Domain,
        .code = ErrorCode{"world_streaming.generation.exhausted"},
        .defaultSeverity = ErrorSeverity::Critical,
        .summary = "A world-partition epoch or cell generation cannot advance without wrapping.",
        .remediationHint = "Retire the exhausted incarnation or slot; never wrap or reuse an issued streaming generation.",
        .retryable = false,
        .userActionable = false,
    };
}  // namespace Horo::WorldStreaming::WorldStreamingErrors
