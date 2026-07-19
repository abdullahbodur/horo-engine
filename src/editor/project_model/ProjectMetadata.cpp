#include "editor/project_model/ProjectMetadata.h"

namespace Horo::Editor
{
namespace
{
[[nodiscard]] bool IsCompatible(const Application::ProjectCompatibilityStatus status) noexcept
{
    return status == Application::ProjectCompatibilityStatus::Current ||
           status == Application::ProjectCompatibilityStatus::CompatibleReleaseLine;
}
} // namespace

/** @copydoc PreflightProjectOpen */
ProjectOpenPreflight PreflightProjectOpen(const std::filesystem::path &projectRoot,
                                          const RendererAvailabilitySnapshot &availability)
{
    const Application::ProjectCompatibilitySnapshot compatibility =
        Application::InspectProjectCompatibility(projectRoot);
    if (!IsCompatible(compatibility.status))
    {
        const bool unreadable = compatibility.status == Application::ProjectCompatibilityStatus::Corrupt ||
                                compatibility.status == Application::ProjectCompatibilityStatus::Inaccessible;
        return ProjectOpenPreflight{.status = unreadable ? ProjectOpenPreflightStatus::ProjectMetadataUnreadable
                                                         : ProjectOpenPreflightStatus::ProjectCompatibilityBlocked,
                                    .diagnostic =
                                        compatibility.diagnostic.has_value()
                                            ? compatibility.diagnostic->message
                                            : "Project compatibility prevents this editor from opening the project."};
    }

    if (!compatibility.metadata.has_value())
    {
        return ProjectOpenPreflight{.status = ProjectOpenPreflightStatus::ProjectMetadataUnreadable,
                                    .diagnostic = "Compatibility inspection did not provide project metadata."};
    }

    const ProjectMetadata &metadata = *compatibility.metadata;
    const RendererBackendAvailability *backend = availability.Find(metadata.renderBackend);
    if (backend == nullptr || backend->state == RendererAvailabilityState::NotInstalled)
    {
        return ProjectOpenPreflight{.status = ProjectOpenPreflightStatus::RendererNotInstalled,
                                    .requiredBackendId = metadata.renderBackend,
                                    .projectName = metadata.name,
                                    .diagnostic = "Project renderer is not installed."};
    }
    if (backend->state == RendererAvailabilityState::RepairRequired ||
        backend->state == RendererAvailabilityState::SignatureInvalid ||
        backend->state == RendererAvailabilityState::Quarantined)
    {
        return ProjectOpenPreflight{.status = ProjectOpenPreflightStatus::RendererRepairRequired,
                                    .requiredBackendId = metadata.renderBackend,
                                    .projectName = metadata.name,
                                    .diagnostic = backend->diagnostic};
    }
    if (backend->state == RendererAvailabilityState::UpdateRequired)
    {
        return ProjectOpenPreflight{.status = ProjectOpenPreflightStatus::RendererUpdateRequired,
                                    .requiredBackendId = metadata.renderBackend,
                                    .projectName = metadata.name,
                                    .diagnostic = backend->diagnostic};
    }
    if (backend->state == RendererAvailabilityState::AbiMismatch)
    {
        return ProjectOpenPreflight{.status = ProjectOpenPreflightStatus::RendererCapabilityMismatch,
                                    .requiredBackendId = metadata.renderBackend,
                                    .projectName = metadata.name,
                                    .diagnostic = backend->diagnostic};
    }
    if (!backend->IsSelectable())
    {
        const std::string diagnostic = !backend->diagnostic.empty()
                                           ? backend->diagnostic
                                           : "Project renderer is not available on this editor installation.";
        return ProjectOpenPreflight{.status = ProjectOpenPreflightStatus::RendererUnavailable,
                                    .requiredBackendId = metadata.renderBackend,
                                    .projectName = metadata.name,
                                    .diagnostic = diagnostic};
    }
    if (metadata.renderBackend != availability.ActiveBackendId())
    {
        return ProjectOpenPreflight{.status = ProjectOpenPreflightStatus::RequiresRendererRestart,
                                    .requiredBackendId = metadata.renderBackend,
                                    .projectName = metadata.name};
    }
    return ProjectOpenPreflight{.status = ProjectOpenPreflightStatus::Ready,
                                .requiredBackendId = metadata.renderBackend,
                                .projectName = metadata.name};
}
} // namespace Horo::Editor
