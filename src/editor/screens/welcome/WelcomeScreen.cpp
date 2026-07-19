#include "Horo/Editor/DefaultScreenFactories.h"
#include "Horo/Editor/EditorGuiContext.h"
#include "Horo/Editor/EditorModalHost.h"
#include "Horo/Editor/EditorServiceRegistry.h"
#include "Horo/Editor/EditorSettingsService.h"
#include "Horo/Editor/GuiScreenHost.h"
#include "editor/project_model/ProjectMetadata.h"
#include "Horo/Editor/RecentProject.h"
#include "Horo/Editor/RecentProjectInspectionService.h"
#include "Horo/Editor/ScreenRegistry.h"
#include "Horo/Editor/SettingsModal.h"
#include "Horo/Editor/WelcomeController.h"
#include "Horo/Foundation/Logging/Logger.h"
#include "Horo/Runtime/Input.h"
#include "WelcomeView.h"

#include <imgui.h>
#include <portable-file-dialogs.h>

#include <filesystem>
#include <algorithm>
#include <memory>
#include <vector>

namespace Horo::Editor
{
    namespace
    {
        class WelcomeScreen final : public GuiScreen
        {
        public:
            explicit WelcomeScreen(const EditorServiceRegistry& services)
                : host_(services.Get<GuiScreenHost>()), context_(services.GetConst<EditorGuiContext>()),
                  modalHost_(services.Get<EditorModalHost>()), settings_(services.Get<EditorSettingsService>()),
                  inputRouter_(services.Get<Input::InputRouter>()),
                  recentInspection_(services.Get<RecentProjectInspectionService>()),
                  logoTexture_(services.TryGet<std::uintptr_t>() ? *services.TryGet<std::uintptr_t>() : 0)
            {
            }

            ScreenId Id() const override
            {
                return static_cast<ScreenId>(GuiRouteKind::Welcome);
            }

            Result<void> OnEnter(const GuiRoute&) override
            {
                recentProjects_ = LoadRecentProjectsFromDisk();
                for (RecentProjectEntry& project : recentProjects_)
                {
                    if (project.compatibility.has_value())
                        project.compatibility->inspectionState = RecentProjectInspectionState::Refreshing;
                }
                static_cast<void>(recentInspection_.Refresh(recentProjects_));
                controller_ = std::make_unique<WelcomeScreenController>(recentProjects_);
                viewModel_ = controller_->BuildViewModel();
                LOG_DEBUG("editor.screens", "WelcomeScreen entered with %zu recent projects.", recentProjects_.size());
                return Result<void>::Success();
            }

            void OnUpdate(float) override
            {
                bool changed = false;
                for (RecentProjectInspectionUpdate& update : recentInspection_.DrainUpdates())
                {
                    const auto project = std::ranges::find(recentProjects_, update.rootPath,
                                                           &RecentProjectEntry::rootPath);
                    if (project == recentProjects_.end())
                        continue;
                    project->compatibility = std::move(update.projection);
                    changed = true;
                }
                if (changed)
                {
                    static_cast<void>(SaveRecentProjectsToDisk(recentProjects_));
                    controller_ = std::make_unique<WelcomeScreenController>(recentProjects_);
                    viewModel_ = controller_->BuildViewModel();
                }
            }

            void Draw(const GuiContentRegion& contentRegion) override
            {
                if (!controller_)
                {
                    return;
                }
                const WelcomeViewResult result =
                    DrawWelcomeView(viewModel_, context_, WelcomeViewAssets{(ImTextureID)logoTexture_}, contentRegion);
                if (modalHost_.HasOpenModal())
                {
                    // A modal owns interaction — ignore any commands the view emits this frame.
                    return;
                }
                switch (result.command)
                {
                case WelcomeViewCommand::NewProject:
                    {
                        static_cast<void>(
                            host_.Navigate(GuiRoute{GuiRouteKind::ProjectCreation, ProjectCreationRouteParameters{}}));
                        break;
                    }
                case WelcomeViewCommand::OpenSettings:
                    {
                        static_cast<void>(modalHost_.OpenRoot(
                            std::make_unique<SettingsModal>(context_, settings_, logoTexture_)));
                        break;
                    }
                case WelcomeViewCommand::OpenRecentProject:
                    {
                        if (result.openRecentIndex >= 0 &&
                            static_cast<std::size_t>(result.openRecentIndex) < recentProjects_.size())
                        {
                            const auto& entry = recentProjects_[static_cast<std::size_t>(result.openRecentIndex)];
                            OpenProject(entry.rootPath, entry.name);
                        }
                        break;
                    }
                case WelcomeViewCommand::OpenProject:
                    {
                        auto nativeDialogContext = inputRouter_.PushContext(
                            Input::InputContextId{"editor.native_dialog.open_project"},
                            Input::InputContextKind::NativeDialog);
                        pfd::select_folder dialog("Select Horo Engine Project", "");
                        const std::string folderPath = dialog.result();
                        if (folderPath.empty())
                        {
                            LOG_INFO("editor.welcome", "Open Project cancelled by user.");
                            break;
                        }
                        const std::filesystem::path path{folderPath};
                        std::string projectName = path.filename().string();
                        if (projectName.empty())
                        {
                            projectName = "Unknown Project";
                        }

                        std::erase_if(recentProjects_,
                                      [&folderPath](const RecentProjectEntry& entry)
                                      {
                                          return entry.rootPath == folderPath;
                                      });
                        recentProjects_.emplace(recentProjects_.begin(), projectName, folderPath, "Just now", "empty");
                        SaveRecentProjectsToDisk(recentProjects_);
                        controller_ = std::make_unique<WelcomeScreenController>(recentProjects_);
                        viewModel_ = controller_->BuildViewModel();

                        OpenProject(folderPath, projectName);
                        break;
                    }
                case WelcomeViewCommand::None:
                    break;
                }
            }

            LeaveDecision CanLeave(const LeaveTarget&) const override
            {
                return LeaveDecision{.disposition = LeaveDisposition::Allow, .requirement = std::nullopt};
            }

            Result<LeaveDecision> ResolveLeave(const LeaveTarget&, const LeaveResolution&) override
            {
                return Result<LeaveDecision>::Success(
                    LeaveDecision{.disposition = LeaveDisposition::Allow, .requirement = std::nullopt});
            }

            void OnLeave() override
            {
                LOG_DEBUG("editor.screens", "WelcomeScreen leaving.");
            }

        private:
            void OpenProject(const std::string& projectRoot, const std::string& fallbackName)
            {
                static_cast<void>(host_.Navigate(GuiRoute{
                    GuiRouteKind::ProjectLoading, ProjectLoadingRouteParameters{projectRoot, fallbackName}}));
            }

            GuiScreenHost& host_;
            const EditorGuiContext& context_;
            EditorModalHost& modalHost_;
            EditorSettingsService& settings_;
            Input::InputRouter& inputRouter_;
            RecentProjectInspectionService& recentInspection_;
            std::uintptr_t logoTexture_;
            std::vector<RecentProjectEntry> recentProjects_;
            std::unique_ptr<WelcomeScreenController> controller_;
            WelcomeViewModel viewModel_;
        };
    } // namespace

    void RegisterWelcomeScreen(ScreenRegistry& registry)
    {
        registry.Register(GuiRouteKind::Welcome, [](const EditorServiceRegistry& services, const GuiRoute&)
        {
            return std::make_unique<WelcomeScreen>(services);
        });
    }
} // namespace Horo::Editor
