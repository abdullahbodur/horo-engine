#pragma once

/**
 * @file ProjectMigrationCatalog.h
 * @brief Public access to the immutable build-generated core migration catalog.
 */

#include "Horo/Application/ProjectMigration.h"

namespace Horo::Application
{
/** @brief Builds the frozen migration support policy for the current Horo release. */
[[nodiscard]] Result<ProjectMigrationSupportDescriptor> BuildBuiltInProjectMigrationSupportDescriptor();
} // namespace Horo::Application
