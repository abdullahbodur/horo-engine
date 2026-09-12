#pragma once

/**
 * @file PresentModeErrors.h
 * @brief Stable typed errors for present-mode negotiation.
 */

#include "Horo/Foundation/ErrorCode.h"

namespace Horo::Render::PresentModeErrors {
    extern const ErrorCodeDescriptor InvalidRequest;
    extern const ErrorCodeDescriptor InvalidCapabilities;
    extern const ErrorCodeDescriptor UnsupportedModeFact;
    extern const ErrorCodeDescriptor RequiredModeUnavailable;
    extern const ErrorCodeDescriptor NoPreferredModeAvailable;
}  // namespace Horo::Render::PresentModeErrors
