#include "Horo/Editor/DefaultScreenFactories.h"
#include "Horo/Editor/EditorGuiContext.h"
#include "Horo/Editor/EditorModalHost.h"
#include "Horo/Editor/EditorServiceRegistry.h"
#include "Horo/Editor/EditorSettingsService.h"
#include "Horo/Editor/GuiScreenHost.h"
#include "Horo/Editor/RecentProject.h"
#include "Horo/Editor/ScreenRegistry.h"
#include "Horo/Editor/SettingsModal.h"
#include "Horo/Editor/WelcomeController.h"
#include "Horo/Foundation/Logging/Logger.h"
#include "WelcomeView.h"

#include <imgui.h>
#include <portable-file-dialogs.h>

#include <filesystem>
#include <memory>
#include <vector>

namespace Horo::Editor {
namespace {

class WelcomeScreen final : public GuiScreen {
public:
    explicit WelcomeScreen(const EditorServiceRegistry &services)
        : host_(services.Get<GuiScreenHost>()),
          context_(services.GetConst<EditorGuiContext>()),
          modalHost_(services.Get<EditorModalHost>()),
          settings_(services.Get<EditorSettingsService>()),
          logoTexture_(services.TryGet<std::uintptr_t>() ? *services.TryGet<std::uintptr_t>() : 0)
    {
    }

    ScreenId Id() const override
    {
        return static_cast<ScreenId>(GuiRouteKind::Welcome);
    }

    Result<void> OnEnter(const GuiRoute &) override
    {
        recentProjects_ = LoadRecentProjectsFromDisk();
        controller_ = std::make_unique<WelcomeScreenController>(recentProjects_);
        viewModel_ = controller_->BuildViewModel();
        LOG_DEBUG("editor.screens", "WelcomeScreen entered with %zu recent projects.", recentProjects_.size());
        return Result<void>::Success();
    }

    void OnUpdate(float) override
    {
        // No time-dependent simulation required for the welcome screen.
    }

    void Draw(const GuiContentRegion &contentRegion) override
    {
        if (!controller_) {
            return;
        }
        const WelcomeViewResult result =
            DrawWelcomeView(viewModel_, context_, WelcomeViewAssets{(ImTextureID)logoTexture_}, contentRegion);
        if (modalHost_.HasOpenModal()) {
            // A modal owns interaction — ignore any commands the view emits this frame.
            return;
        }
        switch (result.command) {
        case WelcomeViewCommand::NewProject: {
            static_cast<void>(
                host_.Navigate(GuiRoute{GuiRouteKind::ProjectCreation, ProjectCreationRouteParameters{}}));
            break;
        }
        case WelcomeViewCommand::OpenSettings: {
            static_cast<void>(modalHost_.OpenRoot(std::make_unique<SettingsModal>(context_, settings_, logoTexture_)));
            break;
        }
        case WelcomeViewCommand::OpenRecentProject: {
            if (result.openRecentIndex >= 0 &&
                static_cast<std::size_t>(result.openRecentIndex) < recentProjects_.size()) {
                const auto &entry = recentProjects_[static_cast<std::size_t>(result.openRecentIndex)];
                static_cast<void>(host_.Navigate(
                    GuiRoute{GuiRouteKind::ProjectLoading, ProjectLoadingRouteParameters{entry.rootPath, entry.name}}));
            }
            break;
        }
        case WelcomeViewCommand::OpenProject: {
            pfd::select_folder dialog("Select Horo Engine Project", "");
            const std::string folderPath = dialog.result();
            if (folderPath.empty()) {
                LOG_INFO("editor.welcome", "Open Project cancelled by user.");
                break;
            }
            const std::filesystem::path path{folderPath};
            std::string projectName = path.filename().string();
            if (projectName.empty()) {
                projectName = "Unknown Project";
            }

            std::erase_if(recentProjects_,
                          [&folderPath](const RecentProjectEntry &entry) { return entry.rootPath == folderPath; });
            recentProjects_.emplace(recentProjects_.begin(), projectName, folderPath, "Just now", "empty");
            SaveRecentProjectsToDisk(recentProjects_);
            controller_ = std::make_unique<WelcomeScreenController>(recentProjects_);
            viewModel_ = controller_->BuildViewModel();

            static_cast<void>(host_.Navigate(
                GuiRoute{GuiRouteKind::ProjectLoading, ProjectLoadingRouteParameters{folderPath, projectName}}));
            break;
        }
        case WelcomeViewCommand::None:
            break;
        }
    }

    LeaveDecision CanLeave(const LeaveTarget &) const override
    {
        return LeaveDecision{.disposition = LeaveDisposition::Allow, .requirement = std::nullopt};
    }

    Result<LeaveDecision> ResolveLeave(const LeaveTarget &, const LeaveResolution &) override
    {
        return Result<LeaveDecision>::Success(
            LeaveDecision{.disposition = LeaveDisposition::Allow, .requirement = std::nullopt});
    }

    void OnLeave() override
    {
        LOG_DEBUG("editor.screens", "WelcomeScreen leaving.");
    }

private:
    GuiScreenHost &host_;
    const EditorGuiContext &context_;
    EditorModalHost &modalHost_;
    EditorSettingsService &settings_;
    std::uintptr_t logoTexture_;
    std::vector<RecentProjectEntry> recentProjects_;
    std::unique_ptr<WelcomeScreenController> controller_;
    WelcomeViewModel viewModel_;
};

} // namespace

void RegisterWelcomeScreen(ScreenRegistry &registry)
{
    registry.Register(GuiRouteKind::Welcome, [](const EditorServiceRegistry &services, const GuiRoute &) {
        return std::make_unique<WelcomeScreen>(services);
    });
}

} // namespace Horo::Editor
