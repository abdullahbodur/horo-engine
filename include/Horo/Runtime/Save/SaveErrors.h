#pragma once

/**
 * @file SaveErrors.h
 * @brief Stable errors for runtime-save value validation and bounded snapshot construction.
 */

#include "Horo/Foundation/ErrorCode.h"

namespace Horo::Runtime::SaveErrors {
    /** @brief A persistent save identity was missing or used the reserved all-zero value. */
    extern const ErrorCodeDescriptor IdentityInvalid;
    /** @brief Persistent identity text or bytes were not in the canonical representation. */
    extern const ErrorCodeDescriptor IdentityMalformed;
    /** @brief A collection contained the same persistent identity more than once. */
    extern const ErrorCodeDescriptor IdentityDuplicate;
    /** @brief A participant type identity was empty, oversized, or noncanonical. */
    extern const ErrorCodeDescriptor ParticipantIdInvalid;
    /** @brief A schema or format version used the reserved zero value. */
    extern const ErrorCodeDescriptor VersionInvalid;
    /** @brief Input requires a newer schema or format version than the reader supports. */
    extern const ErrorCodeDescriptor VersionUnsupportedNewer;
}  // namespace Horo::Runtime::SaveErrors
