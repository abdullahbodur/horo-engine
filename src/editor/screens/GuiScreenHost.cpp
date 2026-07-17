#include "Horo/Editor/GuiScreenHost.h"

#include <imgui.h>

#include "Horo/Editor/EditorGuiContext.h"
#include "Horo/Editor/EditorModalHost.h"
#include "Horo/Editor/EditorSettingsService.h"
#include "Horo/Editor/Localization/LocalizationService.h"
#include "Horo/Editor/ProjectCreationService.h"
#include "editor/project_model/RendererAvailability.h"
#include "Horo/Editor/SettingsModal.h"
#include "Horo/Foundation/DataBus.h"
#include "Horo/Foundation/Logging/Logger.h"
#include "Horo/Runtime/Input.h"
#include "editor/status_bar/EditorStatusBar.h"

#include <algorithm>
#include <cstdio>

namespace Horo::Editor
{
namespace
{

[[nodiscard]] Error MakeScreenError(const char *code, const char *message)
{
    return Error{.code = ErrorCode{code},
                 .domain = ErrorDomainId{"horo.editor.screens"},
                 .severity = ErrorSeverity::Error,
                 .message = message};
}

[[nodiscard]] const char *GetLeaveActionLabel(LeaveAction action) noexcept
{
    using enum LeaveAction;
    switch (action)
    {
    case Discard:
        return "Discard & Leave";
    case CancelOperation:
        return "Cancel Operation";
    case Save:
        return "Save & Leave";
    case Stay:
        return "Stay Here";
    default:
        return "Action";
    }
}

} // namespace

GuiScreenHost::GuiScreenHost(const EditorGuiContext &context, EditorModalHost &modalHost,
                             EditorSettingsService &settingsService, LocalizationService &localization,
                             EngineDataBus &engineEvents, ProjectCreationService &creationService,
                             Input::InputRouter &inputRouter,
                             const RendererAvailabilitySnapshot &rendererAvailability, ScreenRegistry screenRegistry,
                             WorkspacePanelRegistry workspacePanelRegistry, std::uintptr_t logoTexture)
    : context_(&context), modalHost_(&modalHost), settingsService_(&settingsService), localization_(&localization),
      engineEvents_(&engineEvents), creationService_(&creationService), logoTexture_(logoTexture),
      screenRegistry_(std::move(screenRegistry)), workspacePanelRegistry_(std::move(workspacePanelRegistry)),
      activeRoute_{GuiRouteKind::Welcome, WelcomeRouteParameters{}}
{
    services_.Register(*this);
    services_.RegisterConst(context);
    services_.Register(modalHost);
    services_.Register(settingsService);
    services_.Register(localization);
    services_.Register(engineEvents);
    services_.Register(creationService);
    services_.Register(inputRouter);
    services_.RegisterConst(rendererAvailability);
    services_.Register(logoTexture_);
    services_.Register<ScreenRegistry>(screenRegistry_);
    services_.Register<WorkspacePanelRegistry>(workspacePanelRegistry_);
    services_.Register<EditorStatusItemRegistry>(statusItemRegistry_);
    statusBar_ = std::make_unique<EditorStatusBar>(context, statusItemRegistry_);
    activeStatusPanelIds_.reserve(16);

    static_cast<void>(statusItemRegistry_.Register(
        EditorStatusItemDescriptor{.id = "horo.status.navigation",
                                   .labelKey = "status.navigation.label",
                                   .alignment = EditorStatusBarAlignment::Left,
                                   .priority = 70,
                                   .order = 30,
                                   .maxWidth = 96.0F},
        EditorStatusItemContent{.value = localization.Get("editor", "status.navigation.idle")}));
    static_cast<void>(statusItemRegistry_.Register(
        EditorStatusItemDescriptor{.id = "horo.status.backend",
                                   .alignment = EditorStatusBarAlignment::Right,
                                   .priority = 100,
                                   .order = 10,
                                   .maxWidth = 96.0F,
                                   .presentation = EditorStatusItemPresentation::Plain},
        EditorStatusItemContent{.label =
                                    rendererAvailability.Find(rendererAvailability.ActiveBackendId()) != nullptr
                                        ? rendererAvailability.Find(rendererAvailability.ActiveBackendId())->displayName
                                        : std::string{rendererAvailability.ActiveBackendId()}}));
    static_cast<void>(
        statusItemRegistry_.Register(EditorStatusItemDescriptor{.id = "horo.status.cpu",
                                                                .labelKey = "status.cpu.label",
                                                                .alignment = EditorStatusBarAlignment::Right,
                                                                .priority = 90,
                                                                .order = 20,
                                                                .maxWidth = 112.0F},
                                     EditorStatusItemContent{.value = "0.0 ms"}));
    static_cast<void>(statusItemRegistry_.Register(
        EditorStatusItemDescriptor{.id = "horo.status.document",
                                   .alignment = EditorStatusBarAlignment::Left,
                                   .priority = 100,
                                   .order = 0,
                                   .maxWidth = 140.0F,
                                   .presentation = EditorStatusItemPresentation::Pill},
        EditorStatusItemContent{.iconResourceId = "horo.status.document",
                                .label = localization.Get("editor", "status.document.saved"),
                                .tone = EditorStatusItemTone::Success,
                                .available = false}));
    static_cast<void>(statusItemRegistry_.Register(
        EditorStatusItemDescriptor{.id = "horo.status.selection",
                                   .labelKey = "status.selection.label",
                                   .alignment = EditorStatusBarAlignment::Left,
                                   .priority = 80,
                                   .order = 10,
                                   .maxWidth = 112.0F},
        EditorStatusItemContent{.value = localization.Get("editor", "status.selection.none"), .available = false}));

    activeScreen_ = CreateScreen(activeRoute_);
    if (activeScreen_)
    {
        static_cast<void>(activeScreen_->OnEnter(activeRoute_));
    }
}

GuiScreenHost::~GuiScreenHost()
{
    Shutdown();
}

/** @copydoc GuiScreenHost::Shutdown */
void GuiScreenHost::Shutdown() noexcept
{
    if (shutdown_)
    {
        return;
    }
    shutdown_ = true;
    if (activeScreen_)
    {
        activeScreen_->OnLeave();
        activeScreen_.reset();
    }
    services_.Clear();
}

bool GuiScreenHost::IsShutdown() const noexcept
{
    return shutdown_;
}

EditorServiceRegistry &GuiScreenHost::Services() noexcept
{
    return services_;
}

const EditorServiceRegistry &GuiScreenHost::Services() const noexcept
{
    return services_;
}

ScreenRegistry &GuiScreenHost::Screens() noexcept
{
    return screenRegistry_;
}

const ScreenRegistry &GuiScreenHost::Screens() const noexcept
{
    return screenRegistry_;
}

/** @copydoc GuiScreenHost::StatusItems */
EditorStatusItemRegistry &GuiScreenHost::StatusItems() noexcept
{
    return statusItemRegistry_;
}

/** @copydoc GuiScreenHost::StatusItems */
const EditorStatusItemRegistry &GuiScreenHost::StatusItems() const noexcept
{
    return statusItemRegistry_;
}

const GuiRoute &GuiScreenHost::ActiveRoute() const noexcept
{
    return activeRoute_;
}

bool GuiScreenHost::IsApplicationCloseRequested() const noexcept
{
    return closeRequested_;
}

/** @copydoc GuiScreenHost::RequestRendererRestart */
Result<void> GuiScreenHost::RequestRendererRestart(EditorRendererRestartRequest request)
{
    if (shutdown_)
        return Result<void>::Failure(MakeScreenError("navigation.host_shutdown", "Screen host is shut down."));
    rendererRestartRequest_ = std::move(request);
    const Result<void> close = RequestCloseApplication();
    if (close.HasError())
        rendererRestartRequest_.reset();
    return close;
}

void GuiScreenHost::RequestFatalShutdown() noexcept
{
    modalHost_->ForceDetachAllForShutdown();
    closeRequested_ = true;
}

/** @copydoc GuiScreenHost::RendererRestartRequest */
const std::optional<EditorRendererRestartRequest> &GuiScreenHost::RendererRestartRequest() const noexcept
{
    return rendererRestartRequest_;
}

void GuiScreenHost::SetActiveCreationId(std::optional<ProjectCreationOperationId> id) noexcept
{
    activeCreationId_ = id;
}

std::optional<ProjectCreationOperationId> GuiScreenHost::GetActiveCreationId() const noexcept
{
    return activeCreationId_;
}

Result<void> GuiScreenHost::Navigate(GuiRoute destination)
{
    if (shutdown_)
        return Result<void>::Failure(MakeScreenError("navigation.host_shutdown", "Screen host is shut down."));
    if (!IsRoutePayloadValid(destination))
    {
        return Result<void>::Failure(
            MakeScreenError("navigation.invalid_route_parameters", "Invalid route payload for requested route kind."));
    }
    if (navigationBusy_)
    {
        return Result<void>::Failure(MakeScreenError("navigation.busy", "Navigation already in progress."));
    }
    if (pendingRequirement_.has_value() || pendingTarget_.has_value())
    {
        return Result<void>::Failure(MakeScreenError("navigation.busy", "Leave resolution already pending."));
    }
    if (AreRoutesIdentical(activeRoute_, destination))
    {
        return Result<void>::Success();
    }
    if (isScreenCallbackActive_)
    {
        if (pendingNavigation_.has_value())
        {
            return Result<void>::Failure(MakeScreenError("navigation.busy", "Navigation already queued this frame."));
        }
        pendingNavigation_ = std::move(destination);
        return Result<void>::Success();
    }
    return ExecuteLeaveCheckAndCommit(LeaveTarget{destination});
}

Result<void> GuiScreenHost::RequestCloseApplication()
{
    if (shutdown_)
        return Result<void>::Failure(MakeScreenError("navigation.host_shutdown", "Screen host is shut down."));
    if (navigationBusy_ || pendingRequirement_.has_value() || pendingTarget_.has_value())
    {
        return Result<void>::Failure(MakeScreenError("navigation.busy", "Navigation already in progress."));
    }
    return ExecuteLeaveCheckAndCommit(LeaveTarget{ApplicationCloseTarget{}});
}

Result<void> GuiScreenHost::ExecuteLeaveCheckAndCommit(const LeaveTarget &target)
{
    if (!activeScreen_)
    {
        if (std::holds_alternative<GuiRoute>(target.value))
        {
            CommitRoute(std::get<GuiRoute>(target.value));
        }
        else
        {
            return CommitApplicationClose();
        }
        return Result<void>::Success();
    }

    const auto decision = activeScreen_->CanLeave(target);
    if (decision.disposition == LeaveDisposition::Deny)
    {
        return Result<void>::Failure(
            MakeScreenError("navigation.leave_denied", "Active screen denied leave transition."));
    }
    if (decision.disposition == LeaveDisposition::RequireResolution)
    {
        if (!decision.requirement.has_value())
        {
            return Result<void>::Failure(MakeScreenError("navigation.invalid_leave_requirement",
                                                         "Screen requested leave resolution without a requirement."));
        }
        if (resolutionAttemptCount_ >= 5)
        {
            return Result<void>::Failure(
                MakeScreenError("navigation.leave_resolution_limit_exceeded", "Exceeded resolution attempts."));
        }
        pendingRequirement_ = decision.requirement;
        pendingTarget_ = target;
        resolutionAttemptCount_++;
        return Result<void>::Success();
    }

    if (std::holds_alternative<GuiRoute>(target.value))
    {
        CommitRoute(std::get<GuiRoute>(target.value));
    }
    else
    {
        return CommitApplicationClose();
    }
    return Result<void>::Success();
}

void GuiScreenHost::CommitRoute(GuiRoute destination)
{
    navigationBusy_ = true;
    resolutionAttemptCount_ = 0;
    pendingRequirement_.reset();
    pendingTarget_.reset();

    std::unique_ptr<GuiScreen> candidate = CreateScreen(destination);
    if (!candidate)
    {
        navigationBusy_ = false;
        return;
    }

    if (activeScreen_)
    {
        activeScreen_->OnLeave();
    }

    if (const auto enterRes = candidate->OnEnter(destination); enterRes.HasError())
    {
        LOG_ERROR("editor.screens", "Destination OnEnter failed: %s", enterRes.ErrorValue().message.c_str());
        if (activeScreen_)
        {
            if (const auto rollback = activeScreen_->OnEnter(activeRoute_); rollback.HasError())
            {
                LOG_ERROR("editor.screens", "Active screen rollback OnEnter failed: %s",
                          rollback.ErrorValue().message.c_str());
                activeScreen_.reset();
                closeRequested_ = true;
            }
        }
        navigationBusy_ = false;
        return;
    }

    const GuiRouteKind prevKind = activeRoute_.kind;
    const GuiRouteRevision prevRev = activeRevision_;
    activeRoute_ = std::move(destination);
    activeRevision_++;
    activeScreen_ = std::move(candidate);
    navigationBusy_ = false;

    LOG_DEBUG("editor.routing", "Route committed: kind=%d revision=%llu", static_cast<int>(activeRoute_.kind),
              static_cast<unsigned long long>(activeRevision_));

    if (engineEvents_)
    {
        GuiRouteChangedEvent ev{prevKind, activeRoute_.kind, prevRev, activeRevision_};
        engineEvents_->Publish(ev);
    }
}

std::unique_ptr<GuiScreen> GuiScreenHost::CreateScreen(const GuiRoute &route)
{
    return screenRegistry_.CreateScreen(route, services_);
}

void GuiScreenHost::OnUpdate(float dt)
{
    if (localization_ != nullptr)
    {
        static_cast<void>(statusItemRegistry_.Update(
            "horo.status.navigation",
            EditorStatusItemContent{.value =
                                        localization_->Get("editor", navigationBusy_ ? "status.navigation.busy"
                                                                                     : "status.navigation.idle")}));
    }
    char cpuFrameTime[32]{};
    std::snprintf(cpuFrameTime, sizeof(cpuFrameTime), "%.1f ms", static_cast<double>(dt * 1000.0F));
    static_cast<void>(statusItemRegistry_.Update("horo.status.cpu", EditorStatusItemContent{.value = cpuFrameTime}));

    if (activeScreen_)
    {
        isScreenCallbackActive_ = true;
        activeScreen_->OnUpdate(dt);
        isScreenCallbackActive_ = false;
    }
    FlushPendingNavigation();
}

void GuiScreenHost::FlushPendingNavigation()
{
    if (!pendingNavigation_.has_value())
    {
        return;
    }
    GuiRoute destination = std::move(*pendingNavigation_);
    pendingNavigation_.reset();
    if (const Result<void> result = Navigate(std::move(destination)); result.HasError())
    {
        LOG_ERROR("editor.screens", "Deferred navigation failed: %s", result.ErrorValue().message.c_str());
    }
}

void GuiScreenHost::Draw()
{
    const ImGuiViewport *viewport = ImGui::GetMainViewport();
    const float contentHeight = std::max(0.0F, viewport->WorkSize.y - EditorStatusBar::Height);
    const GuiContentRegion contentRegion{viewport->WorkPos.x, viewport->WorkPos.y, viewport->WorkSize.x, contentHeight};

    if (activeScreen_)
    {
        isScreenCallbackActive_ = true;
        activeScreen_->Draw(contentRegion);
        isScreenCallbackActive_ = false;
    }

    FlushPendingNavigation();

    activeStatusPanelIds_.clear();
    if (activeScreen_)
    {
        activeScreen_->CollectActivePanelIds(activeStatusPanelIds_);
    }

    if (statusBar_)
    {
        const bool interactionEnabled =
            modalHost_ != nullptr && !modalHost_->HasOpenModal() && !pendingRequirement_.has_value();
        const auto invocation = statusBar_->Draw(ImVec2{contentRegion.x, contentRegion.y + contentRegion.height},
                                                 ImVec2{contentRegion.width, EditorStatusBar::Height},
                                                 EditorStatusBarContext{activeStatusPanelIds_}, interactionEnabled);
        if (invocation.has_value() && engineEvents_ != nullptr)
        {
            engineEvents_->Publish(*invocation);
        }
    }
    if (pendingRequirement_.has_value() && pendingTarget_.has_value())
    {
        PresentLeaveDialog(*pendingRequirement_, *pendingTarget_);
    }
}

/** @copydoc GuiScreenHost::DispatchMenuInvocation */
void GuiScreenHost::DispatchMenuInvocation(const EditorMenuInvocation &invocation)
{
    const EditorMenuAction action = invocation.action;
    switch (action)
    {
    case EditorMenuAction::NewProject:
        static_cast<void>(Navigate(GuiRoute{GuiRouteKind::ProjectCreation, ProjectCreationRouteParameters{}}));
        return;
    case EditorMenuAction::OpenProject:
        static_cast<void>(Navigate(GuiRoute{GuiRouteKind::Welcome, WelcomeRouteParameters{}}));
        return;
    case EditorMenuAction::OpenEditorSettings:
        if (context_ && settingsService_ && modalHost_)
        {
            auto modal = std::make_unique<SettingsModal>(*context_, *settingsService_, logoTexture_);
            static_cast<void>(modalHost_->OpenRoot(std::move(modal)));
        }
        return;
    case EditorMenuAction::ExitApplication:
        static_cast<void>(RequestCloseApplication());
        return;
    case EditorMenuAction::SaveScene:
    case EditorMenuAction::Undo:
    case EditorMenuAction::Redo:
    case EditorMenuAction::CreatePrimitive:
        if (activeScreen_)
        {
            static_cast<void>(activeScreen_->HandleMenuInvocation(invocation));
        }
        return;
    case EditorMenuAction::None:
        return;
    }
}

void GuiScreenHost::ExecuteLeaveResolution(LeaveAction action, LeaveRequirement requirement, LeaveTarget target)
{
    if (action == LeaveAction::Stay)
    {
        pendingRequirement_.reset();
        pendingTarget_.reset();
        resolutionAttemptCount_ = 0;
        return;
    }
    if (!activeScreen_ || !pendingRequirement_.has_value() || !pendingTarget_.has_value())
    {
        return;
    }

    const LeaveRequirement current = *pendingRequirement_;
    const bool staleRequirement = current.subject != requirement.subject || current.revision != requirement.revision;
    const bool actionAllowed = std::ranges::find(current.allowedActions, action) != current.allowedActions.end();
    if (staleRequirement || !actionAllowed)
    {
        LOG_ERROR("editor.screens", "Rejected stale or disallowed leave resolution.");
        return;
    }

    const auto result =
        activeScreen_->ResolveLeave(target, LeaveResolution{requirement.subject, requirement.revision, action});
    if (result.HasError())
    {
        LOG_ERROR("editor.screens", "Leave resolution failed: %s", result.ErrorValue().message.c_str());
        return;
    }

    const LeaveDecision &decision = result.Value();
    if (decision.disposition == LeaveDisposition::Deny)
    {
        pendingRequirement_.reset();
        pendingTarget_.reset();
        resolutionAttemptCount_ = 0;
        return;
    }
    if (decision.disposition == LeaveDisposition::RequireResolution)
    {
        if (!decision.requirement.has_value())
        {
            LOG_ERROR("editor.screens", "Leave resolution requested without a requirement.");
            return;
        }
        const LeaveRequirement &next = *decision.requirement;
        const bool progressed = next.subject != current.subject || next.revision > current.revision;
        if (!progressed || ++resolutionAttemptCount_ >= 5)
        {
            LOG_ERROR("editor.screens", "Leave resolution chain made no progress or exceeded its limit.");
            pendingRequirement_.reset();
            pendingTarget_.reset();
            resolutionAttemptCount_ = 0;
            return;
        }
        pendingRequirement_ = next;
        return;
    }

    pendingRequirement_.reset();
    pendingTarget_.reset();
    resolutionAttemptCount_ = 0;
    if (std::holds_alternative<GuiRoute>(target.value))
    {
        CommitRoute(std::get<GuiRoute>(target.value));
    }
    else
    {
        const Result<void> closed = CommitApplicationClose();
        if (closed.HasError())
            LOG_WARN("editor.screens", "Application close was rejected: %s", closed.ErrorValue().message.c_str());
    }
}

Result<void> GuiScreenHost::CommitApplicationClose()
{
    if (modalHost_->HasOpenModal())
    {
        const Result<void> modalClosed = modalHost_->RequestCloseAllForShutdown();
        if (modalClosed.HasError())
        {
            rendererRestartRequest_.reset();
            closeRequested_ = false;
            return modalClosed;
        }
    }
    closeRequested_ = true;
    return Result<void>::Success();
}

void GuiScreenHost::PresentLeaveDialog(const LeaveRequirement &requirement, const LeaveTarget &target)
{
    ImGui::OpenPopup("Unsaved Changes");
    const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5F, 0.5F));
    if (!ImGui::BeginPopupModal("Unsaved Changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        return;
    }

    if (requirement.kind == LeaveRequirementKind::UnsavedDraft)
    {
        ImGui::Text("You have unsaved project creation changes.\nDo you want to discard them and leave?");
    }
    else if (requirement.kind == LeaveRequirementKind::RunningOperation)
    {
        ImGui::Text("A background operation is running.\nDo you want to cancel the operation and leave?");
    }
    else
    {
        ImGui::Text("This screen requires confirmation before leaving.");
    }
    ImGui::Separator();

    std::optional<LeaveAction> selectedAction;
    for (const auto action : requirement.allowedActions)
    {
        if (ImGui::Button(GetLeaveActionLabel(action), ImVec2(140, 0)))
        {
            ImGui::CloseCurrentPopup();
            selectedAction = action;
        }
        ImGui::SameLine();
    }
    ImGui::NewLine();
    ImGui::EndPopup();
    if (selectedAction.has_value())
    {
        ExecuteLeaveResolution(*selectedAction, requirement, target);
    }
}

} // namespace Horo::Editor
