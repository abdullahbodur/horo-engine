#pragma once

/**
 * @file AnimationErrors.h
 * @brief Stable animation identity, handle, and component-contract failures.
 */

#include "Horo/Foundation/ErrorCode.h"

namespace Horo::Animation::AnimationErrors {
    /** @brief A stable or runtime animation identity uses a reserved representation. */
    extern const ErrorCodeDescriptor IdentityInvalid;
    /** @brief A runtime handle is malformed or belongs to no published animation object. */
    extern const ErrorCodeDescriptor HandleMalformed;
    /** @brief A runtime handle belongs to another runtime or authored component owner. */
    extern const ErrorCodeDescriptor HandleOwnerMismatch;
    /** @brief A runtime handle or result targets a retired generation. */
    extern const ErrorCodeDescriptor HandleStale;
    /** @brief A runtime generation cannot advance without reusing a previously issued value. */
    extern const ErrorCodeDescriptor GenerationExhausted;
    /** @brief An animation component contains an invalid or contradictory contract. */
    extern const ErrorCodeDescriptor ComponentInvalid;
    /** @brief A runtime component does not match its immutable authored asset binding. */
    extern const ErrorCodeDescriptor ComponentBindingMismatch;
    /** @brief A component was produced for an unsupported animation contract version. */
    extern const ErrorCodeDescriptor ContractVersionUnsupported;
}  // namespace Horo::Animation::AnimationErrors
