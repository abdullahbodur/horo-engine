#pragma once

/**
 * @file XRErrors.h
 * @brief Stable backend-neutral XR failure identities.
 */

#include "Horo/Foundation/ErrorCode.h"

#include <span>

namespace Horo::XR::XRErrors {
    /** @brief An XR contract version uses an invalid representation. */
    extern const ErrorCodeDescriptor ContractVersionInvalid;
    /** @brief A provided XR contract version cannot satisfy the required version. */
    extern const ErrorCodeDescriptor ContractVersionIncompatible;
    /** @brief An XR identity uses an invalid or malformed representation. */
    extern const ErrorCodeDescriptor IdentityInvalid;
    /** @brief An XR identity belongs to a replaced owner generation. */
    extern const ErrorCodeDescriptor IdentityStale;
    /** @brief An immutable XR capability descriptor is malformed. */
    extern const ErrorCodeDescriptor CapabilityDescriptorInvalid;
    /** @brief XR capability evidence changed after the caller captured its revision. */
    extern const ErrorCodeDescriptor CapabilityStale;
    /** @brief The selected XR system permanently does not support the requested operation. */
    extern const ErrorCodeDescriptor OperationUnsupported;
    /** @brief A known XR operation is not currently available. */
    extern const ErrorCodeDescriptor OperationUnavailable;
    /** @brief An XR operation conflicts with the active contract or system configuration. */
    extern const ErrorCodeDescriptor OperationIncompatible;
    /** @brief An XR request exceeds an immutable system capacity limit. */
    extern const ErrorCodeDescriptor CapacityExceeded;
    /** @brief An XR operation request is malformed. */
    extern const ErrorCodeDescriptor OperationInvalid;
    /** @brief An XR coordinate-space relation or convention is incompatible. */
    extern const ErrorCodeDescriptor CoordinateSpaceIncompatible;
    /** @brief XR clock evidence mixes or omits required time domains. */
    extern const ErrorCodeDescriptor TimeDomainIncompatible;
    /** @brief XR pose component validity, value, confidence, or loss evidence is malformed. */
    extern const ErrorCodeDescriptor PoseInvalid;
    /** @brief An XR space or pose references a retired world-origin revision. */
    extern const ErrorCodeDescriptor OriginRevisionStale;

    /**
     * @brief Returns every stable XRApi descriptor for module-registry contribution.
     * @return Bounded immutable descriptor references owned for process lifetime by XRApi.
     */
    [[nodiscard]] std::span<const ErrorCodeDescriptor *const> Descriptors() noexcept;
}  // namespace Horo::XR::XRErrors
