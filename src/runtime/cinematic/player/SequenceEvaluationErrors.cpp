#include "Horo/Cinematic/SequenceEvaluationErrors.h"

namespace Horo::Cinematic::SequenceEvaluationErrors {
    namespace {
        const ErrorDomainId EvaluationDomain{"horo.cinematic.evaluation"};
    }

    const ErrorCodeDescriptor Malformed{.domain = EvaluationDomain,
                                        .code = ErrorCode{"cinematic.evaluation.malformed"},
                                        .defaultSeverity = ErrorSeverity::Error,
                                        .summary = "The sequence evaluation contract is malformed.",
                                        .remediationHint = "Rebuild the evaluation plan and cursor from validated cooked data."};
    const ErrorCodeDescriptor Stale{.domain = EvaluationDomain,
                                    .code = ErrorCode{"cinematic.evaluation.stale"},
                                    .defaultSeverity = ErrorSeverity::Warning,
                                    .summary = "The sequence evaluation cursor is stale.",
                                    .remediationHint = "Discard it and synchronize a cursor to the current player revision."};
    const ErrorCodeDescriptor PlayerStateInvalid{.domain = EvaluationDomain,
                                                 .code = ErrorCode{"cinematic.evaluation.player_state_invalid"},
                                                 .defaultSeverity = ErrorSeverity::Warning,
                                                 .summary = "The player is not eligible for frame evaluation.",
                                                 .remediationHint = "Evaluate only a Playing player at its owning boundary."};
    const ErrorCodeDescriptor DeltaInvalid{.domain = EvaluationDomain,
                                           .code = ErrorCode{"cinematic.evaluation.delta_invalid"},
                                           .defaultSeverity = ErrorSeverity::Error,
                                           .summary = "The source-clock delta is negative.",
                                           .remediationHint =
                                               "Use a non-negative source delta and express direction through playback rate."};
    const ErrorCodeDescriptor ArithmeticOverflow{.domain = EvaluationDomain,
                                                 .code = ErrorCode{"cinematic.evaluation.arithmetic_overflow"},
                                                 .defaultSeverity = ErrorSeverity::Error,
                                                 .summary = "Exact timeline arithmetic would overflow.",
                                                 .remediationHint = "Use bounded clock quanta and retire an exhausted timeline."};
    const ErrorCodeDescriptor CapacityExceeded{.domain = EvaluationDomain,
                                               .code = ErrorCode{"cinematic.evaluation.capacity_exceeded"},
                                               .defaultSeverity = ErrorSeverity::Warning,
                                               .summary = "The evaluation exceeds a fixed frame capacity.",
                                               .remediationHint = "Use a larger admitted profile or split the sequence during cook."};
    const ErrorCodeDescriptor HookUnavailable{.domain = EvaluationDomain,
                                              .code = ErrorCode{"cinematic.evaluation.hook_unavailable"},
                                              .defaultSeverity = ErrorSeverity::Error,
                                              .summary = "A crossed key has no typed destination hook.",
                                              .remediationHint =
                                                  "Inject the required session event or camera owner adapter before activation."};
    const ErrorCodeDescriptor RevisionExhausted{.domain = EvaluationDomain,
                                                .code = ErrorCode{"cinematic.evaluation.revision_exhausted"},
                                                .defaultSeverity = ErrorSeverity::Critical,
                                                .summary = "The frame evaluation revision cannot advance safely.",
                                                .remediationHint = "Retire the player and never wrap its evaluation revision."};
}  // namespace Horo::Cinematic::SequenceEvaluationErrors
