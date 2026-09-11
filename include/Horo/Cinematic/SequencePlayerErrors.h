#pragma once

/**
 * @file SequencePlayerErrors.h
 * @brief Stable failures for the backend-neutral sequence-player state machine.
 */

#include "Horo/Foundation/ErrorCode.h"

namespace Horo::Cinematic::SequencePlayerErrors {
    /** @brief A player handle contains a reserved session or player identity. */
    extern const ErrorCodeDescriptor HandleInvalid;
    /** @brief A player handle targets a different session or player. */
    extern const ErrorCodeDescriptor HandleUnknown;
    /** @brief A player handle or operation fence names a retired generation or revision. */
    extern const ErrorCodeDescriptor HandleStale;
    /** @brief The requested command is not admitted from the current playback state. */
    extern const ErrorCodeDescriptor TransitionInvalid;
    /** @brief A timeline position is outside the player's immutable sequence bounds. */
    extern const ErrorCodeDescriptor TimeInvalid;
    /** @brief A playback rate is malformed or exceeds the bounded supported range. */
    extern const ErrorCodeDescriptor RateInvalid;
    /** @brief The monotonic player control revision cannot advance without wrapping. */
    extern const ErrorCodeDescriptor RevisionExhausted;
}  // namespace Horo::Cinematic::SequencePlayerErrors
