#pragma once

/**
 * @file HeadlessMeshCooker.h
 * @brief Built-in headless mesh validation cooker contribution.
 */

#include "Horo/Assets/CookCatalog.h"

#include <memory>

namespace Horo::Assets
{

/**
 * @brief Factory that creates the built-in headless mesh validation cooker.
 * @return Shared pointer to an ICookerStrategy that validates mesh source presence
 *         and produces a deterministic metadata payload for the headless-null target.
 */
[[nodiscard]] std::shared_ptr<const ICookerStrategy> CreateHeadlessMeshCooker();

/**
 * @brief Registers the headless mesh cooker into the given catalog.
 * @param catalog Unsealed catalog to register into.
 * @return Result<void> with the catalog's own registration result.
 */
[[nodiscard]] Result<void> RegisterHeadlessMeshCooker(CookerCatalog &catalog);

} // namespace Horo::Assets
