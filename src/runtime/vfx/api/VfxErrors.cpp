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
    const ErrorCodeDescriptor ParticleDescriptorMalformed{.domain = VfxDomain,
                                                          .code = ErrorCode{"vfx.particle_descriptor.malformed"},
                                                          .defaultSeverity = ErrorSeverity::Error,
                                                          .summary = "The particle-system source schema is malformed.",
                                                          .remediationHint =
                                                              "Repair the reported field and import the particle system again.",
                                                          .userActionable = true};
    const ErrorCodeDescriptor ParticleDescriptorDuplicate{.domain = VfxDomain,
                                                          .code = ErrorCode{"vfx.particle_descriptor.duplicate"},
                                                          .defaultSeverity = ErrorSeverity::Error,
                                                          .summary = "The particle-system source contains a duplicate field.",
                                                          .remediationHint = "Remove the ambiguous duplicate JSON field before import.",
                                                          .userActionable = true};
    const ErrorCodeDescriptor
        ParticleDescriptorVersionUnsupported{.domain = VfxDomain,
                                             .code = ErrorCode{"vfx.particle_descriptor.version_unsupported"},
                                             .defaultSeverity = ErrorSeverity::Error,
                                             .summary = "The particle-system schema version is not directly readable.",
                                             .remediationHint = "Run the matching source migration or use a compatible engine version.",
                                             .userActionable = true};
    const ErrorCodeDescriptor ParticleDescriptorLimitExceeded{.domain = VfxDomain,
                                                              .code = ErrorCode{"vfx.particle_descriptor.limit_exceeded"},
                                                              .defaultSeverity = ErrorSeverity::Error,
                                                              .summary = "The particle-system source exceeds a safety limit.",
                                                              .remediationHint =
                                                                  "Reduce the reported source size, depth, rate, or particle count.",
                                                              .userActionable = true};
    const ErrorCodeDescriptor ParticleRangeInvalid{.domain = VfxDomain,
                                                   .code = ErrorCode{"vfx.particle_descriptor.range_invalid"},
                                                   .defaultSeverity = ErrorSeverity::Error,
                                                   .summary = "A particle-system numeric range is invalid.",
                                                   .remediationHint = "Use finite ordered values within the documented field bounds.",
                                                   .userActionable = true};
    const ErrorCodeDescriptor ParticleLifetimeUnbounded{.domain = VfxDomain,
                                                        .code = ErrorCode{"vfx.particle_descriptor.lifetime_unbounded"},
                                                        .defaultSeverity = ErrorSeverity::Error,
                                                        .summary = "An infinite-lifetime particle has no terminal condition.",
                                                        .remediationHint =
                                                            "Add collision or explicit-signal termination, or author a finite lifetime.",
                                                        .userActionable = true};
    const ErrorCodeDescriptor ParticleModeIncompatible{.domain = VfxDomain,
                                                       .code = ErrorCode{"vfx.particle_descriptor.mode_incompatible"},
                                                       .defaultSeverity = ErrorSeverity::Error,
                                                       .summary = "Particle-system execution policies are incompatible.",
                                                       .remediationHint = "Align simulation, collision, kill, render, and sort policies.",
                                                       .userActionable = true};
    const ErrorCodeDescriptor ParticleMaterialMissing{.domain = VfxDomain,
                                                      .code = ErrorCode{"vfx.particle_material.missing"},
                                                      .defaultSeverity = ErrorSeverity::Error,
                                                      .summary = "The particle material is missing from the cook snapshot.",
                                                      .remediationHint = "Restore or explicitly replace the referenced material asset.",
                                                      .userActionable = true};
    const ErrorCodeDescriptor ParticleMaterialTypeMismatch{.domain = VfxDomain,
                                                           .code = ErrorCode{"vfx.particle_material.type_mismatch"},
                                                           .defaultSeverity = ErrorSeverity::Error,
                                                           .summary = "The particle material reference has an incompatible asset type.",
                                                           .remediationHint = "Assign a material asset to the particle-system output.",
                                                           .userActionable = true};
    const ErrorCodeDescriptor ParticleMaterialUnloadable{.domain = VfxDomain,
                                                         .code = ErrorCode{"vfx.particle_material.unloadable"},
                                                         .defaultSeverity = ErrorSeverity::Error,
                                                         .summary = "The particle material cannot be loaded for cooking.",
                                                         .remediationHint = "Repair or republish the material before cooking.",
                                                         .retryable = true,
                                                         .userActionable = true};
    const ErrorCodeDescriptor ParticleCookTierExceeded{.domain = VfxDomain,
                                                       .code = ErrorCode{"vfx.particle_cook.tier_exceeded"},
                                                       .defaultSeverity = ErrorSeverity::Error,
                                                       .summary = "The particle system exceeds the selected cook tier.",
                                                       .remediationHint =
                                                           "Reduce the emitter workload or explicitly select a larger admitted tier.",
                                                       .userActionable = true};
    const ErrorCodeDescriptor
        ParticleSimulationIdentityInvalid{.domain = VfxDomain,
                                          .code = ErrorCode{"vfx.particle.identity_invalid"},
                                          .defaultSeverity = ErrorSeverity::Error,
                                          .summary = "The particle simulation identity is invalid or was already issued.",
                                          .remediationHint =
                                              "Issue strictly increasing non-zero simulation identities independently from storage slots."};
    const ErrorCodeDescriptor
        ParticleBufferInvalid{.domain = VfxDomain,
                              .code = ErrorCode{"vfx.particle_buffer.invalid"},
                              .defaultSeverity = ErrorSeverity::Error,
                              .summary = "The CPU particle buffer allocation contract is invalid.",
                              .remediationHint =
                                  "Use a valid buffer identity and bounded non-zero capacity, payload count, and byte budget."};
    const ErrorCodeDescriptor
        ParticleBufferAllocationFailed{.domain = VfxDomain,
                                       .code = ErrorCode{"vfx.particle_buffer.allocation_failed"},
                                       .defaultSeverity = ErrorSeverity::Error,
                                       .summary = "The admitted CPU particle storage block could not be allocated.",
                                       .remediationHint =
                                           "Reduce the prepared capacity or release other scene-owned VFX storage before retrying.",
                                       .retryable = true};
    const ErrorCodeDescriptor ParticleBufferCapacityExceeded{.domain = VfxDomain,
                                                             .code = ErrorCode{"vfx.particle_buffer.capacity_exceeded"},
                                                             .defaultSeverity = ErrorSeverity::Warning,
                                                             .summary = "The CPU particle buffer has no reusable slot.",
                                                             .remediationHint =
                                                                 "Reject the newest birth or use an explicitly admitted overload policy."};
    const ErrorCodeDescriptor ParticleBufferThreadViolation{.domain = VfxDomain,
                                                            .code = ErrorCode{"vfx.particle_buffer.thread_violation"},
                                                            .defaultSeverity = ErrorSeverity::Error,
                                                            .summary =
                                                                "CPU particle storage was accessed outside its owning simulation thread.",
                                                            .remediationHint =
                                                                "Schedule mutation and views on the VFX simulation owner safe point."};
    const ErrorCodeDescriptor
        ParticleBufferShutDown{.domain = VfxDomain,
                               .code = ErrorCode{"vfx.particle_buffer.shut_down"},
                               .defaultSeverity = ErrorSeverity::Warning,
                               .summary = "The CPU particle buffer has closed admission and retired its live slots.",
                               .remediationHint =
                                   "Discard the old buffer facade and create storage for the replacement scene incarnation."};
    const ErrorCodeDescriptor ParticleHandleInvalid{.domain = VfxDomain,
                                                    .code = ErrorCode{"vfx.particle_buffer.handle_invalid"},
                                                    .defaultSeverity = ErrorSeverity::Error,
                                                    .summary = "The particle handle is malformed or belongs to another buffer.",
                                                    .remediationHint =
                                                        "Use the complete handle returned by the owning CPU particle buffer."};
    const ErrorCodeDescriptor ParticleHandleStale{.domain = VfxDomain,
                                                  .code = ErrorCode{"vfx.particle_buffer.handle_stale"},
                                                  .defaultSeverity = ErrorSeverity::Warning,
                                                  .summary = "The particle handle names a killed or recycled slot generation.",
                                                  .remediationHint =
                                                      "Discard stale handles; storage-slot reuse never preserves particle authority."};
}  // namespace Horo::Vfx::VfxErrors
