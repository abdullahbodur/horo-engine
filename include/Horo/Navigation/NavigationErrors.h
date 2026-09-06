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
}  // namespace Horo::Navigation::NavigationErrors
