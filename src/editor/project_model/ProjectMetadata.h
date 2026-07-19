#pragma once

#include "Horo/Application/ProjectCompatibility.h"
#include "editor/project_model/RendererAvailability.h"

#include <filesystem>
#include <string>

namespace Horo::Editor
{
/**
 * @file ProjectMetadata.h
 * @brief Project identity and renderer startup preflight contracts.
 */

using ProjectMetadata = Application::ProjectMetadata;

/** @brief Result category for project renderer startup preflight. */
enum class ProjectOpenPreflightStatus
{
    Ready,
    RequiresRendererRestart,
    RendererNotInstalled,
    RendererUnavailable,
    RendererRepairRequired,
    RendererUpdateRequired,
    RendererCapabilityMismatch,
    ProjectCompatibilityBlocked,
    ProjectMetadataUnreadable,
};

/** @brief Decision produced before entering project loading or workspace routes. */
struct ProjectOpenPreflight
{
    ProjectOpenPreflightStatus status = ProjectOpenPreflightStatus::ProjectMetadataUnreadable;
    std::string requiredBackendId;
    std::string projectName;
    std::string diagnostic;
};

/**
 * @brief Resolves whether a project may open in the current renderer composition.
 * @param projectRoot Project root to inspect.
 * @param availability Current machine-local renderer availability snapshot.
 * @return Explicit ready, restart-required, unavailable, or metadata failure decision.
 */
[[nodiscard]] ProjectOpenPreflight PreflightProjectOpen(const std::filesystem::path &projectRoot,
                                                        const RendererAvailabilitySnapshot &availability);
} // namespace Horo::Editor
