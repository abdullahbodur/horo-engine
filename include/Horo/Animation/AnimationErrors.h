#pragma once

/**
 * @file AnimationErrors.h
 * @brief Stable animation identity, handle, and component-contract failures.
 */

#include "Horo/Foundation/ErrorCode.h"

namespace Horo::Animation::AnimationErrors {
    /** @brief A stable or runtime animation identity uses a reserved representation. */
    extern const ErrorCodeDescriptor IdentityInvalid;
    /** @brief A runtime handle is malformed or belongs to no published animation object. */
    extern const ErrorCodeDescriptor HandleMalformed;
    /** @brief A runtime handle belongs to another runtime or authored component owner. */
    extern const ErrorCodeDescriptor HandleOwnerMismatch;
    /** @brief A runtime handle or result targets a retired generation. */
    extern const ErrorCodeDescriptor HandleStale;
    /** @brief A runtime generation cannot advance without reusing a previously issued value. */
    extern const ErrorCodeDescriptor GenerationExhausted;
    /** @brief An animation component contains an invalid or contradictory contract. */
    extern const ErrorCodeDescriptor ComponentInvalid;
    /** @brief A runtime component does not match its immutable authored asset binding. */
    extern const ErrorCodeDescriptor ComponentBindingMismatch;
    /** @brief A component was produced for an unsupported animation contract version. */
    extern const ErrorCodeDescriptor ContractVersionUnsupported;
    /** @brief A skeleton asset was produced for an unsupported contract version. */
    extern const ErrorCodeDescriptor SkeletonVersionUnsupported;
    /** @brief Skeleton validation is closed because the owning boundary is unavailable or shutting down. */
    extern const ErrorCodeDescriptor SkeletonAdmissionRejected;
    /** @brief Skeleton validation was cancelled before immutable publication. */
    extern const ErrorCodeDescriptor SkeletonValidationCancelled;
    /** @brief A reload candidate does not replace the declared stable skeleton identity. */
    extern const ErrorCodeDescriptor SkeletonReloadMismatch;
    /** @brief Skeleton counts, hierarchy depth, or names exceed the captured finite limits. */
    extern const ErrorCodeDescriptor SkeletonLimitExceeded;
    /** @brief Stable joint or socket identity is duplicated within one skeleton asset. */
    extern const ErrorCodeDescriptor SkeletonDuplicateIdentity;
    /** @brief A joint or socket references a joint absent from the candidate skeleton. */
    extern const ErrorCodeDescriptor SkeletonJointMissing;
    /** @brief The candidate parent graph contains a cycle and has no complete topological order. */
    extern const ErrorCodeDescriptor SkeletonHierarchyCycle;
    /** @brief A reference transform or inverse bind matrix is malformed or mutually inconsistent. */
    extern const ErrorCodeDescriptor SkeletonTransformInvalid;
    /** @brief Retarget, mirror, side, name, or socket metadata is malformed or contradictory. */
    extern const ErrorCodeDescriptor SkeletonMetadataInvalid;
}  // namespace Horo::Animation::AnimationErrors
