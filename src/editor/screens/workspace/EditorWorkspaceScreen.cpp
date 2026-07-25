#include "Horo/Editor/DefaultScreenFactories.h"
#include "Horo/Editor/EditorGuiContext.h"
#include "Horo/Editor/EditorServiceRegistry.h"
#include "editor/document/EditorViewportSceneExtractor.h"
#include "editor/renderer/EditorViewportRenderer.h"
#include "Horo/Editor/GuiScreenHost.h"
#include "Horo/Editor/Localization/ILocalizationService.h"
#include "Horo/Editor/EditorSettingsStore.h"
#include "Horo/Editor/ProjectOpenService.h"
#include "Horo/Editor/ScreenRegistry.h"
#include "Horo/Editor/WorkspacePanelRegistry.h"
#include "Horo/Foundation/Logging/Logger.h"

#include "editor/screens/NavigationErrors.h"
#include "EditorWorkspaceView.h"
#include "editor/screens/workspace/EditorWorkspaceController.h"
#include "editor/input/EditorInputActions.h"
#include "Horo/Application/ProjectCompatibility.h"

#include <memory>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace Horo::Editor
{
    namespace
    {
        class EditorWorkspaceScreen final : public GuiScreen
        {
        public:
            explicit EditorWorkspaceScreen(const EditorServiceRegistry& services)
                : host_(services.Get<GuiScreenHost>()), context_(services.GetConst<EditorGuiContext>()),
                  registry_(services.Get<WorkspacePanelRegistry>()),
                  statusItems_(services.Get<EditorStatusItemRegistry>()),
                  inputRouter_(services.Get<Input::InputRouter>()),
                  workspaceInputContext_(inputRouter_.PushContext(Input::InputContextId{"editor.workspace"},
                                                                  Input::InputContextKind::EditorWorkspace)),
                  view_(context_, registry_, services.Get<std::uintptr_t>(), inputRouter_, workspaceInputContext_),
                  viewportRenderer_(services.TryGet<IEditorViewportRenderer>()),
                  viewportSceneState_(services.Get<EditorViewportSceneState>()),
                  runtimeScene_(services.Get<Runtime::RuntimeSceneService>()),
                  projectOpenService_(services.Get<ProjectOpenService>())
            {
            }

            ScreenId Id() const override
            {
                return static_cast<ScreenId>(GuiRouteKind::EditorWorkspace);
            }

            Result<void> OnEnter(const GuiRoute& route) override
            {
                if (!std::holds_alternative<EditorWorkspaceRouteParameters>(route.parameters))
                    return Result<void>::Failure(MakeError(NavigationErrors::InvalidRouteParameters));
                const auto& params = std::get<EditorWorkspaceRouteParameters>(route.parameters);
                auto reserved = projectOpenService_.ReserveSession(params.session);
                if (reserved.HasError()) return Result<void>::Failure(reserved.ErrorValue());
                ProjectSessionActivationLease activation = std::move(reserved).Value();
                std::string projectRoot = activation.Candidate().projectRoot.string();

                controller_ = std::make_unique<EditorWorkspaceController>(std::move(projectRoot), runtimeScene_);
                host_.SetCurrentProjectRoot(controller_->ViewModel().projectRoot);
                LoadProjectInputProfile(controller_->ViewModel().projectRoot);
                viewportSceneState_.Replace(controller_->ViewportScene());
                publishedSceneRevision_ = controller_->ViewportScene().documentRevision;
                publishedSelectionRevision_ = controller_->CurrentSelectionRevision();
                publishedViewportRevision_ = controller_->CurrentViewportRevision();
                LOG_INFO("editor.workspace", "EditorWorkspaceScreen entered for '%s'",
                         controller_->ViewModel().projectRoot.c_str());

                PanelContext panelContext{
                    controller_->DataBus(), viewportRenderer_, &inputRouter_, &workspaceInputContext_
                };
                registry_.AttachAll(panelContext);
                UpdateStatusItems();
                if (auto committed = activation.Commit(); committed.HasError())
                {
                    registry_.DetachAll();
                    viewportSceneState_.Clear();
                    controller_.reset();
                    return committed;
                }
                return Result<void>::Success();
            }

            void OnUpdate(float /*dt*/) override
            {
                if (controller_)
                {
                    controller_->SynchronizeRuntimeScenePreview();
                    controller_->UpdateFps(ImGui::GetIO().Framerate);
                }
            }

            void Draw(const GuiContentRegion& contentRegion) override
            {
                if (!controller_)
                {
                    return;
                }

                EditorWorkspaceViewCommandData command;
                view_.Draw(controller_->ViewModel(), command, contentRegion);

                if (command.command == EditorWorkspaceViewCommand::None && !command.menuInvocation.has_value())
                {
                    RouteInputAction(command);
                }

                if (command.menuInvocation.has_value())
                {
                    host_.DispatchMenuInvocation(*command.menuInvocation);
                }

                if (command.command != EditorWorkspaceViewCommand::None)
                {
                    controller_->ProcessCommand(command);
                    if (command.command == EditorWorkspaceViewCommand::ReturnToWelcome)
                    {
                        static_cast<void>(host_.Navigate(GuiRoute{GuiRouteKind::Welcome, WelcomeRouteParameters{}}));
                    }
                }
                PublishViewportSceneIfChanged();

                UpdateStatusItems();
            }

            void CollectActivePanelIds(std::vector<std::string_view>& output) const override
            {
                if (!controller_)
                {
                    return;
                }
                const EditorWorkspaceViewModel& viewModel = controller_->ViewModel();
                const auto append = [&output](const std::string& panelId)
                {
                    if (!panelId.empty())
                    {
                        output.push_back(panelId);
                    }
                };
                append(viewModel.activeLeftPanelId);
                append(viewModel.activeRightPanelId);
                append(viewModel.activeLeftTopPanelId);
                append(viewModel.activeLeftBottomPanelId);
                append(viewModel.activeRightTopPanelId);
                append(viewModel.activeRightBottomPanelId);
                append(viewModel.activeBottomPanelId);
                append(viewModel.activeBottomLeftPanelId);
                append(viewModel.activeBottomRightPanelId);
                append(viewModel.activeDocumentPanelId);
            }

            bool HandleMenuInvocation(const EditorMenuInvocation& invocation) override
            {
                const EditorMenuAction action = invocation.action;
                if (controller_ && action == EditorMenuAction::CreatePrimitive && invocation.primitive.has_value())
                {
                    EditorWorkspaceViewCommandData command;
                    command.command = EditorWorkspaceViewCommand::CreatePrimitive;
                    command.primitivePayload = invocation.primitive;
                    command.objectPayload = controller_->ViewModel().primarySelection;
                    controller_->ProcessCommand(command);
                    PublishViewportSceneIfChanged();
                    return true;
                }
                if (!controller_ || (action != EditorMenuAction::SaveScene && action != EditorMenuAction::Undo &&
                    action != EditorMenuAction::Redo))
                {
                    return false;
                }
                EditorWorkspaceViewCommandData command;
                command.command = action == EditorMenuAction::SaveScene
                                      ? EditorWorkspaceViewCommand::SaveScene
                                      : (action == EditorMenuAction::Undo
                                             ? EditorWorkspaceViewCommand::UndoScene
                                             : EditorWorkspaceViewCommand::RedoScene);
                controller_->ProcessCommand(command);
                PublishViewportSceneIfChanged();
                return true;
            }

            LeaveDecision CanLeave(const LeaveTarget&) const override
            {
                using enum LeaveAction;
                using enum LeaveDisposition;
                if (controller_ && controller_->ViewModel().isDirty)
                {
                    LeaveRequirement requirement{
                        .kind = LeaveRequirementKind::DirtyDocument,
                        .subject = 1,
                        .revision = 1,
                        .allowedActions = {Save, Discard, Stay}
                    };
                    return LeaveDecision{.disposition = RequireResolution, .requirement = requirement};
                }
                return LeaveDecision{.disposition = Allow, .requirement = std::nullopt};
            }

            Result<LeaveDecision> ResolveLeave(const LeaveTarget&, const LeaveResolution& resolution) override
            {
                using enum LeaveAction;
                using enum LeaveDisposition;
                if (resolution.subject != 1 || resolution.revision != 1)
                {
                    return Result<LeaveDecision>::Failure(
                        MakeError(NavigationErrors::WorkspaceStaleLeaveSubject));
                }
                if (resolution.action == Save)
                {
                    if (controller_)
                    {
                        EditorWorkspaceViewCommandData command;
                        command.command = EditorWorkspaceViewCommand::SaveScene;
                        controller_->ProcessCommand(command);
                    }
                    return Result<LeaveDecision>::Success(LeaveDecision{
                        .disposition = Allow, .requirement = std::nullopt
                    });
                }
                if (resolution.action == Discard)
                {
                    return Result<LeaveDecision>::Success(LeaveDecision{
                        .disposition = Allow, .requirement = std::nullopt
                    });
                }
                if (resolution.action == Stay)
                {
                    return Result<LeaveDecision>::Success(LeaveDecision{
                        .disposition = Deny, .requirement = std::nullopt
                    });
                }
                return Result<LeaveDecision>::Failure(
                    MakeError(NavigationErrors::WorkspaceLeaveActionNotAllowed));
            }

            void OnLeave() override
            {
                LOG_INFO("editor.workspace", "EditorWorkspaceScreen leaving.");
                static_cast<void>(statusItems_.Update("horo.status.document",
                                                      EditorStatusItemContent{.available = false}));
                static_cast<void>(statusItems_.Update("horo.status.selection",
                                                      EditorStatusItemContent{.available = false}));
                registry_.DetachAll();
                if (previousInputProfile_.has_value())
                {
                    const Result<void> restored = inputRouter_.SetProfile(std::move(*previousInputProfile_));
                    if (restored.HasError())
                    LOG_ERROR("editor.input", "Unable to restore editor input profile: %s",
                              restored.ErrorValue().message.c_str());
                    previousInputProfile_.reset();
                }
                workspaceInputContext_.Reset();
                viewportSceneState_.Clear();
                publishedSceneRevision_ = {};
                publishedSelectionRevision_ = {};
                publishedViewportRevision_ = {};
                controller_.reset();
            }

        private:
            void LoadProjectInputProfile(const std::filesystem::path& projectRoot)
            {
                previousInputProfile_ = inputRouter_.Profile();
                Input::InputBindingProfile merged{.profileId = "project-composed"};
                const auto mergeProfile = [&](const Input::InputBindingProfile& profile, const std::string& source)
                {
                    Result<Input::InputBindingProfile> layered = Input::MergeBindingProfiles(merged, profile);
                    if (layered.HasError())
                    {
                        LOG_ERROR("editor.input", "Unable to layer input profile '%s': %s", source.c_str(),
                                  layered.ErrorValue().message.c_str());
                        return false;
                    }
                    merged = std::move(layered).Value();
                    return true;
                };
                const auto mergeFile = [&](const std::filesystem::path& path)
                {
                    std::error_code error;
                    if (!std::filesystem::exists(path, error) || error)
                        return true;
                    const Result<Input::InputBindingProfile> loaded = Input::LoadBindingProfile(path);
                    if (loaded.HasError())
                    {
                        LOG_ERROR("editor.input", "Keeping last valid input profile; '%s' is invalid: %s",
                                  path.string().c_str(),
                                  loaded.ErrorValue().message.c_str());
                        return false;
                    }
                    return mergeProfile(loaded.Value(), path.string());
                };

                // Defaults are resolved by the action descriptors. Profile layers then
                // apply from project defaults to user-wide and project-user overrides.
                if (!mergeFile(projectRoot / ".horo" / "input.json") ||
                    !mergeProfile(*previousInputProfile_, "editor-global"))
                    return;
                const Result<Application::ProjectMetadata> metadata =
                    Application::LoadProjectMetadata(projectRoot);
                if (metadata.HasValue() &&
                    !mergeFile(ResolveEditorSettingsHomeDirectory() / ".horo" / "input" / "projects" /
                        (metadata.Value().projectId + ".json")))
                    return;

                const Result<void> applied = inputRouter_.SetProfile(std::move(merged));
                if (applied.HasError())
                LOG_ERROR("editor.input", "Keeping last valid input profile for project '%s': %s",
                          projectRoot.string().c_str(), applied.ErrorValue().message.c_str());
            }

            void RouteInputAction(EditorWorkspaceViewCommandData& command)
            {
                const auto pressed = [this](const char* id)
                {
                    return inputRouter_.ReadAction(workspaceInputContext_, Input::ActionId{id}).pressed;
                };
                if (pressed(kActionRedo)) command.command = EditorWorkspaceViewCommand::RedoScene;
                else if (pressed(kActionUndo)) command.command = EditorWorkspaceViewCommand::UndoScene;
                else if (pressed(kActionSave)) command.command = EditorWorkspaceViewCommand::SaveScene;
                else if (pressed(kActionToolSelect))
                {
                    command.command = EditorWorkspaceViewCommand::ChangeTransformTool;
                    command.transformToolPayload = EditorTransformTool::Select;
                }
                else if (pressed(kActionToolMove))
                {
                    command.command = EditorWorkspaceViewCommand::ChangeTransformTool;
                    command.transformToolPayload = EditorTransformTool::Move;
                }
                else if (pressed(kActionToolRotate))
                {
                    command.command = EditorWorkspaceViewCommand::ChangeTransformTool;
                    command.transformToolPayload = EditorTransformTool::Rotate;
                }
                else if (pressed(kActionToolScale))
                {
                    command.command = EditorWorkspaceViewCommand::ChangeTransformTool;
                    command.transformToolPayload = EditorTransformTool::Scale;
                }
                else if (controller_->ViewModel().primarySelection.has_value() && pressed(kActionDuplicate))
                {
                    command.command = EditorWorkspaceViewCommand::DuplicateObject;
                    command.objectPayload = controller_->ViewModel().primarySelection;
                }
                else if (controller_->ViewModel().primarySelection.has_value() && pressed(kActionDelete))
                {
                    command.command = EditorWorkspaceViewCommand::DeleteObject;
                    command.objectPayload = controller_->ViewModel().primarySelection;
                }
            }

            /** @brief Publishes document, selection, or viewport changes to the composition-owned render state. */
            void PublishViewportSceneIfChanged()
            {
                if (!controller_)
                {
                    return;
                }
                const DocumentRevision documentRevision = controller_->ViewportScene().documentRevision;
                const SelectionRevision selectionRevision = controller_->CurrentSelectionRevision();
                const ViewportRevision viewportRevision = controller_->CurrentViewportRevision();
                if (documentRevision == publishedSceneRevision_ && selectionRevision == publishedSelectionRevision_ &&
                    viewportRevision == publishedViewportRevision_)
                {
                    return;
                }
                viewportSceneState_.Replace(controller_->ViewportScene());
                publishedSceneRevision_ = documentRevision;
                publishedSelectionRevision_ = selectionRevision;
                publishedViewportRevision_ = viewportRevision;
            }

            void UpdateStatusItems()
            {
                if (!controller_)
                {
                    return;
                }
                const EditorWorkspaceViewModel& viewModel = controller_->ViewModel();
                static_cast<void>(statusItems_.Update(
                    "horo.status.document",
                    EditorStatusItemContent{
                        .iconResourceId = "horo.status.document",
                        .label = context_.localization.Get("editor", viewModel.isDirty
                                                                         ? "status.document.unsaved"
                                                                         : "status.document.saved"),
                        .tone = viewModel.isDirty ? EditorStatusItemTone::Warning : EditorStatusItemTone::Success,
                        .available = true
                    }));
                static_cast<void>(statusItems_.Update(
                    "horo.status.selection",
                    EditorStatusItemContent{
                        .value = context_.localization.Get("editor", viewModel.primarySelection.has_value()
                                                                         ? "status.selection.one"
                                                                         : "status.selection.none"),
                        .available = true
                    }));
            }

            GuiScreenHost& host_;
            const EditorGuiContext& context_;
            WorkspacePanelRegistry& registry_;
            EditorStatusItemRegistry& statusItems_;
            Input::InputRouter& inputRouter_;
            Input::InputContextToken workspaceInputContext_;
            EditorWorkspaceView view_;
            IEditorViewportRenderer* viewportRenderer_{nullptr};
            EditorViewportSceneState& viewportSceneState_;
            Runtime::RuntimeSceneService& runtimeScene_;
            ProjectOpenService& projectOpenService_;
            DocumentRevision publishedSceneRevision_{};
            SelectionRevision publishedSelectionRevision_{};
            ViewportRevision publishedViewportRevision_{};
            std::unique_ptr<EditorWorkspaceController> controller_;
            std::optional<Input::InputBindingProfile> previousInputProfile_;
        };
    } // namespace

    void RegisterEditorWorkspaceScreen(ScreenRegistry& registry)
    {
        registry.Register(GuiRouteKind::EditorWorkspace, [](const EditorServiceRegistry& services, const GuiRoute&)
        {
            return std::make_unique<EditorWorkspaceScreen>(services);
        });
    }
} // namespace Horo::Editor
