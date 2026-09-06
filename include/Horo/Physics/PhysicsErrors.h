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
    /** @brief A mutable Physics operation was attempted outside its owning thread. */
    extern const ErrorCodeDescriptor ThreadAffinityViolation;
    /** @brief Solver child work exceeded its fixed-tick deadline and was cooperatively cancelled. */
    extern const ErrorCodeDescriptor SolverDeadlineExceeded;
    /** @brief Malformed or unsupported-version descriptor metadata. */
    extern const ErrorCodeDescriptor DescriptorInvalid;
    /** @brief Unknown or unsupported numerical/solver profile. */
    extern const ErrorCodeDescriptor ProfileUnsupported;
    /** @brief Requested capacity exceeds the bounded profile or lacks its required budget. */
    extern const ErrorCodeDescriptor CapacityExceeded;
    /** @brief Admission evidence changed after the caller captured its capability revision. */
    extern const ErrorCodeDescriptor CapabilityStale;
    /** @brief Candidate or process initialization failed after releasing acquired resources. */
    extern const ErrorCodeDescriptor InitializationFailed;
}  // namespace Horo::Physics::PhysicsErrors
