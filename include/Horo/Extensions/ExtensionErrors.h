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
    /** @brief Application capability registry input is malformed. */
    extern const ErrorCodeDescriptor CapabilityRegistryInvalid;
    /** @brief A provider already owns the exact capability contract version. */
    extern const ErrorCodeDescriptor CapabilityRegistryDuplicate;
    /** @brief No compatible provider contract version exists. */
    extern const ErrorCodeDescriptor CapabilityVersionIncompatible;
    /** @brief The bounded application capability registry is full. */
    extern const ErrorCodeDescriptor CapabilityRegistryCapacityExceeded;
    /** @brief The application capability registry is shutting down. */
    extern const ErrorCodeDescriptor CapabilityRegistryShutdown;
    /** @brief A backend-service descriptor or requested identity is malformed. */
    extern const ErrorCodeDescriptor BackendServiceInvalid;
    /** @brief A backend service identity already has a published provider. */
    extern const ErrorCodeDescriptor BackendServiceDuplicate;
    /** @brief No live backend service exists for the requested identity. */
    extern const ErrorCodeDescriptor BackendServiceUnavailable;
    /** @brief The resolved provider does not implement the requested stable contract. */
    extern const ErrorCodeDescriptor BackendServiceContractMismatch;
    /** @brief The caller's C++ contract type does not match the registered adapter. */
    extern const ErrorCodeDescriptor BackendServiceTypeMismatch;
    /** @brief A backend service was called outside its declared thread rule. */
    extern const ErrorCodeDescriptor BackendServiceThreadViolation;
    /** @brief A backend provider operation failed while preserving its typed cause. */
    extern const ErrorCodeDescriptor BackendServiceInvocationFailed;
    /** @brief A backend service operation was cooperatively cancelled. */
    extern const ErrorCodeDescriptor BackendServiceCancelled;
    /** @brief The bounded backend-service registry is full. */
    extern const ErrorCodeDescriptor BackendServiceCapacityExceeded;
    /** @brief Backend-service registration and invocation admission are closed. */
    extern const ErrorCodeDescriptor BackendServiceShutdown;
    /** @brief A project-validator descriptor, snapshot, finding, or limit is malformed. */
    extern const ErrorCodeDescriptor ProjectValidatorRegistryInvalid;
    /** @brief A project-validator identity already has a published provider. */
    extern const ErrorCodeDescriptor ProjectValidatorRegistryDuplicate;
    /** @brief The bounded project-validator provider registry is full. */
    extern const ErrorCodeDescriptor ProjectValidatorRegistryCapacityExceeded;
    /** @brief Project-validator registration and new validation admission are closed. */
    extern const ErrorCodeDescriptor ProjectValidatorRegistryShutdown;
    /** @brief One attributed project-validator callback or finding pass failed. */
    extern const ErrorCodeDescriptor ProjectValidatorInvocationFailed;
    /** @brief Project validation was cooperatively cancelled without publishing partial results. */
    extern const ErrorCodeDescriptor ProjectValidationCancelled;
}  // namespace Horo::Extensions::ExtensionErrors
