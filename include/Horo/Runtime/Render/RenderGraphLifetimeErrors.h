#pragma once

/**
 * @file RenderGraphLifetimeErrors.h
 * @brief Stable typed errors for render-graph lifetime planning.
 */

#include "Horo/Foundation/ErrorCode.h"

namespace Horo::Render::RenderGraphLifetimeErrors {
    extern const ErrorCodeDescriptor AllocationFailed;
    extern const ErrorCodeDescriptor CapacityExceeded;
    extern const ErrorCodeDescriptor DescriptorInvalid;
    extern const ErrorCodeDescriptor DuplicateRequirement;
    extern const ErrorCodeDescriptor InvalidGraph;
    extern const ErrorCodeDescriptor InvalidLimits;
    extern const ErrorCodeDescriptor InvalidSchedule;
    extern const ErrorCodeDescriptor MissingRequirement;
    extern const ErrorCodeDescriptor OwnerMismatch;
    extern const ErrorCodeDescriptor UnexpectedRequirement;
}  // namespace Horo::Render::RenderGraphLifetimeErrors
