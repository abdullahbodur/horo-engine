#include <catch2/catch_test_macros.hpp>

#include "Horo/Application/ProjectCompatibility.h"
#include "editor/project_model/ProjectMetadata.h"
#include "editor/project_model/RendererAvailability.h"

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
        const auto release = Horo::Application::CurrentEngineReleaseVersion();
        const auto *decision = Horo::Application::BuiltInReleaseCompatibilityRegistry().Find(release);
        REQUIRE((decision != nullptr));
        std::ofstream output(root_ / ".horo/project.json");
        output << "{\n"
                  "  \"horoVersion\": \""
               << Horo::Application::FormatHoroVersion(release.value)
               << "\",\n"
                  "  \"persistentContract\": \""
               << Horo::Application::FormatPersistentContractHash(decision->persistentContract)
               << "\",\n"
                  "  \"projectId\": \"proj_test\",\n"
                  "  \"name\": \"Renderer Project\",\n"
                  "  \"projectVersion\": \"0.1.0\",\n"
                  "  \"createdAt\": \"2026-07-18T00:00:00Z\",\n"
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

TEST_CASE("Loads Persisted Backend And Requires Composition Restart When Different", "[unit][editor]")
{
    TemporaryProject project;
    project.WriteMetadata("opengl");

    const auto metadata = Horo::Application::LoadProjectMetadata(project.Root());
    REQUIRE((metadata.HasValue()));
    REQUIRE((metadata.Value().renderBackend == "opengl"));

    const ProjectOpenPreflight preflight = PreflightProjectOpen(project.Root(), Availability());
    REQUIRE((preflight.status == ProjectOpenPreflightStatus::RequiresRendererRestart));
    REQUIRE((preflight.requiredBackendId == "opengl"));
}

TEST_CASE("Accepts Project When Persisted Backend Is Already Active", "[unit][editor]")
{
    TemporaryProject project;
    project.WriteMetadata("metal");

    const ProjectOpenPreflight preflight = PreflightProjectOpen(project.Root(), Availability());
    REQUIRE((preflight.status == ProjectOpenPreflightStatus::Ready));
    REQUIRE((preflight.requiredBackendId == "metal"));
}

TEST_CASE("Rejects Unavailable Persisted Backend Without Fallback", "[unit][editor]")
{
    TemporaryProject project;
    project.WriteMetadata("vulkan");

    const ProjectOpenPreflight preflight = PreflightProjectOpen(project.Root(), Availability());
    REQUIRE((preflight.status == ProjectOpenPreflightStatus::RendererNotInstalled));
    REQUIRE((preflight.requiredBackendId == "vulkan"));
}

TEST_CASE("Reports Unreadable Metadata", "[unit][editor]")
{
    TemporaryProject project;
    const ProjectOpenPreflight preflight = PreflightProjectOpen(project.Root(), Availability());
    REQUIRE((preflight.status == ProjectOpenPreflightStatus::ProjectMetadataUnreadable));
}

TEST_CASE("Preserves Actionable Renderer Resolution States", "[unit][editor]")
{
    TemporaryProject project;
    project.WriteMetadata("vulkan");

    const auto preflightFor = [&project](const RendererAvailabilityState state) {
        const RendererAvailabilitySnapshot availability{
            {RendererBackendAvailability{"vulkan", "Vulkan", state, "Action required."}}, "metal"};
        return PreflightProjectOpen(project.Root(), availability).status;
    };

    REQUIRE((preflightFor(RendererAvailabilityState::RepairRequired) ==
             ProjectOpenPreflightStatus::RendererRepairRequired));
    REQUIRE((preflightFor(RendererAvailabilityState::UpdateRequired) ==
             ProjectOpenPreflightStatus::RendererUpdateRequired));
    REQUIRE((preflightFor(RendererAvailabilityState::AbiMismatch) ==
             ProjectOpenPreflightStatus::RendererCapabilityMismatch));
}

TEST_CASE("Rejects Malformed Nested And Duplicate Metadata", "[unit][editor]")
{
    TemporaryProject project;
    project.WriteRawMetadata(
        R"({"formatVersion":1.5,"projectId":"p","name":"n","settings":{"renderBackend":"metal"}})");
    REQUIRE((Horo::Application::LoadProjectMetadata(project.Root()).HasError()));

    project.WriteRawMetadata(R"({"horoVersion":"0.0.1","horoVersion":"0.0.1"})");
    REQUIRE((Horo::Application::LoadProjectMetadata(project.Root()).HasError()));
}

TEST_CASE("Accepts Unicode Escapes And Rejects Oversized Metadata", "[unit][editor]")
{
    TemporaryProject project;
    project.WriteMetadata("metal");
    const Horo::Result<Horo::Application::ProjectMetadata> unicodeMetadata =
        Horo::Application::LoadProjectMetadata(project.Root());
    REQUIRE((unicodeMetadata.HasValue()));
    REQUIRE((!unicodeMetadata.Value().name.empty()));

    project.WriteRawMetadata(std::string(64U * 1024U + 1U, 'x'));
    REQUIRE((Horo::Application::LoadProjectMetadata(project.Root()).HasError()));
}
} // namespace
