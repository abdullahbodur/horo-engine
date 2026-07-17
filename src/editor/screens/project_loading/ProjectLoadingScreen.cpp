#include "Horo/Editor/DefaultScreenFactories.h"
#include "Horo/Editor/EditorGuiContext.h"
#include "Horo/Editor/EditorServiceRegistry.h"
#include "Horo/Editor/GuiScreenHost.h"
#include "Horo/Editor/ProjectCreationService.h"
#include "editor/project_model/ProjectMetadata.h"
#include "editor/project_model/RendererAvailability.h"
#include "Horo/Editor/ScreenRegistry.h"
#include "Horo/Foundation/Logging/LogContext.h"
#include "Horo/Foundation/Logging/Logger.h"
#include "ProjectLoadingView.h"

#include <memory>
#include <optional>
#include <string>

namespace Horo::Editor
{
    namespace
    {
        class ProjectLoadingScreen final : public GuiScreen
        {
        public:
            explicit ProjectLoadingScreen(const EditorServiceRegistry& services)
                : host_(services.Get<GuiScreenHost>()), context_(services.GetConst<EditorGuiContext>()),
                  creationService_(services.Get<ProjectCreationService>()),
                  rendererAvailability_(services.GetConst<RendererAvailabilitySnapshot>())
            {
            }

            ScreenId Id() const override
            {
                return static_cast<ScreenId>(GuiRouteKind::ProjectLoading);
            }

            Result<void> OnEnter(const GuiRoute& route) override
            {
                state_ = ProjectLoadingViewState{};
                if (std::holds_alternative<ProjectLoadingRouteParameters>(route.parameters))
                {
                    const auto& params = std::get<ProjectLoadingRouteParameters>(route.parameters);
                    state_.projectRoot = params.projectRoot;
                    state_.projectName = params.projectName;
                }
                operationId_ = host_.GetActiveCreationId();
                if (!operationId_.has_value() && !ResolveProjectRenderer())
                {
                    return Result<void>::Success();
                }
                logCtx_ = std::make_unique<Horo::Log::LogContext>("project", state_.projectName, "path",
                                                                  state_.projectRoot,
                                                                  "async", operationId_.has_value() ? "yes" : "no");
                LOG_INFO("editor.loading", "ProjectLoadingScreen entered: project='%s' path='%s' (async=%s).",
                         state_.projectName.c_str(), state_.projectRoot.c_str(),
                         operationId_.has_value() ? "yes" : "no");
                return Result<void>::Success();
            }

            void OnUpdate(float dt) override
            {
                if (state_.isCancelled)
                {
                    return;
                }
                if (operationId_.has_value())
                {
                    UpdateAsyncCreation();
                }
                else
                {
                    UpdateSimulatedLoading(dt);
                }
            }

            void Draw(const GuiContentRegion& contentRegion) override
            {
                const auto cmd = DrawProjectLoadingView(state_, context_, contentRegion);
                if (cmd == ProjectLoadingViewCommand::Cancel)
                {
                    LOG_INFO("editor.loading", "User cancelled project loading for '%s'.", state_.projectName.c_str());
                    state_.isCancelled = true;
                    if (operationId_.has_value())
                    {
                        static_cast<void>(creationService_.RequestCancel(*operationId_));
                        host_.SetActiveCreationId(std::nullopt);
                    }
                    static_cast<void>(host_.Navigate(GuiRoute{GuiRouteKind::Welcome, WelcomeRouteParameters{}}));
                }
            }

            LeaveDecision CanLeave(const LeaveTarget& target) const override
            {
                using enum LeaveDisposition;
                using enum LeaveAction;

                if (state_.progress >= 100.0F || state_.isCancelled || std::holds_alternative<GuiRoute>(target.value))
                {
                    return LeaveDecision{.disposition = Allow, .requirement = std::nullopt};
                }
                if (std::holds_alternative<ApplicationCloseTarget>(target.value))
                {
                    LeaveRequirement req{
                        .kind = LeaveRequirementKind::RunningOperation,
                        .subject = operationId_.value_or(2),
                        .revision = 1,
                        .allowedActions = {CancelOperation, Stay}
                    };
                    return LeaveDecision{.disposition = RequireResolution, .requirement = req};
                }
                return LeaveDecision{.disposition = Allow, .requirement = std::nullopt};
            }

            Result<LeaveDecision> ResolveLeave(const LeaveTarget&, const LeaveResolution& resolution) override
            {
                using enum LeaveDisposition;
                using enum LeaveAction;

                if (resolution.subject != operationId_.value_or(2) || resolution.revision != 1)
                {
                    return Result<LeaveDecision>::Failure(Error{
                        .code = ErrorCode{"navigation.stale_leave_subject"},
                        .domain = ErrorDomainId{"horo.editor.project_loading"},
                        .severity = ErrorSeverity::Error,
                        .message = "Project loading leave requirement is stale."
                    });
                }
                if (resolution.action == CancelOperation)
                {
                    state_.isCancelled = true;
                    if (operationId_.has_value())
                    {
                        static_cast<void>(creationService_.RequestCancel(*operationId_));
                        host_.SetActiveCreationId(std::nullopt);
                    }
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
                return Result<LeaveDecision>::Failure(Error{
                    .code = ErrorCode{"navigation.leave_action_not_allowed"},
                    .domain = ErrorDomainId{"horo.editor.project_loading"},
                    .severity = ErrorSeverity::Error,
                    .message = "Project loading leave action is not allowed."
                });
            }

            void OnLeave() override
            {
                LOG_DEBUG("editor.screens", "ProjectLoadingScreen leaving.");
                logCtx_.reset(); // pop project MDC frame
            }

        private:
            bool ResolveProjectRenderer()
            {
                const ProjectOpenPreflight preflight = PreflightProjectOpen(state_.projectRoot, rendererAvailability_);
                if (preflight.status == ProjectOpenPreflightStatus::Ready)
                {
                    return true;
                }
                if (preflight.status == ProjectOpenPreflightStatus::RequiresRendererRestart)
                {
                    static_cast<void>(host_.RequestRendererRestart(EditorRendererRestartRequest{
                        preflight.requiredBackendId, state_.projectRoot,
                        preflight.projectName.empty() ? state_.projectName : preflight.projectName
                    }));
                    return false;
                }
                state_.statusText = "Error: " + preflight.diagnostic;
                state_.isCancelled = true;
                LOG_ERROR("editor.loading", "Project renderer preflight failed: %s", preflight.diagnostic.c_str());
                return false;
            }

            void UpdateAsyncCreation()
            {
                using enum ProjectCreationOperationPhase;

                creationService_.PumpMainThread();
                if (const auto snapshot = creationService_.Query(*operationId_))
                {
                    state_.progress = snapshot->progress * 100.0F;
                    const char* phaseStr = nullptr;
                    switch (snapshot->phase)
                    {
                    case Validating:
                        state_.statusText = "Validating project settings...";
                        phaseStr = "Validating";
                        break;
                    case Staging:
                        state_.statusText = "Preparing staging directories...";
                        phaseStr = "Staging";
                        break;
                    case WritingMetadata:
                        state_.statusText = "Writing project configuration...";
                        phaseStr = "WritingMetadata";
                        break;
                    case WritingScaffolding:
                        state_.statusText = "Scaffolding asset tree...";
                        phaseStr = "WritingScaffolding";
                        break;
                    case Promoting:
                        state_.statusText = "Promoting workspace...";
                        phaseStr = "Promoting";
                        break;
                    case Completed:
                        state_.statusText = "Project ready!";
                        state_.progress = 100.0F;
                        phaseStr = "Completed";
                        break;
                    default:
                        state_.statusText = "Processing...";
                        phaseStr = "Unknown";
                        break;
                    }
                    if (phaseStr && phaseStr != lastLoggedPhase_)
                    {
                        LOG_INFO("editor.loading", "[%s] %.0f%% — %s", phaseStr, static_cast<double>(state_.progress),
                                 state_.statusText.c_str());
                        lastLoggedPhase_ = phaseStr;
                    }
                    if (snapshot->state == ProjectCreationOperationState::Succeeded)
                    {
                        LOG_INFO("editor.loading",
                                 "Async project creation succeeded. Transitioning to EditorWorkspace.");
                        host_.SetActiveCreationId(std::nullopt);
                        if (!ResolveProjectRenderer())
                        {
                            return;
                        }
                        static_cast<void>(host_.Navigate(GuiRoute{
                            GuiRouteKind::EditorWorkspace,
                            EditorWorkspaceRouteParameters{state_.projectRoot, std::nullopt}
                        }));
                    }
                    else if (snapshot->state == ProjectCreationOperationState::Failed)
                    {
                        if (snapshot->error)
                        {
                            LOG_ERROR("editor.project_creation", "Project creation failed: %s",
                                      snapshot->error->message.c_str());
                            state_.statusText = "Error: " + snapshot->error->message;
                        }
                        else
                        {
                            LOG_WARN("editor.loading", "Project creation cancelled or failed (no error detail).");
                            state_.statusText = "Project creation cancelled or failed.";
                        }
                        state_.isCancelled = true;
                    }
                }
            }

            void UpdateSimulatedLoading(float dt)
            {
                const float prevProgress = state_.progress;
                state_.progress += dt * 45.0F;

                const char* phaseStr = nullptr;
                if (state_.progress < 30.0F)
                {
                    state_.statusText = "Reading project metadata...";
                    phaseStr = "ReadingMetadata";
                }
                else if (state_.progress < 70.0F)
                {
                    state_.statusText = "Scanning asset index...";
                    phaseStr = "ScanningAssets";
                }
                else
                {
                    state_.statusText = "Initializing workspace session...";
                    phaseStr = "InitializingSession";
                }
                if (phaseStr && phaseStr != lastLoggedPhase_)
                {
                    LOG_INFO("editor.loading", "[Simulated] %s (%.0f%%).", phaseStr,
                             static_cast<double>(state_.progress));
                    lastLoggedPhase_ = phaseStr;
                }
                (void)prevProgress;

                if (state_.progress >= 100.0F)
                {
                    state_.progress = 100.0F;
                    LOG_INFO("editor.loading",
                             "[Simulated] Project loading complete. Transitioning to EditorWorkspace.");
                    static_cast<void>(host_.Navigate(GuiRoute{
                        GuiRouteKind::EditorWorkspace, EditorWorkspaceRouteParameters{state_.projectRoot, std::nullopt}
                    }));
                }
            }

            GuiScreenHost& host_;
            const EditorGuiContext& context_;
            ProjectCreationService& creationService_;
            const RendererAvailabilitySnapshot& rendererAvailability_;
            ProjectLoadingViewState state_;
            std::optional<ProjectCreationOperationId> operationId_;
            const char* lastLoggedPhase_ = nullptr;
            /// @brief RAII MDC frame scoped to the loading session.
            std::unique_ptr<Horo::Log::LogContext> logCtx_;
        };
    } // namespace

    void RegisterProjectLoadingScreen(ScreenRegistry& registry)
    {
        registry.Register(GuiRouteKind::ProjectLoading, [](const EditorServiceRegistry& services, const GuiRoute&)
        {
            return std::make_unique<ProjectLoadingScreen>(services);
        });
    }
} // namespace Horo::Editor
