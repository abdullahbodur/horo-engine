#include "Horo/Application/ProjectCompatibility.h"
#include "Horo/Editor/GuiScreenHost.h"
#include "editor/screens/project_loading/ProjectLoadingView.h"
#include "editor/screens/welcome/WelcomeView.h"
#include "helpers/editor_ui/HeadlessEditorGuiFixture.h"

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <string>

namespace {
    Horo::Editor::RecentProjectEntry MakeRecentProject(const Horo::Application::ProjectCompatibilityStatus status,
                                                       const Horo::Editor::RecentProjectInspectionState inspectionState) {
        using namespace Horo::Editor;
        return RecentProjectEntry{
            .name = "Example Project",
            .rootPath = "/projects/example",
            .lastOpenedLabel = "Yesterday",
            .thumbnailKey = "example",
            .compatibility =
                RecentProjectCompatibilityProjection{
                    .status = status,
                    .inspectionState = inspectionState,
                },
        };
    }
}  // namespace

TEST_CASE("Welcome presentation renders recent-project compatibility states and news", "[unit][editor][gui][welcome]") {
    using namespace Horo;
    using namespace Horo::Application;
    using namespace Horo::Editor;

    Tests::EditorGuiContextFixture fixture;
    static constexpr std::array statuses{
        ProjectCompatibilityStatus::Current,
        ProjectCompatibilityStatus::CompatibleReleaseLine,
        ProjectCompatibilityStatus::AutomaticMigrationRequired,
        ProjectCompatibilityStatus::RecoveryRequired,
        ProjectCompatibilityStatus::FutureVersion,
        ProjectCompatibilityStatus::MigrationPathMissing,
        ProjectCompatibilityStatus::RequiredProviderUnavailable,
        ProjectCompatibilityStatus::Corrupt,
        ProjectCompatibilityStatus::Inaccessible,
    };

    WelcomeViewModel model{
        .productName = "Horo",
        .statusLabel = "Ready",
        .whatsNew = {WhatsNewEntry{"Release", "Editor workflows", "Improved editor workflows."},
                     WhatsNewEntry{"Docs", "Architecture", "Updated architecture guidance."}},
    };
    for (std::size_t index = 0; index < statuses.size(); ++index) {
        model.recentProjects.push_back(MakeRecentProject(statuses[index], index == 0 ? RecentProjectInspectionState::Refreshing
                                                                                     : RecentProjectInspectionState::Fresh));
    }
    model.recentProjects.push_back(RecentProjectEntry{"Legacy Project", "/projects/legacy", "Last week", "legacy", std::nullopt});

    fixture.imgui.BeginFrame();
    const WelcomeViewResult result =
        DrawWelcomeView(model, fixture.context, WelcomeViewAssets{}, GuiContentRegion{0.0F, 0.0F, 1280.0F, 800.0F});
    fixture.imgui.EndFrame();

    REQUIRE(result.command == WelcomeViewCommand::None);
    REQUIRE(result.openRecentIndex == -1);
    REQUIRE(model.recentProjects.size() == statuses.size() + 1);
}

TEST_CASE("Project loading presentation renders active cancelled and failure states", "[unit][editor][gui][project-loading]") {
    using namespace Horo::Editor;

    Tests::EditorGuiContextFixture fixture;
    const GuiContentRegion content{0.0F, 0.0F, 1280.0F, 800.0F};
    std::array states{
        ProjectLoadingViewState{.projectName = "Active", .projectRoot = "/projects/active", .progress = 42.0F},
        ProjectLoadingViewState{.projectName = "Cancelled", .projectRoot = "/projects/cancelled", .progress = 60.0F, .isCancelled = true},
        ProjectLoadingViewState{.projectName = "Retryable",
                                .projectRoot = "/projects/retryable",
                                .progress = 75.0F,
                                .statusText = "Import failed",
                                .hasFailed = true,
                                .canRetry = true},
        ProjectLoadingViewState{.projectName = "Failed",
                                .projectRoot = "/projects/failed",
                                .progress = 100.0F,
                                .statusText = "Project cannot be opened",
                                .hasFailed = true},
    };

    for (auto &state : states) {
        fixture.imgui.BeginFrame();
        const ProjectLoadingViewCommand command = DrawProjectLoadingView(state, fixture.context, content);
        fixture.imgui.EndFrame();
        REQUIRE(command == ProjectLoadingViewCommand::None);
    }
}
