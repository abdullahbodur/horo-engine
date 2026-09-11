#pragma once

/**
 * @file SequenceEvaluationErrors.h
 * @brief Stable failures for deterministic frame-hot sequence evaluation.
 */

#include "Horo/Foundation/ErrorCode.h"

namespace Horo::Cinematic::SequenceEvaluationErrors {
    /** @brief Activation data, cursor state, or callback contracts are malformed. */
    extern const ErrorCodeDescriptor Malformed;
    /** @brief A cursor or command was produced for an older player generation or control revision. */
    extern const ErrorCodeDescriptor Stale;
    /** @brief The player is not currently eligible for authoritative evaluation. */
    extern const ErrorCodeDescriptor PlayerStateInvalid;
    /** @brief The supplied source delta is negative. */
    extern const ErrorCodeDescriptor DeltaInvalid;
    /** @brief Exact rate or timeline arithmetic cannot be represented without wrapping. */
    extern const ErrorCodeDescriptor ArithmeticOverflow;
    /** @brief A bounded track, crossing, output, or player-order capacity was exceeded. */
    extern const ErrorCodeDescriptor CapacityExceeded;
    /** @brief A crossed event or camera cut has no required typed destination hook. */
    extern const ErrorCodeDescriptor HookUnavailable;
    /** @brief The monotonic evaluation revision cannot advance without wrapping. */
    extern const ErrorCodeDescriptor RevisionExhausted;
}  // namespace Horo::Cinematic::SequenceEvaluationErrors
