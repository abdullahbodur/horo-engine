#include "Horo/Animation/AnimationErrors.h"

namespace Horo::Animation::AnimationErrors {
    namespace {
        const ErrorDomainId AnimationDomain{"horo.animation"};
    }

    const ErrorCodeDescriptor IdentityInvalid{.domain = AnimationDomain,
                                              .code = ErrorCode{"animation.identity.invalid"},
                                              .defaultSeverity = ErrorSeverity::Error,
                                              .summary = "The animation identity uses a reserved representation.",
                                              .remediationHint = "Use a typed identity issued by the owning asset or animation boundary."};
    const ErrorCodeDescriptor HandleMalformed{.domain = AnimationDomain,
                                              .code = ErrorCode{"animation.handle.malformed"},
                                              .defaultSeverity = ErrorSeverity::Error,
                                              .summary = "The animation handle is incomplete or malformed.",
                                              .remediationHint = "Resolve a current non-owning handle from the animation runtime."};
    const ErrorCodeDescriptor HandleOwnerMismatch{.domain = AnimationDomain,
                                                  .code = ErrorCode{"animation.handle.owner_mismatch"},
                                                  .defaultSeverity = ErrorSeverity::Warning,
                                                  .summary = "The animation handle belongs to another runtime or authored component.",
                                                  .remediationHint =
                                                      "Submit the handle only to the runtime and component owner that issued it."};
    const ErrorCodeDescriptor HandleStale{.domain = AnimationDomain,
                                          .code = ErrorCode{"animation.handle.stale"},
                                          .defaultSeverity = ErrorSeverity::Warning,
                                          .summary = "The animation handle targets a retired generation.",
                                          .remediationHint = "Discard cached handles and resolve the current runtime generation."};
    const ErrorCodeDescriptor GenerationExhausted{.domain = AnimationDomain,
                                                  .code = ErrorCode{"animation.generation.exhausted"},
                                                  .defaultSeverity = ErrorSeverity::Critical,
                                                  .summary = "An animation generation cannot advance without reuse.",
                                                  .remediationHint =
                                                      "Retire the owning runtime or slot instead of wrapping its generation."};
    const ErrorCodeDescriptor ComponentInvalid{.domain = AnimationDomain,
                                               .code = ErrorCode{"animation.component.invalid"},
                                               .defaultSeverity = ErrorSeverity::Error,
                                               .summary = "The animation component contract is invalid or contradictory.",
                                               .remediationHint = "Correct its typed source, policy, domain, state, and pose ordering."};
    const ErrorCodeDescriptor ComponentBindingMismatch{.domain = AnimationDomain,
                                                       .code = ErrorCode{"animation.component.binding_mismatch"},
                                                       .defaultSeverity = ErrorSeverity::Error,
                                                       .summary = "The runtime animation projection does not match its authored binding.",
                                                       .remediationHint =
                                                           "Rebuild the runtime component from the current authored asset identities."};
    const ErrorCodeDescriptor ContractVersionUnsupported{.domain = AnimationDomain,
                                                         .code = ErrorCode{"animation.contract.version_unsupported"},
                                                         .defaultSeverity = ErrorSeverity::Error,
                                                         .summary = "The animation component contract version is unsupported.",
                                                         .remediationHint =
                                                             "Migrate or recook the component for the current animation contract."};
}  // namespace Horo::Animation::AnimationErrors
