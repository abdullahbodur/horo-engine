#pragma once

/**
 * @file CinematicErrors.h
 * @brief Stable cinematic identity errors independent of runtime and editor services.
 */

#include "Horo/Foundation/ErrorCode.h"

namespace Horo::Cinematic::CinematicErrors {
    /** @brief An identity contains a reserved stable value or generation. */
    extern const ErrorCodeDescriptor IdentityInvalid;
    /** @brief An identity does not name the requested authored object. */
    extern const ErrorCodeDescriptor IdentityUnknown;
    /** @brief An identity names an authored object with a retired generation. */
    extern const ErrorCodeDescriptor IdentityStale;
    /** @brief A generation cannot advance without wrapping into a reserved value. */
    extern const ErrorCodeDescriptor GenerationExhausted;
    /** @brief Canonical serialized identity bytes contain a reserved value. */
    extern const ErrorCodeDescriptor SerializedIdentityInvalid;
}  // namespace Horo::Cinematic::CinematicErrors
