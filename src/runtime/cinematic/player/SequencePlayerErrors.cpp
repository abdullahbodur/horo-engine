#include "Horo/Cinematic/SequencePlayerErrors.h"

namespace Horo::Cinematic::SequencePlayerErrors {
    namespace {
        const ErrorDomainId PlayerDomain{"horo.cinematic.player"};
    }

    const ErrorCodeDescriptor HandleInvalid{.domain = PlayerDomain,
                                            .code = ErrorCode{"cinematic.player.handle_invalid"},
                                            .defaultSeverity = ErrorSeverity::Error,
                                            .summary = "The sequence-player handle uses a reserved representation.",
                                            .remediationHint = "Use a handle issued by the active cinematic runtime session."};
    const ErrorCodeDescriptor HandleUnknown{.domain = PlayerDomain,
                                            .code = ErrorCode{"cinematic.player.handle_unknown"},
                                            .defaultSeverity = ErrorSeverity::Warning,
                                            .summary = "The sequence-player handle targets another session or player.",
                                            .remediationHint = "Resolve the player through its owning runtime session."};
    const ErrorCodeDescriptor HandleStale{.domain = PlayerDomain,
                                          .code = ErrorCode{"cinematic.player.handle_stale"},
                                          .defaultSeverity = ErrorSeverity::Warning,
                                          .summary = "The sequence-player handle or operation fence is stale.",
                                          .remediationHint = "Discard late work and obtain the current player generation."};
    const ErrorCodeDescriptor TransitionInvalid{.domain = PlayerDomain,
                                                .code = ErrorCode{"cinematic.player.transition_invalid"},
                                                .defaultSeverity = ErrorSeverity::Warning,
                                                .summary = "The playback command is invalid in the current player state.",
                                                .remediationHint = "Wait for the current lifecycle transition or create a new player."};
    const ErrorCodeDescriptor TimeInvalid{.domain = PlayerDomain,
                                          .code = ErrorCode{"cinematic.player.time_invalid"},
                                          .defaultSeverity = ErrorSeverity::Error,
                                          .summary = "The requested sequence time is outside the player bounds.",
                                          .remediationHint = "Seek to an exact time between zero and the sequence duration."};
    const ErrorCodeDescriptor RateInvalid{.domain = PlayerDomain,
                                          .code = ErrorCode{"cinematic.player.rate_invalid"},
                                          .defaultSeverity = ErrorSeverity::Error,
                                          .summary = "The requested playback rate is malformed or unbounded.",
                                          .remediationHint = "Use a non-zero denominator and a rate magnitude no greater than 1024."};
    const ErrorCodeDescriptor RevisionExhausted{.domain = PlayerDomain,
                                                .code = ErrorCode{"cinematic.player.revision_exhausted"},
                                                .defaultSeverity = ErrorSeverity::Critical,
                                                .summary = "The sequence-player control revision cannot advance safely.",
                                                .remediationHint = "Retire the player permanently; never wrap its revision."};
}  // namespace Horo::Cinematic::SequencePlayerErrors
