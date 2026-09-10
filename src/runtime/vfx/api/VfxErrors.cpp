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
    const ErrorCodeDescriptor CapabilityDataInvalid{.domain = VfxDomain,
                                                    .code = ErrorCode{"vfx.capability.invalid"},
                                                    .defaultSeverity = ErrorSeverity::Error,
                                                    .summary = "The VFX capability snapshot is malformed or contradictory.",
                                                    .remediationHint =
                                                        "Publish complete effective facts and finite limits under a new revision."};
    const ErrorCodeDescriptor
        QualityPolicyInvalid{.domain = VfxDomain,
                             .code = ErrorCode{"vfx.quality_policy.invalid"},
                             .defaultSeverity = ErrorSeverity::Error,
                             .summary = "The VFX quality policy is invalid.",
                             .remediationHint = "Correct the profile, revision, budgets, and Automatic threshold before publication."};
    const ErrorCodeDescriptor RequirementInvalid{.domain = VfxDomain,
                                                 .code = ErrorCode{"vfx.requirement.invalid"},
                                                 .defaultSeverity = ErrorSeverity::Error,
                                                 .summary = "The compiled VFX requirement or fallback variant is invalid.",
                                                 .remediationHint = "Reject or recook the malformed effect artifact."};
    const ErrorCodeDescriptor
        DomainConflict{.domain = VfxDomain,
                       .code = ErrorCode{"vfx.domain.conflict"},
                       .defaultSeverity = ErrorSeverity::Error,
                       .summary = "The authored simulation preference conflicts with mandatory CPU behavior.",
                       .remediationHint = "Author the gameplay unit for CPU simulation or split visual-only work at a typed boundary."};
    const ErrorCodeDescriptor UnsupportedCapability{.domain = VfxDomain,
                                                    .code = ErrorCode{"vfx.capability.unsupported"},
                                                    .defaultSeverity = ErrorSeverity::Error,
                                                    .summary = "No permitted VFX path satisfies the effective capability facts.",
                                                    .remediationHint = "Cook a compatible fallback or select a qualified product policy."};
    const ErrorCodeDescriptor MissingKernel{.domain = VfxDomain,
                                            .code = ErrorCode{"vfx.kernel.missing"},
                                            .defaultSeverity = ErrorSeverity::Error,
                                            .summary = "The required VFX simulation kernel is missing.",
                                            .remediationHint = "Cook the required CPU or GPU kernel without silently changing domain."};
    const ErrorCodeDescriptor MissingVariant{.domain = VfxDomain,
                                             .code = ErrorCode{"vfx.variant.missing"},
                                             .defaultSeverity = ErrorSeverity::Error,
                                             .summary = "No authored compatible VFX fallback variant can be selected.",
                                             .remediationHint = "Author and cook an explicit bounded degradation variant."};
    const ErrorCodeDescriptor LimitExceeded{.domain = VfxDomain,
                                            .code = ErrorCode{"vfx.limit.exceeded"},
                                            .defaultSeverity = ErrorSeverity::Warning,
                                            .summary = "The VFX request exceeds an effective finite resource limit.",
                                            .remediationHint =
                                                "Reduce the authored workload explicitly or increase a qualified policy budget."};
    const ErrorCodeDescriptor CapabilityRevisionStale{.domain = VfxDomain,
                                                      .code = ErrorCode{"vfx.capability.stale"},
                                                      .defaultSeverity = ErrorSeverity::Warning,
                                                      .summary = "The prepared VFX decision references stale capability evidence.",
                                                      .remediationHint = "Re-resolve against the current immutable capability snapshot."};
    const ErrorCodeDescriptor QualityPolicyRevisionStale{.domain = VfxDomain,
                                                         .code = ErrorCode{"vfx.quality_policy.stale"},
                                                         .defaultSeverity = ErrorSeverity::Warning,
                                                         .summary = "The prepared VFX decision references a stale quality policy.",
                                                         .remediationHint = "Re-resolve against the current validated policy revision."};
}  // namespace Horo::Vfx::VfxErrors
