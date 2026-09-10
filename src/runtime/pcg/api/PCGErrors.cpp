#include "Horo/PCG/PCGErrors.h"

namespace Horo::PCG::PCGErrors {
    namespace {
        const ErrorDomainId PcgDomain{"horo.pcg"};
    }

    const ErrorCodeDescriptor IdentityInvalid{.domain = PcgDomain,
                                              .code = ErrorCode{"pcg.identity.invalid"},
                                              .defaultSeverity = ErrorSeverity::Error,
                                              .summary = "The PCG identity uses a reserved representation.",
                                              .remediationHint = "Use identities issued by the owning PCG boundary."};
    const ErrorCodeDescriptor IdentityUnknown{.domain = PcgDomain,
                                              .code = ErrorCode{"pcg.identity.unknown"},
                                              .defaultSeverity = ErrorSeverity::Warning,
                                              .summary = "The PCG identity belongs to a different graph or logical owner.",
                                              .remediationHint = "Resolve the identity only through its exact graph owner."};
    const ErrorCodeDescriptor IdentityStale{.domain = PcgDomain,
                                            .code = ErrorCode{"pcg.identity.stale"},
                                            .defaultSeverity = ErrorSeverity::Warning,
                                            .summary = "The PCG identity belongs to a retired graph revision or execution.",
                                            .remediationHint = "Discard stale work and resolve the current generation explicitly."};
    const ErrorCodeDescriptor RevisionExhausted{.domain = PcgDomain,
                                                .code = ErrorCode{"pcg.revision.exhausted"},
                                                .defaultSeverity = ErrorSeverity::Critical,
                                                .summary = "The PCG graph revision cannot advance without wrapping.",
                                                .remediationHint = "Retire the graph identity permanently; never reuse a revision."};
    const ErrorCodeDescriptor SerializedIdentityInvalid{.domain = PcgDomain,
                                                        .code = ErrorCode{"pcg.identity.serialized_invalid"},
                                                        .defaultSeverity = ErrorSeverity::Error,
                                                        .summary = "Serialized PCG identity bytes contain a reserved value.",
                                                        .remediationHint = "Reject or recook the malformed PCG payload."};
}  // namespace Horo::PCG::PCGErrors
