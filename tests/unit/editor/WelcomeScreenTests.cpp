#include "Horo/Editor/WelcomeScreen.h"

#include <cassert>
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

    WelcomeScreenController controller{{
        RecentProjectEntry{"Valid", "/tmp/valid", "today", "valid"},
        RecentProjectEntry{"Missing Path", "", "today", "missing"},
        RecentProjectEntry{"", "/tmp/missing-name", "today", "missing-name"},
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
    assert(parameters->projectRoot == "/tmp/valid");

    assert(!controller.RequestOpenRecentProject(1).has_value());
}

void RendersDeterministicPreviewText() {
    using namespace Horo::Editor;

    WelcomeScreenController controller{{RecentProjectEntry{"Project", "/tmp/project", "today", "project"}}};
    const std::string text = RenderWelcomeScreenText(controller.BuildViewModel());

    assert(text.find("Horo Editor") != std::string::npos);
    assert(text.find("Under active development") != std::string::npos);
    assert(text.find("Project") != std::string::npos);
    assert(text.find("/tmp/project") != std::string::npos);
}

} // namespace

int main() {
    ValidateRoutePayloads();
    FiltersInvalidRecentProjects();
    RendersDeterministicPreviewText();
    return 0;
}
