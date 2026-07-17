#include "editor/project_model/ProjectMetadata.h"
#include "editor/project_model/RendererAvailability.h"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>

namespace
{
using namespace Horo::Editor;

class TemporaryProject
{
  public:
    TemporaryProject()
    {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        root_ = std::filesystem::temp_directory_path() / ("horo-project-metadata-" + std::to_string(stamp));
        std::filesystem::create_directories(root_ / ".horo");
    }

    ~TemporaryProject()
    {
        std::error_code error;
        std::filesystem::remove_all(root_, error);
    }

    [[nodiscard]] const std::filesystem::path &Root() const noexcept
    {
        return root_;
    }

    void WriteMetadata(const std::string &renderer) const
    {
        std::ofstream output(root_ / ".horo/project.json");
        output << "{\n"
                  "  \"formatVersion\": 1,\n"
                  "  \"projectId\": \"proj_test\",\n"
                  "  \"name\": \"Renderer Project\",\n"
                  "  \"settings\": { \"renderBackend\": \""
               << renderer << "\" }\n}\n";
    }

    void WriteRawMetadata(const std::string &contents) const
    {
        std::ofstream output(root_ / ".horo/project.json", std::ios::binary);
        output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    }

  private:
    std::filesystem::path root_;
};

RendererAvailabilitySnapshot Availability()
{
    return RendererAvailabilitySnapshot{
        {
            RendererBackendAvailability{"opengl", "OpenGL", RendererAvailabilityState::Available, {}},
            RendererBackendAvailability{"metal", "Metal", RendererAvailabilityState::Active, {}},
            RendererBackendAvailability{"vulkan", "Vulkan", RendererAvailabilityState::NotInstalled,
                                        "Renderer component is not installed."},
        },
        "metal"};
}

void LoadsPersistedBackendAndRequiresCompositionRestartWhenDifferent()
{
    TemporaryProject project;
    project.WriteMetadata("opengl");

    const auto metadata = LoadProjectMetadata(project.Root());
    assert(metadata.HasValue());
    assert(metadata.Value().renderBackend == "opengl");

    const ProjectOpenPreflight preflight = PreflightProjectOpen(project.Root(), Availability());
    assert(preflight.status == ProjectOpenPreflightStatus::RequiresRendererRestart);
    assert(preflight.requiredBackendId == "opengl");
}

void AcceptsProjectWhenPersistedBackendIsAlreadyActive()
{
    TemporaryProject project;
    project.WriteMetadata("metal");

    const ProjectOpenPreflight preflight = PreflightProjectOpen(project.Root(), Availability());
    assert(preflight.status == ProjectOpenPreflightStatus::Ready);
    assert(preflight.requiredBackendId == "metal");
}

void RejectsUnavailablePersistedBackendWithoutFallback()
{
    TemporaryProject project;
    project.WriteMetadata("vulkan");

    const ProjectOpenPreflight preflight = PreflightProjectOpen(project.Root(), Availability());
    assert(preflight.status == ProjectOpenPreflightStatus::RendererNotInstalled);
    assert(preflight.requiredBackendId == "vulkan");
}

void ReportsUnreadableMetadata()
{
    TemporaryProject project;
    const ProjectOpenPreflight preflight = PreflightProjectOpen(project.Root(), Availability());
    assert(preflight.status == ProjectOpenPreflightStatus::ProjectMetadataUnreadable);
}

void PreservesActionableRendererResolutionStates()
{
    TemporaryProject project;
    project.WriteMetadata("vulkan");

    const auto preflightFor = [&project](const RendererAvailabilityState state) {
        const RendererAvailabilitySnapshot availability{
            {RendererBackendAvailability{"vulkan", "Vulkan", state, "Action required."}}, "metal"};
        return PreflightProjectOpen(project.Root(), availability).status;
    };

    assert(preflightFor(RendererAvailabilityState::RepairRequired) ==
           ProjectOpenPreflightStatus::RendererRepairRequired);
    assert(preflightFor(RendererAvailabilityState::UpdateRequired) ==
           ProjectOpenPreflightStatus::RendererUpdateRequired);
    assert(preflightFor(RendererAvailabilityState::AbiMismatch) ==
           ProjectOpenPreflightStatus::RendererCapabilityMismatch);
}

void RejectsMalformedNestedAndDuplicateMetadata()
{
    TemporaryProject project;
    project.WriteRawMetadata(
        R"({"formatVersion":1.5,"projectId":"p","name":"n","settings":{"renderBackend":"metal"}})");
    assert(LoadProjectMetadata(project.Root()).HasError());

    project.WriteRawMetadata(
        R"({"formatVersion":1,"projectId":"p","name":"renderBackend: metal","other":{"renderBackend":"metal"}})");
    assert(LoadProjectMetadata(project.Root()).HasError());

    project.WriteRawMetadata(
        R"({"formatVersion":1,"projectId":"p","name":"n","settings":{"renderBackend":"metal","renderBackend":"opengl"}})");
    assert(LoadProjectMetadata(project.Root()).HasError());
}

void AcceptsUnicodeEscapesAndRejectsOversizedMetadata()
{
    TemporaryProject project;
    project.WriteRawMetadata(
        R"({"formatVersion":1,"projectId":"p","name":"Horo \u0130stanbul","settings":{"renderBackend":"metal"}})");
    const Horo::Result<ProjectMetadata> unicodeMetadata = LoadProjectMetadata(project.Root());
    assert(unicodeMetadata.HasValue());
    assert(!unicodeMetadata.Value().name.empty());

    project.WriteRawMetadata(std::string(64U * 1024U + 1U, 'x'));
    assert(LoadProjectMetadata(project.Root()).HasError());
}
} // namespace

int main()
{
    LoadsPersistedBackendAndRequiresCompositionRestartWhenDifferent();
    AcceptsProjectWhenPersistedBackendIsAlreadyActive();
    RejectsUnavailablePersistedBackendWithoutFallback();
    ReportsUnreadableMetadata();
    PreservesActionableRendererResolutionStates();
    RejectsMalformedNestedAndDuplicateMetadata();
    AcceptsUnicodeEscapesAndRejectsOversizedMetadata();
    return 0;
}
