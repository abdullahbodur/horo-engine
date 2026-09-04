#pragma once

/** @file PhysicsErrors.h
 * @brief Stable Horo physics error identities, never native solver codes or messages.
 */

#include "Horo/Foundation/ErrorCode.h"

namespace Horo::Physics::PhysicsErrors {
    /** @brief Missing or invalid published-world identity. */
    extern const ErrorCodeDescriptor WorldInvalid;
    /** @brief Missing world, invalid slot index or zero slot generation. */
    extern const ErrorCodeDescriptor HandleMalformed;
    /** @brief A well-formed handle belongs to a different world generation. */
    extern const ErrorCodeDescriptor HandleWorldMismatch;
    /** @brief The owning registry found an absent, retired or replaced slot generation. */
    extern const ErrorCodeDescriptor HandleStale;
    /** @brief A registry cannot issue another generation without reusing an old identity. */
    extern const ErrorCodeDescriptor GenerationExhausted;
    /** @brief Required Physics functionality is absent from the admitted composition. */
    extern const ErrorCodeDescriptor CapabilityUnavailable;
    /** @brief An operation is not supported by the selected qualified Physics profile. */
    extern const ErrorCodeDescriptor OperationUnsupported;
    /** @brief The world lifecycle phase cannot admit the requested operation. */
    extern const ErrorCodeDescriptor InvalidState;
}  // namespace Horo::Physics::PhysicsErrors
