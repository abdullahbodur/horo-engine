#pragma once

/**
 * @file NetworkErrors.h
 * @brief Stable backend-neutral network error identities.
 */

#include "Horo/Foundation/ErrorCode.h"

namespace Horo::Network::NetworkErrors {
    /** @brief A stable network identity uses its reserved zero representation. */
    extern const ErrorCodeDescriptor IdentityInvalid;
    /** @brief Replication descriptor metadata is malformed or exceeds its declared bounds. */
    extern const ErrorCodeDescriptor ReplicationDescriptorInvalid;
    /** @brief Schema, field, or tombstone identities collide in one candidate snapshot. */
    extern const ErrorCodeDescriptor ReplicationDescriptorConflict;
    /** @brief A replacement reuses stable identity with incompatible semantics. */
    extern const ErrorCodeDescriptor ReplicationDescriptorIncompatible;
    /** @brief An exact replication schema identity is absent from the pinned snapshot. */
    extern const ErrorCodeDescriptor ReplicationSchemaUnknown;
    /** @brief Descriptor snapshot construction exceeded its explicit finite capacity. */
    extern const ErrorCodeDescriptor ReplicationCapacityExceeded;
    /** @brief Transport capability evidence or a bounded requirement is malformed. */
    extern const ErrorCodeDescriptor TransportCapabilityDescriptorInvalid;
    /** @brief Transport capability evidence changed after the caller captured its revision. */
    extern const ErrorCodeDescriptor TransportCapabilityStale;
    /** @brief The explicit transport candidate cannot provide an exact required delivery semantic. */
    extern const ErrorCodeDescriptor TransportDeliveryUnsupported;
    /** @brief Known transport functionality is not currently available. */
    extern const ErrorCodeDescriptor TransportCapabilityUnavailable;
    /** @brief A channel, message, or deadline requirement exceeds the candidate's finite limits. */
    extern const ErrorCodeDescriptor TransportLimitExceeded;
    /** @brief Caller-owned cancellation rejected transport admission before queue mutation. */
    extern const ErrorCodeDescriptor TransportOperationCancelled;
    /** @brief Caller-owned shutdown state rejected transport admission before queue mutation. */
    extern const ErrorCodeDescriptor TransportShuttingDown;
}  // namespace Horo::Network::NetworkErrors
