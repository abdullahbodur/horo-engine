#pragma once

/**
 * @file NavigationErrors.h
 * @brief Stable Horo navigation error identities independent of provider-native failures.
 */

#include "Horo/Foundation/ErrorCode.h"

namespace Horo::Navigation::NavigationErrors {
    /** @brief A stable or runtime navigation identity uses its reserved invalid representation. */
    extern const ErrorCodeDescriptor IdentityInvalid;
    /** @brief A runtime navigation handle is malformed, foreign, or generation-stale. */
    extern const ErrorCodeDescriptor InvalidHandle;
    /** @brief A navigation generation cannot advance without reusing an issued identity. */
    extern const ErrorCodeDescriptor GenerationExhausted;
    /** @brief Capability evidence, requested query identity, quality, or finite limits are malformed. */
    extern const ErrorCodeDescriptor CapabilityDescriptorInvalid;
    /** @brief Provider capability evidence changed after the caller captured its revision. */
    extern const ErrorCodeDescriptor CapabilityStale;
    /** @brief The selected provider permanently does not implement the requested operation or quality. */
    extern const ErrorCodeDescriptor OperationUnsupported;
    /** @brief Known provider functionality is not currently available. */
    extern const ErrorCodeDescriptor CapabilityUnavailable;
    /** @brief Requested finite work or output bounds exceed the provider declaration. */
    extern const ErrorCodeDescriptor QueryLimitExceeded;
    /** @brief Queue, request-record, or admission capacity rejected work before a handle or job existed. */
    extern const ErrorCodeDescriptor AdmissionRejected;
    /** @brief An admitted navigation query was cancelled before owner-thread publication. */
    extern const ErrorCodeDescriptor QueryCancelled;
    /** @brief An admitted query result belongs to an old navigation topology generation. */
    extern const ErrorCodeDescriptor StaleSnapshot;
    /** @brief Required navigation coverage or active world data is unavailable. */
    extern const ErrorCodeDescriptor NoNavigationData;
    /** @brief Provider execution failed after admission; normalized Horo diagnostics carry actionable detail. */
    extern const ErrorCodeDescriptor ProviderFailed;
    /** @brief A terminal outcome or its bounded provenance/coverage evidence is malformed. */
    extern const ErrorCodeDescriptor OutcomeDescriptorInvalid;
    /** @brief An accepted request belongs to a revoked or replacement navigation-world incarnation. */
    extern const ErrorCodeDescriptor InvalidWorld;
    /** @brief An accepted operation exhausted a declared execution, scratch, result, or retry bound. */
    extern const ErrorCodeDescriptor CapacityExceeded;
    /** @brief An area identity, source, or finite non-negative traversal cost is malformed. */
    extern const ErrorCodeDescriptor AreaDescriptorInvalid;
    /** @brief A filter identity, source, referenced identity, or finite non-negative cost is malformed. */
    extern const ErrorCodeDescriptor FilterDescriptorInvalid;
    /** @brief Area, filter, or filter-override identities collide within one deterministic registry. */
    extern const ErrorCodeDescriptor DescriptorConflict;
    /** @brief An exact area identity is absent and cannot be replaced by a default area. */
    extern const ErrorCodeDescriptor AreaUnknown;
    /** @brief An exact filter identity is absent and cannot be replaced by a default filter. */
    extern const ErrorCodeDescriptor FilterUnknown;
}  // namespace Horo::Navigation::NavigationErrors
