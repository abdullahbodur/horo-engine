#include "Horo/Editor/ProjectCreationController.h"
#include "editor/project_model/RendererAvailability.h"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

namespace
{
const Horo::Editor::RendererAvailabilitySnapshot &DefaultAvailability()
{
    using namespace Horo::Editor;
    static const RendererAvailabilitySnapshot availability{
        {RendererBackendAvailability{"opengl", "OpenGL", RendererAvailabilityState::Active, {}}}, "opengl"};
    return availability;
}

class TemporaryDirectory
{
  public:
    TemporaryDirectory()
    {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = std::filesystem::temp_directory_path() / ("horo-project-creation-" + std::to_string(stamp));
        std::filesystem::create_directories(path_);
    }

    ~TemporaryDirectory()
    {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }

    [[nodiscard]] const std::filesystem::path &Path() const
    {
        return path_;
    }

  private:
    std::filesystem::path path_;
};

void HasDiagnostic(const Horo::Editor::ProjectCreationValidation &validation,
                   const Horo::Editor::ProjectCreationDiagnosticCode code)
{
    for (const auto &diagnostic : validation.diagnostics)
    {
        if (diagnostic.code == code)
        {
            return;
        }
    }
    assert(false && "expected validation diagnostic");
}

void ProvidesDocumentDefaults()
{
    using namespace Horo::Editor;

    const ProjectCreationDraft draft = ProjectCreationController{DefaultAvailability()}.Draft();
    assert(draft.templateId == "3d-starter");
    assert(draft.projectVersion == "0.1.0");
    assert(draft.defaultScene == "assets/scenes/main.horo");
    assert(draft.renderBackend == "opengl");
    assert(draft.physicsEnabled);
    assert(draft.targetFrameRate == 60);
    assert(draft.buildProfile == "desktop-debug");
    assert(draft.assetCompression == "lz4");
    assert(draft.textureCompression == "bc7");
    assert(draft.targetPlatform == "host");
    assert(draft.compilerFamily == "default");
    assert(draft.minimumCxxStandard == 20);
    assert(!ProjectCreationController{DefaultAvailability()}.IsDirty());
}

void RejectsBlankAndPathLikeNames()
{
    using namespace Horo::Editor;

    TemporaryDirectory temporaryDirectory;
    ProjectCreationController controller{DefaultAvailability()};
    controller.SetProjectPath((temporaryDirectory.Path() / "new-project").string());

    controller.SetProjectName("   ");
    HasDiagnostic(controller.Validate(), ProjectCreationDiagnosticCode::ProjectNameRequired);

    controller.SetProjectName("bad/name");
    HasDiagnostic(controller.Validate(), ProjectCreationDiagnosticCode::ProjectNameContainsPathSeparator);
}

void DistinguishesOccupiedEmptyAndMissingDestinations()
{
    using namespace Horo::Editor;

    TemporaryDirectory temporaryDirectory;
    const auto occupied = temporaryDirectory.Path() / "occupied";
    const auto empty = temporaryDirectory.Path() / "empty";
    const auto missing = temporaryDirectory.Path() / "missing";
    std::filesystem::create_directories(occupied);
    std::filesystem::create_directories(empty);
    std::ofstream{occupied / "existing.txt"} << "occupied";

    assert(InspectProjectCreationLocation(occupied).kind == ProjectCreationLocationKind::OccupiedDirectory);
    assert(InspectProjectCreationLocation(empty).kind == ProjectCreationLocationKind::EmptyDirectory);
    assert(InspectProjectCreationLocation(missing).kind == ProjectCreationLocationKind::Missing);

    ProjectCreationController controller{DefaultAvailability()};
    controller.SetProjectName("NewProject");
    controller.SetProjectPath(occupied.string());
    HasDiagnostic(controller.Validate(), ProjectCreationDiagnosticCode::ProjectPathOccupied);

    controller.SetProjectPath(empty.string());
    assert(controller.Validate().IsValid());
    assert(controller.BuildCreationRequest().has_value());

    controller.SetProjectPath(missing.string());
    assert(controller.Validate().IsValid());
    assert(controller.BuildCreationRequest().has_value());
    assert(!std::filesystem::exists(missing));
}

void TracksDraftLeaveIntent()
{
    using namespace Horo::Editor;

    ProjectCreationController controller{DefaultAvailability()};
    assert(controller.LeaveIntent() == ProjectCreationLeaveIntent::Allow);

    controller.SetProjectName("Changed");
    assert(controller.IsDirty());
    assert(controller.LeaveIntent() == ProjectCreationLeaveIntent::RequireDiscardConfirmation);

    controller.DiscardDraft();
    assert(!controller.IsDirty());
    assert(controller.LeaveIntent() == ProjectCreationLeaveIntent::Allow);
}

void DefaultsToActiveBackendAndRejectsUnavailableSelection()
{
    using namespace Horo::Editor;
    const RendererAvailabilitySnapshot availability{
        {
            RendererBackendAvailability{"opengl", "OpenGL", RendererAvailabilityState::Available, {}},
            RendererBackendAvailability{"metal", "Metal", RendererAvailabilityState::Active, {}},
            RendererBackendAvailability{"vulkan", "Vulkan", RendererAvailabilityState::NotInstalled,
                                        "Renderer component is not installed."},
        },
        "metal"};
    ProjectCreationController controller{availability};
    assert(controller.Draft().renderBackend == "metal");

    TemporaryDirectory temporaryDirectory;
    controller.SetProjectName("BackendProject");
    controller.SetProjectPath((temporaryDirectory.Path() / "backend-project").string());
    controller.SetRenderBackend("vulkan");
    HasDiagnostic(controller.Validate(), ProjectCreationDiagnosticCode::RendererBackendUnavailable);
    assert(!controller.BuildCreationRequest().has_value());
}

} // namespace

int main()
{
    ProvidesDocumentDefaults();
    RejectsBlankAndPathLikeNames();
    DistinguishesOccupiedEmptyAndMissingDestinations();
    TracksDraftLeaveIntent();
    DefaultsToActiveBackendAndRejectsUnavailableSelection();
    return 0;
}
