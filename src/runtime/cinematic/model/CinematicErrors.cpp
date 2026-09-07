#include "Horo/Cinematic/CinematicErrors.h"

namespace Horo::Cinematic::CinematicErrors {
    namespace {
        const ErrorDomainId CinematicDomain{"horo.cinematic"};
    }

    const ErrorCodeDescriptor IdentityInvalid{.domain = CinematicDomain,
                                              .code = ErrorCode{"cinematic.identity.invalid"},
                                              .defaultSeverity = ErrorSeverity::Error,
                                              .summary = "The cinematic identity uses a reserved representation.",
                                              .remediationHint = "Use an identity issued by the owning sequence document."};
    const ErrorCodeDescriptor IdentityUnknown{.domain = CinematicDomain,
                                              .code = ErrorCode{"cinematic.identity.unknown"},
                                              .defaultSeverity = ErrorSeverity::Warning,
                                              .summary = "The cinematic identity does not name the requested authored object.",
                                              .remediationHint = "Resolve the identity against its exact sequence owner."};
    const ErrorCodeDescriptor IdentityStale{.domain = CinematicDomain,
                                            .code = ErrorCode{"cinematic.identity.stale"},
                                            .defaultSeverity = ErrorSeverity::Warning,
                                            .summary = "The cinematic identity names a retired generation.",
                                            .remediationHint = "Discard the stale reference and obtain the current generation."};
    const ErrorCodeDescriptor GenerationExhausted{.domain = CinematicDomain,
                                                  .code = ErrorCode{"cinematic.identity.generation_exhausted"},
                                                  .defaultSeverity = ErrorSeverity::Critical,
                                                  .summary = "The cinematic identity generation cannot advance without wrapping.",
                                                  .remediationHint =
                                                      "Retire the exhausted identity permanently; never wrap its generation."};
    const ErrorCodeDescriptor SerializedIdentityInvalid{.domain = CinematicDomain,
                                                        .code = ErrorCode{"cinematic.identity.serialized_invalid"},
                                                        .defaultSeverity = ErrorSeverity::Error,
                                                        .summary = "The serialized cinematic identity contains a reserved value.",
                                                        .remediationHint = "Reject or recook the malformed sequence payload."};
}  // namespace Horo::Cinematic::CinematicErrors
