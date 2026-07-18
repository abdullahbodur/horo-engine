#include "Horo/Editor/WelcomeController.h"

#include <cassert>
#include <filesystem>
#include <string>
#include <variant>
#include <vector>

namespace {

void ValidateRoutePayloads() {
    using namespace Horo::Editor;

    assert(IsRoutePayloadValid(GuiRoute{GuiRouteKind::Welcome, WelcomeRouteParameters{}}));
    assert(IsRoutePayloadValid(GuiRoute{GuiRouteKind::ProjectBrowser, ProjectBrowserRouteParameters{}}));
    assert(IsRoutePayloadValid(GuiRoute{GuiRouteKind::ProjectCreation, ProjectCreationRouteParameters{}}));
    assert(IsRoutePayloadValid(GuiRoute{GuiRouteKind::EditorWorkspace,
                                        EditorWorkspaceRouteParameters{"/tmp/project", std::nullopt}}));

    assert(!IsRoutePayloadValid(GuiRoute{GuiRouteKind::Welcome, ProjectBrowserRouteParameters{}}));
}

void FiltersInvalidRecentProjects() {
    using namespace Horo::Editor;

    const std::string validRoot = (std::filesystem::temp_directory_path() / "horo-valid-project").string();

    WelcomeScreenController controller{{
        RecentProjectEntry{"Valid", validRoot, "today", "valid"},
        RecentProjectEntry{"Missing Path", "", "today", "missing"},
        RecentProjectEntry{"", "/tmp/missing-name", "today", "missing-name"},
        RecentProjectEntry{"Relative Path", "~/projects/example", "today", "relative"},
    }};

    const WelcomeViewModel model = controller.BuildViewModel();
    assert(model.productName == "Horo Editor");
    assert(model.recentProjects.size() == 1);
    assert(model.recentProjects[0].name == "Valid");

    const std::optional<WelcomeAction> openRecent = controller.RequestOpenRecentProject(0);
    assert(openRecent.has_value());
    assert(openRecent->kind == WelcomeActionKind::OpenRecentProject);
    assert(openRecent->route.kind == GuiRouteKind::EditorWorkspace);

    const auto* parameters = std::get_if<EditorWorkspaceRouteParameters>(&openRecent->route.parameters);
    assert(parameters != nullptr);
    assert(parameters->projectRoot == validRoot);

    assert(!controller.RequestOpenRecentProject(1).has_value());
}

void RendersDeterministicPreviewText() {
    using namespace Horo::Editor;

    const std::string projectRoot = (std::filesystem::temp_directory_path() / "horo-preview-project").string();
    WelcomeScreenController controller{{RecentProjectEntry{"Project", projectRoot, "today", "project"}}};
    const WelcomeViewModel viewModel = controller.BuildViewModel();
    const std::string text = RenderWelcomeScreenText(viewModel);

    assert(text.find("Horo Editor") != std::string::npos);
    assert(text.find(viewModel.statusLabel) != std::string::npos);
    assert(text.find("Project") != std::string::npos);
    assert(text.find(projectRoot) != std::string::npos);
}

} // namespace

int main() {
    ValidateRoutePayloads();
    FiltersInvalidRecentProjects();
    RendersDeterministicPreviewText();
    return 0;
}
