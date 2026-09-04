#pragma once

/**
 * @file AudioErrors.h
 * @brief Stable backend-neutral audio failure identities.
 */

#include "Horo/Foundation/ErrorCode.h"

namespace Horo::Audio::AudioErrors {
    extern const ErrorCodeDescriptor ResamplerInvalid;
    extern const ErrorCodeDescriptor ResamplerBudgetExceeded;
    extern const ErrorCodeDescriptor CommandBufferInvalid;
    extern const ErrorCodeDescriptor MemoryInvalid;
    extern const ErrorCodeDescriptor MemoryBudgetExceeded;
    extern const ErrorCodeDescriptor MemoryAllocationFailed;
    extern const ErrorCodeDescriptor IdentityInvalid;
    extern const ErrorCodeDescriptor HandleMalformed;
    extern const ErrorCodeDescriptor HandleOwnerMismatch;
    extern const ErrorCodeDescriptor HandleStale;
    extern const ErrorCodeDescriptor HandleCapacityExhausted;
    extern const ErrorCodeDescriptor HandleGenerationExhausted;
    extern const ErrorCodeDescriptor CapabilityUnavailable;
    extern const ErrorCodeDescriptor OperationUnsupported;
    extern const ErrorCodeDescriptor OperationCancelled;
    extern const ErrorCodeDescriptor RuntimeInactive;
    extern const ErrorCodeDescriptor DeviceUnavailable;
    extern const ErrorCodeDescriptor BackendFailed;
}  // namespace Horo::Audio::AudioErrors
