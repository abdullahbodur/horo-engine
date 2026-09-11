#pragma once

#include "Horo/Foundation/ErrorCode.h"

namespace Horo::Extensions::ExtensionErrors {
    extern const ErrorCodeDescriptor InvalidManifest;
    extern const ErrorCodeDescriptor LoadFailed;
    extern const ErrorCodeDescriptor MissingEntryPoint;
    extern const ErrorCodeDescriptor ContributionRejected;
    extern const ErrorCodeDescriptor InvocationFailed;
    extern const ErrorCodeDescriptor ModuleResolutionFailed;
    /** @brief A lifecycle transition used the wrong owner, ordering, or state evidence. */
    extern const ErrorCodeDescriptor LifecycleTransitionInvalid;
    /** @brief A lifecycle transition targeted a state revision that is no longer current. */
    extern const ErrorCodeDescriptor LifecycleRevisionStale;
    /** @brief A lifecycle revision, generation, message, or audit-history bound was exhausted. */
    extern const ErrorCodeDescriptor LifecycleCapacityExceeded;
    /** @brief Capability admission policy or manifest-derived request is malformed. */
    extern const ErrorCodeDescriptor CapabilityAdmissionInvalid;
    /** @brief A required permission is unknown, unapproved, or belongs to another activation. */
    extern const ErrorCodeDescriptor PermissionDenied;
    /** @brief A capability is absent from the host composition or exact admitted set. */
    extern const ErrorCodeDescriptor CapabilityUnavailable;
    /** @brief A retained capability handle outlived its activation admission. */
    extern const ErrorCodeDescriptor CapabilityRevoked;
}  // namespace Horo::Extensions::ExtensionErrors
