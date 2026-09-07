#pragma once

/**
 * @file VfxErrors.h
 * @brief Stable VFX identity errors independent of simulation and rendering backends.
 */

#include "Horo/Foundation/ErrorCode.h"

namespace Horo::Vfx::VfxErrors {
    /** @brief An identity contains a reserved scope, slot, or generation value. */
    extern const ErrorCodeDescriptor IdentityInvalid;
    /** @brief An identity does not name the requested owner slot. */
    extern const ErrorCodeDescriptor IdentityUnknown;
    /** @brief An identity names a known slot with a retired generation. */
    extern const ErrorCodeDescriptor IdentityStale;
    /** @brief A generation cannot advance without wrapping into a reserved value. */
    extern const ErrorCodeDescriptor GenerationExhausted;
    /** @brief Canonical serialized identity bytes are malformed. */
    extern const ErrorCodeDescriptor SerializedIdentityInvalid;
}  // namespace Horo::Vfx::VfxErrors
