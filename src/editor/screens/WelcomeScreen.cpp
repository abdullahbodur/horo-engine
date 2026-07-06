#include "Horo/Editor/WelcomeScreen.h"

#include <sstream>
#include <utility>

namespace Horo::Editor {

/** @copydoc IsRoutePayloadValid */
bool IsRoutePayloadValid(const GuiRoute& route) noexcept {
    switch (route.kind) {
    case GuiRouteKind::Welcome:
        return std::holds_alternative<WelcomeRouteParameters>(route.parameters);
    case GuiRouteKind::ProjectBrowser:
        return std::holds_alternative<ProjectBrowserRouteParameters>(route.parameters);
    case GuiRouteKind::ProjectCreation:
        return std::holds_alternative<ProjectCreationRouteParameters>(route.parameters);
    case GuiRouteKind::EditorWorkspace:
        return std::holds_alternative<EditorWorkspaceRouteParameters>(route.parameters);
    }

    return false;
}

/** @copydoc IsDisplayableRecentProject */
bool IsDisplayableRecentProject(const RecentProjectEntry& entry) noexcept {
    return !entry.name.empty() && !entry.rootPath.empty();
}

/** @copydoc WelcomeScreenController::WelcomeScreenController */
WelcomeScreenController::WelcomeScreenController(std::vector<RecentProjectEntry> recentProjects)
    : recentProjects_(std::move(recentProjects)) {}

/** @copydoc WelcomeScreenController::BuildViewModel */
WelcomeViewModel WelcomeScreenController::BuildViewModel() const {
    WelcomeViewModel model;
    model.productName = "Horo Editor";
    model.statusLabel = "Game Engine";

    model.recentProjects.reserve(recentProjects_.size());
    for (const RecentProjectEntry& entry : recentProjects_) {
        if (IsDisplayableRecentProject(entry)) {
            model.recentProjects.push_back(entry);
        }
    }

    return model;
}

/** @copydoc WelcomeScreenController::RequestCreateProject */
WelcomeAction WelcomeScreenController::RequestCreateProject() const {
    return WelcomeAction{
        WelcomeActionKind::CreateProject,
        GuiRoute{GuiRouteKind::ProjectCreation, ProjectCreationRouteParameters{}},
    };
}

/** @copydoc WelcomeScreenController::RequestOpenProject */
WelcomeAction WelcomeScreenController::RequestOpenProject() const {
    return WelcomeAction{
        WelcomeActionKind::OpenProject,
        GuiRoute{GuiRouteKind::ProjectBrowser, ProjectBrowserRouteParameters{}},
    };
}

/** @copydoc WelcomeScreenController::RequestOpenRecentProject */
std::optional<WelcomeAction> WelcomeScreenController::RequestOpenRecentProject(const std::size_t index) const {
    const WelcomeViewModel model = BuildViewModel();
    if (index >= model.recentProjects.size()) {
        return std::nullopt;
    }

    const RecentProjectEntry& project = model.recentProjects[index];
    return WelcomeAction{
        WelcomeActionKind::OpenRecentProject,
        GuiRoute{GuiRouteKind::EditorWorkspace, EditorWorkspaceRouteParameters{project.rootPath, std::nullopt}},
    };
}

/** @copydoc RenderWelcomeScreenText */
std::string RenderWelcomeScreenText(const WelcomeViewModel& viewModel) {
    std::ostringstream out;
    out << viewModel.productName << '\n';
    out << viewModel.statusLabel << '\n';
    out << "Actions: New Project | Open Project | Open Recent | Open Settings" << '\n';

    if (viewModel.recentProjects.empty()) {
        out << "Recent Projects: none" << '\n';
        return out.str();
    }

    out << "Recent Projects:" << '\n';
    for (std::size_t i = 0; i < viewModel.recentProjects.size(); ++i) {
        const RecentProjectEntry& project = viewModel.recentProjects[i];
        out << "  [" << i << "] " << project.name << " — " << project.rootPath;
        if (!project.lastOpenedLabel.empty()) {
            out << " (" << project.lastOpenedLabel << ')';
        }
        out << '\n';
    }

    return out.str();
}

} // namespace Horo::Editor
