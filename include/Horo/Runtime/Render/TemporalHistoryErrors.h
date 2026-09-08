#pragma once

/**
 * @file TemporalHistoryErrors.h
 * @brief Stable typed error descriptors for temporal render history ownership.
 */

#include "Horo/Foundation/ErrorCode.h"

namespace Horo::Render::TemporalHistoryErrors {
    extern const ErrorCodeDescriptor AllocationFailed;
    extern const ErrorCodeDescriptor CapacityExceeded;
    extern const ErrorCodeDescriptor FrameAlreadyPending;
    extern const ErrorCodeDescriptor InvalidDescriptor;
    extern const ErrorCodeDescriptor InvalidFrame;
    extern const ErrorCodeDescriptor InvalidHandle;
    extern const ErrorCodeDescriptor InvalidLimits;
    extern const ErrorCodeDescriptor InvalidResetCause;
    extern const ErrorCodeDescriptor OwnerExhausted;
    extern const ErrorCodeDescriptor ResetRequired;
    extern const ErrorCodeDescriptor StoreStopped;
    extern const ErrorCodeDescriptor WrongOwner;
    extern const ErrorCodeDescriptor WrongThread;
}  // namespace Horo::Render::TemporalHistoryErrors
