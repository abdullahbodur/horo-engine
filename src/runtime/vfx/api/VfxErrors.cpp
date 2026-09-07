#include "Horo/Vfx/VfxErrors.h"

namespace Horo::Vfx::VfxErrors {
    namespace {
        const ErrorDomainId VfxDomain{"horo.vfx"};
    }

    const ErrorCodeDescriptor IdentityInvalid{.domain = VfxDomain,
                                              .code = ErrorCode{"vfx.identity.invalid"},
                                              .defaultSeverity = ErrorSeverity::Error,
                                              .summary = "The VFX identity uses a reserved representation.",
                                              .remediationHint = "Use an identity issued by the owning VFX boundary."};
    const ErrorCodeDescriptor IdentityUnknown{.domain = VfxDomain,
                                              .code = ErrorCode{"vfx.identity.unknown"},
                                              .defaultSeverity = ErrorSeverity::Warning,
                                              .summary = "The VFX identity does not name the requested owner slot.",
                                              .remediationHint = "Resolve the identity against its exact owner scope."};
    const ErrorCodeDescriptor IdentityStale{.domain = VfxDomain,
                                            .code = ErrorCode{"vfx.identity.stale"},
                                            .defaultSeverity = ErrorSeverity::Warning,
                                            .summary = "The VFX identity names a retired slot generation.",
                                            .remediationHint = "Discard the stale reference and obtain the current generation."};
    const ErrorCodeDescriptor GenerationExhausted{.domain = VfxDomain,
                                                  .code = ErrorCode{"vfx.identity.generation_exhausted"},
                                                  .defaultSeverity = ErrorSeverity::Critical,
                                                  .summary = "The VFX slot generation cannot advance without wrapping.",
                                                  .remediationHint = "Retire the exhausted slot permanently; never wrap its generation."};
    const ErrorCodeDescriptor SerializedIdentityInvalid{.domain = VfxDomain,
                                                        .code = ErrorCode{"vfx.identity.serialized_invalid"},
                                                        .defaultSeverity = ErrorSeverity::Error,
                                                        .summary = "The serialized VFX identity contains a reserved value.",
                                                        .remediationHint = "Reject or recook the malformed identity payload."};
}  // namespace Horo::Vfx::VfxErrors
