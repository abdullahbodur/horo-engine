#pragma once

/**
 * @file RenderAdapterErrors.h
 * @brief Stable typed errors for adapter discovery and selection.
 */

#include "Horo/Foundation/ErrorCode.h"

namespace Horo::Render::RenderAdapterErrors {
    extern const ErrorCodeDescriptor AdapterUnavailable;
    extern const ErrorCodeDescriptor DiscoveryStopped;
    extern const ErrorCodeDescriptor InvalidDiscoveryRequest;
    extern const ErrorCodeDescriptor InvalidSelectionRequest;
    extern const ErrorCodeDescriptor InvalidSnapshot;
    extern const ErrorCodeDescriptor NoCompatibleAdapter;
    extern const ErrorCodeDescriptor RequiredAdapterNotFound;
    extern const ErrorCodeDescriptor StaleDiscovery;
}  // namespace Horo::Render::RenderAdapterErrors
