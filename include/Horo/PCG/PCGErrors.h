#pragma once

/**
 * @file PCGErrors.h
 * @brief Stable procedural-generation identity failures independent of runtime and target backends.
 */

#include "Horo/Foundation/ErrorCode.h"

namespace Horo::PCG::PCGErrors {
    /** @brief A PCG identity contains a reserved value. */
    extern const ErrorCodeDescriptor IdentityInvalid;
    /** @brief A PCG identity belongs to a different graph or logical owner. */
    extern const ErrorCodeDescriptor IdentityUnknown;
    /** @brief A PCG identity belongs to a retired graph revision or execution. */
    extern const ErrorCodeDescriptor IdentityStale;
    /** @brief A graph revision cannot advance without reusing a previously issued value. */
    extern const ErrorCodeDescriptor RevisionExhausted;
    /** @brief Canonical serialized PCG identity bytes contain a reserved value. */
    extern const ErrorCodeDescriptor SerializedIdentityInvalid;
    /** @brief A point schema or attribute key is malformed or contains an unknown type. */
    extern const ErrorCodeDescriptor PointSchemaInvalid;
    /** @brief Two schema fields or storage columns use the same canonical key. */
    extern const ErrorCodeDescriptor PointAttributeDuplicate;
    /** @brief A storage column does not exist in the captured schema. */
    extern const ErrorCodeDescriptor PointAttributeUnknown;
    /** @brief A storage column has a different type than its schema field. */
    extern const ErrorCodeDescriptor PointAttributeTypeMismatch;
    /** @brief Point core or column data violates its finite/value/length contract. */
    extern const ErrorCodeDescriptor PointDataInvalid;
    /** @brief A tier point, attribute, payload, or memory ceiling was exceeded. */
    extern const ErrorCodeDescriptor PointCapacityExceeded;
    /** @brief Checked PCG size arithmetic overflowed or used invalid alignment. */
    extern const ErrorCodeDescriptor PointSizeOverflow;
}  // namespace Horo::PCG::PCGErrors
