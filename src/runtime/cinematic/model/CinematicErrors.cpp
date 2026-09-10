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
    const ErrorCodeDescriptor SequenceSchemaMalformed{.domain = CinematicDomain,
                                                      .code = ErrorCode{"cinematic.sequence_schema.malformed"},
                                                      .defaultSeverity = ErrorSeverity::Error,
                                                      .summary = "The sequence source schema is malformed.",
                                                      .remediationHint = "Repair the reported sequence field and save the asset again.",
                                                      .userActionable = true};
    const ErrorCodeDescriptor SequenceSchemaDuplicate{.domain = CinematicDomain,
                                                      .code = ErrorCode{"cinematic.sequence_schema.duplicate"},
                                                      .defaultSeverity = ErrorSeverity::Error,
                                                      .summary = "The sequence source contains an ambiguous duplicate identity.",
                                                      .remediationHint = "Regenerate the duplicate track or dependency identity.",
                                                      .userActionable = true};
    const ErrorCodeDescriptor SequenceSchemaVersionUnsupported{.domain = CinematicDomain,
                                                               .code = ErrorCode{"cinematic.sequence_schema.version_unsupported"},
                                                               .defaultSeverity = ErrorSeverity::Error,
                                                               .summary = "The sequence source schema version is not directly readable.",
                                                               .remediationHint =
                                                                   "Run the matching sequence migration or upgrade the engine.",
                                                               .userActionable = true};
    const ErrorCodeDescriptor SequenceSchemaLimitExceeded{.domain = CinematicDomain,
                                                          .code = ErrorCode{"cinematic.sequence_schema.limit_exceeded"},
                                                          .defaultSeverity = ErrorSeverity::Error,
                                                          .summary = "The sequence source exceeds a parser safety limit.",
                                                          .remediationHint = "Reduce the reported source, track, key, or dependency count.",
                                                          .userActionable = true};
    const ErrorCodeDescriptor SequenceCookTierExceeded{.domain = CinematicDomain,
                                                       .code = ErrorCode{"cinematic.sequence_cook.tier_exceeded"},
                                                       .defaultSeverity = ErrorSeverity::Error,
                                                       .summary = "The sequence exceeds the selected cook tier.",
                                                       .remediationHint =
                                                           "Reduce sequence complexity or explicitly select a larger admitted tier.",
                                                       .userActionable = true};
    const ErrorCodeDescriptor SequenceReferenceMissing{.domain = CinematicDomain,
                                                       .code = ErrorCode{"cinematic.sequence_reference.missing"},
                                                       .defaultSeverity = ErrorSeverity::Error,
                                                       .summary = "A sequence dependency is missing from the cook snapshot.",
                                                       .remediationHint = "Restore or explicitly repair the referenced asset.",
                                                       .userActionable = true};
    const ErrorCodeDescriptor SequenceReferenceMoved{.domain = CinematicDomain,
                                                     .code = ErrorCode{"cinematic.sequence_reference.move_pending"},
                                                     .defaultSeverity = ErrorSeverity::Error,
                                                     .summary = "A sequence dependency move is not reconciled in the cook snapshot.",
                                                     .remediationHint =
                                                         "Refresh the Asset Registry and cook again using the same stable AssetId.",
                                                     .retryable = true,
                                                     .userActionable = true};
    const ErrorCodeDescriptor SequenceReferenceUnloadable{.domain = CinematicDomain,
                                                          .code = ErrorCode{"cinematic.sequence_reference.unloadable"},
                                                          .defaultSeverity = ErrorSeverity::Error,
                                                          .summary = "A sequence dependency cannot be loaded for cooking.",
                                                          .remediationHint = "Repair or republish the referenced asset before cooking.",
                                                          .retryable = true,
                                                          .userActionable = true};
    const ErrorCodeDescriptor SequenceReferenceTypeMismatch{.domain = CinematicDomain,
                                                            .code = ErrorCode{"cinematic.sequence_reference.type_mismatch"},
                                                            .defaultSeverity = ErrorSeverity::Error,
                                                            .summary = "A sequence dependency has an incompatible asset type.",
                                                            .remediationHint = "Assign an asset of the type required by the owning track.",
                                                            .userActionable = true};
    const ErrorCodeDescriptor SequenceReferenceCycle{.domain = CinematicDomain,
                                                     .code = ErrorCode{"cinematic.sequence_reference.cycle"},
                                                     .defaultSeverity = ErrorSeverity::Error,
                                                     .summary = "The reachable sub-sequence graph contains a cycle.",
                                                     .remediationHint = "Remove one sub-sequence edge from the reported cycle.",
                                                     .userActionable = true};
}  // namespace Horo::Cinematic::CinematicErrors
