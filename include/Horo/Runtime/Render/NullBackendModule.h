#pragma once

/**
 * @file NullBackendModule.h
 * @brief Explicit composition entry point for the built-in null renderer module.
 */

#include "Horo/Runtime/Render/RenderBackendRegistry.h"

namespace Horo::Render
{
/**
 * @brief Registers the side-effect-free null renderer provider.
 * @param registry Host-owned registry still open for composition.
 * @return Success or the registry's typed validation error.
 */
[[nodiscard]] Result<void> RegisterNullRenderBackend(RenderBackendRegistry &registry);
} // namespace Horo::Render
