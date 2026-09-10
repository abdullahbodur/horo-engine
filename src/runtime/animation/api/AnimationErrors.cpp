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
    const ErrorCodeDescriptor SkeletonVersionUnsupported{AnimationDomain, ErrorCode{"animation.skeleton.version_unsupported"},
                                                         ErrorSeverity::Error, "The skeleton asset contract version is unsupported.",
                                                         "Migrate or recook the skeleton for the current contract."};
    const ErrorCodeDescriptor SkeletonAdmissionRejected{AnimationDomain, ErrorCode{"animation.skeleton.admission_rejected"},
                                                        ErrorSeverity::Warning, "The skeleton owner is not accepting validation work.",
                                                        "Retry only after a current asset owner resumes admission."};
    const ErrorCodeDescriptor SkeletonValidationCancelled{AnimationDomain, ErrorCode{"animation.skeleton.validation_cancelled"},
                                                          ErrorSeverity::Warning, "Skeleton validation was cancelled before publication.",
                                                          "Submit a new candidate under a current owner operation."};
    const ErrorCodeDescriptor SkeletonReloadMismatch{AnimationDomain, ErrorCode{"animation.skeleton.reload_mismatch"}, ErrorSeverity::Error,
                                                     "The skeleton reload candidate has a different stable identity.",
                                                     "Publish different identities as separate assets instead of a reload."};
    const ErrorCodeDescriptor SkeletonLimitExceeded{AnimationDomain, ErrorCode{"animation.skeleton.limit_exceeded"}, ErrorSeverity::Error,
                                                    "The skeleton exceeds a finite validation limit.",
                                                    "Reduce joints, depth, sockets, or metadata to the captured limits."};
    const ErrorCodeDescriptor SkeletonDuplicateIdentity{AnimationDomain, ErrorCode{"animation.skeleton.duplicate_identity"},
                                                        ErrorSeverity::Error, "The skeleton contains a duplicate stable identity.",
                                                        "Assign every joint and socket one unique stable identity."};
    const ErrorCodeDescriptor SkeletonJointMissing{AnimationDomain, ErrorCode{"animation.skeleton.joint_missing"}, ErrorSeverity::Error,
                                                   "Skeleton hierarchy or metadata references a missing joint.",
                                                   "Reference only joints declared by the same skeleton candidate."};
    const ErrorCodeDescriptor SkeletonHierarchyCycle{AnimationDomain, ErrorCode{"animation.skeleton.hierarchy_cycle"}, ErrorSeverity::Error,
                                                     "The skeleton parent hierarchy contains a cycle.",
                                                     "Break the cycle so every joint reaches one root."};
    const ErrorCodeDescriptor SkeletonTransformInvalid{AnimationDomain, ErrorCode{"animation.skeleton.transform_invalid"},
                                                       ErrorSeverity::Error, "Skeleton reference or inverse bind transforms are invalid.",
                                                       "Provide finite affine transforms and matching inverse bind matrices."};
    const ErrorCodeDescriptor SkeletonMetadataInvalid{AnimationDomain, ErrorCode{"animation.skeleton.metadata_invalid"},
                                                      ErrorSeverity::Error,
                                                      "Skeleton joint, mirror, retarget, or socket metadata is invalid.",
                                                      "Correct typed metadata and bounded advisory names."};
    const ErrorCodeDescriptor SkinningVersionUnsupported{AnimationDomain, ErrorCode{"animation.skinning.version_unsupported"},
                                                         ErrorSeverity::Error,
                                                         "The skeletal-mesh skinning contract version is unsupported.",
                                                         "Migrate or recook the mesh for the current skinning contract."};
    const ErrorCodeDescriptor SkinningAdmissionRejected{AnimationDomain, ErrorCode{"animation.skinning.admission_rejected"},
                                                        ErrorSeverity::Warning, "The skinning owner is not accepting validation work.",
                                                        "Retry only after a current asset owner resumes admission."};
    const ErrorCodeDescriptor SkinningValidationCancelled{AnimationDomain, ErrorCode{"animation.skinning.validation_cancelled"},
                                                          ErrorSeverity::Warning, "Skinning validation was cancelled before publication.",
                                                          "Submit a new candidate under a current owner operation."};
    const ErrorCodeDescriptor SkinningReloadMismatch{AnimationDomain, ErrorCode{"animation.skinning.reload_mismatch"}, ErrorSeverity::Error,
                                                     "The reload candidate has a different skeletal-mesh identity.",
                                                     "Publish different mesh identities as separate assets instead of a reload."};
    const ErrorCodeDescriptor SkinningSkeletonMismatch{AnimationDomain, ErrorCode{"animation.skinning.skeleton_mismatch"},
                                                       ErrorSeverity::Error, "The skinning binding targets an incompatible skeleton.",
                                                       "Bind the mesh to the exact validated skeleton identity and contract version."};
    const ErrorCodeDescriptor SkinningBindingStale{AnimationDomain, ErrorCode{"animation.skinning.binding_stale"}, ErrorSeverity::Warning,
                                                   "The skinning binding targets a retired skeleton generation.",
                                                   "Rebuild the binding against the current immutable skeleton publication."};
    const ErrorCodeDescriptor
        SkinningLimitExceeded{AnimationDomain, ErrorCode{"animation.skinning.limit_exceeded"}, ErrorSeverity::Error,
                              "The skinning asset exceeds a finite validation limit.",
                              "Reduce LODs, vertices, sections, joints, palettes, or influences to the captured limits."};
    const ErrorCodeDescriptor SkinningDuplicateIdentity{AnimationDomain, ErrorCode{"animation.skinning.duplicate_identity"},
                                                        ErrorSeverity::Error, "The skinning asset contains a duplicate stable identity.",
                                                        "Assign unique mesh joints, skeleton targets, sections, and LOD levels."};
    const ErrorCodeDescriptor SkinningJointMissing{AnimationDomain, ErrorCode{"animation.skinning.joint_missing"}, ErrorSeverity::Error,
                                                   "A skinning remap, palette, or influence references an absent joint.",
                                                   "Reference only joints declared by the binding and target skeleton."};
    const ErrorCodeDescriptor SkinningInfluenceInvalid{AnimationDomain, ErrorCode{"animation.skinning.influence_invalid"},
                                                       ErrorSeverity::Error, "A vertex influence set is malformed or cannot be normalized.",
                                                       "Provide unique finite positive weights for bounded palette joints."};
    const ErrorCodeDescriptor SkinningLayoutInvalid{AnimationDomain, ErrorCode{"animation.skinning.layout_invalid"}, ErrorSeverity::Error,
                                                    "Skeletal-mesh LOD, section, range, or bounds metadata is invalid.",
                                                    "Provide contiguous sections, valid bounds, and canonical LOD levels."};
}  // namespace Horo::Animation::AnimationErrors
