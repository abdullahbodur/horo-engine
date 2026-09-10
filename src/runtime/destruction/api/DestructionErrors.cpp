#include "Horo/Destruction/DestructionErrors.h"

namespace Horo::Destruction::DestructionErrors {
    namespace {
        const ErrorDomainId DestructionDomain{"horo.destruction"};
    }

    const ErrorCodeDescriptor IdentityInvalid{.domain = DestructionDomain,
                                              .code = ErrorCode{"destruction.identity.invalid"},
                                              .defaultSeverity = ErrorSeverity::Error,
                                              .summary = "The destruction identity uses a reserved or incomplete representation.",
                                              .remediationHint = "Use identities issued by the owning destruction or asset boundary."};
    const ErrorCodeDescriptor IdentityUnknown{.domain = DestructionDomain,
                                              .code = ErrorCode{"destruction.identity.unknown"},
                                              .defaultSeverity = ErrorSeverity::Warning,
                                              .summary = "The destruction identity belongs to a different logical owner.",
                                              .remediationHint =
                                                  "Resolve the identity only against its exact asset, world, and destructible owner."};
    const ErrorCodeDescriptor StaleGeneration{.domain = DestructionDomain,
                                              .code = ErrorCode{"destruction.identity.stale_generation"},
                                              .defaultSeverity = ErrorSeverity::Warning,
                                              .summary = "The destruction identity belongs to a retired runtime generation.",
                                              .remediationHint = "Discard stale work and resolve the current destructible generation."};
    const ErrorCodeDescriptor StaleContent{.domain = DestructionDomain,
                                           .code = ErrorCode{"destruction.identity.stale_content"},
                                           .defaultSeverity = ErrorSeverity::Warning,
                                           .summary = "The destruction identity belongs to replaced fracture content.",
                                           .remediationHint =
                                               "Resolve the current artifact content and migrate stable chunk identities explicitly."};
    const ErrorCodeDescriptor StaleRevision{.domain = DestructionDomain,
                                            .code = ErrorCode{"destruction.identity.stale_revision"},
                                            .defaultSeverity = ErrorSeverity::Warning,
                                            .summary = "The destruction identity belongs to a retired semantic revision.",
                                            .remediationHint = "Re-query the current immutable destruction snapshot before continuing."};
    const ErrorCodeDescriptor GenerationExhausted{.domain = DestructionDomain,
                                                  .code = ErrorCode{"destruction.identity.generation_exhausted"},
                                                  .defaultSeverity = ErrorSeverity::Critical,
                                                  .summary = "The destruction generation cannot advance without wrapping.",
                                                  .remediationHint = "Retire the exhausted owner permanently; never reuse its generation."};
    const ErrorCodeDescriptor RevisionExhausted{.domain = DestructionDomain,
                                                .code = ErrorCode{"destruction.identity.revision_exhausted"},
                                                .defaultSeverity = ErrorSeverity::Critical,
                                                .summary = "The destruction revision cannot advance without wrapping.",
                                                .remediationHint = "Reject the transition and replace the owning generation explicitly."};
    const ErrorCodeDescriptor SerializedIdentityInvalid{.domain = DestructionDomain,
                                                        .code = ErrorCode{"destruction.identity.serialized_invalid"},
                                                        .defaultSeverity = ErrorSeverity::Error,
                                                        .summary = "Serialized destruction identity bytes are malformed.",
                                                        .remediationHint = "Reject or recook the malformed identity payload."};
}  // namespace Horo::Destruction::DestructionErrors
