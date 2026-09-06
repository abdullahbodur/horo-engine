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
}  // namespace Horo::Network::NetworkErrors
