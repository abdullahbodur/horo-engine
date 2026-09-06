#pragma once

/**
 * @file AIErrors.h
 * @brief Stable gameplay-AI identity errors independent of runtime storage.
 */

#include "Horo/Foundation/ErrorCode.h"

namespace Horo::AI::AIErrors {
    /** @brief A persistent AI identity uses its reserved zero representation. */
    extern const ErrorCodeDescriptor IdentityInvalid;
    /** @brief Two descriptors collide within one persistent identity domain. */
    extern const ErrorCodeDescriptor DescriptorConflict;
    /** @brief A descriptor set exceeds its bounded validation capacity. */
    extern const ErrorCodeDescriptor DescriptorLimitExceeded;
    /** @brief A runtime handle is malformed or belongs to another scene-runtime incarnation. */
    extern const ErrorCodeDescriptor HandleInvalid;
    /** @brief A runtime slot generation cannot advance without wrapping. */
    extern const ErrorCodeDescriptor GenerationExhausted;
}  // namespace Horo::AI::AIErrors
