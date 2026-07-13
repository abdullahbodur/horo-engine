#include "Horo/Editor/GuiScreenHost.h"

#include <imgui.h>

#include "Horo/Editor/DefaultScreenFactories.h"
#include "Horo/Editor/DefaultWorkspacePanels.h"
#include "Horo/Editor/EditorGuiContext.h"
#include "Horo/Editor/EditorModalHost.h"
#include "Horo/Editor/EditorSettingsService.h"
#include "Horo/Editor/Localization/LocalizationService.h"
#include "Horo/Editor/ProjectCreationService.h"
#include "Horo/Foundation/DataBus.h"
#include "Horo/Foundation/Logging/Logger.h"

namespace Horo::Editor {
namespace {

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
    switch (action) {
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
                             std::uintptr_t logoTexture)
    : context_(&context), modalHost_(&modalHost), settingsService_(&settingsService), localization_(&localization),
      engineEvents_(&engineEvents), creationService_(&creationService), logoTexture_(logoTexture),
      activeRoute_{GuiRouteKind::Welcome, WelcomeRouteParameters{}}
{
    services_.Register(*this);
    services_.RegisterConst(context);
    services_.Register(modalHost);
    services_.Register(settingsService);
    services_.Register(localization);
    services_.Register(engineEvents);
    services_.Register(creationService);
    services_.Register(logoTexture_);
    services_.Register<ScreenRegistry>(screenRegistry_);
    services_.Register<WorkspacePanelRegistry>(workspacePanelRegistry_);

    RegisterWelcomeScreen(screenRegistry_);
    RegisterProjectCreationScreen(screenRegistry_);
    RegisterProjectLoadingScreen(screenRegistry_);
    RegisterEditorWorkspaceScreen(screenRegistry_);
    RegisterDefaultWorkspacePanels(workspacePanelRegistry_);

    activeScreen_ = CreateScreen(activeRoute_);
    if (activeScreen_) {
        static_cast<void>(activeScreen_->OnEnter(activeRoute_));
    }
}

GuiScreenHost::~GuiScreenHost()
{
    if (activeScreen_) {
        activeScreen_->OnLeave();
        activeScreen_.reset();
    }
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

const GuiRoute &GuiScreenHost::ActiveRoute() const noexcept
{
    return activeRoute_;
}

bool GuiScreenHost::IsApplicationCloseRequested() const noexcept
{
    return closeRequested_;
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
    if (!IsRoutePayloadValid(destination)) {
        return Result<void>::Failure(
            MakeScreenError("navigation.invalid_route_parameters", "Invalid route payload for requested route kind."));
    }
    if (navigationBusy_) {
        return Result<void>::Failure(MakeScreenError("navigation.busy", "Navigation already in progress."));
    }
    if (AreRoutesIdentical(activeRoute_, destination)) {
        return Result<void>::Success();
    }
    return ExecuteLeaveCheckAndCommit(LeaveTarget{destination});
}

Result<void> GuiScreenHost::RequestCloseApplication()
{
    if (navigationBusy_) {
        return Result<void>::Failure(MakeScreenError("navigation.busy", "Navigation already in progress."));
    }
    return ExecuteLeaveCheckAndCommit(LeaveTarget{ApplicationCloseTarget{}});
}

Result<void> GuiScreenHost::ExecuteLeaveCheckAndCommit(const LeaveTarget &target)
{
    if (!activeScreen_) {
        if (std::holds_alternative<GuiRoute>(target.value)) {
            CommitRoute(std::get<GuiRoute>(target.value));
        } else {
            closeRequested_ = true;
        }
        return Result<void>::Success();
    }

    const auto decision = activeScreen_->CanLeave(target);
    if (decision.disposition == LeaveDisposition::Deny) {
        return Result<void>::Failure(
            MakeScreenError("navigation.leave_denied", "Active screen denied leave transition."));
    }
    if (decision.disposition == LeaveDisposition::RequireResolution && decision.requirement.has_value()) {
        if (resolutionAttemptCount_ >= 5) {
            return Result<void>::Failure(
                MakeScreenError("navigation.leave_resolution_limit_exceeded", "Exceeded resolution attempts."));
        }
        pendingRequirement_ = decision.requirement;
        pendingTarget_ = target;
        resolutionAttemptCount_++;
        return Result<void>::Success();
    }

    if (std::holds_alternative<GuiRoute>(target.value)) {
        CommitRoute(std::get<GuiRoute>(target.value));
    } else {
        closeRequested_ = true;
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
    if (!candidate) {
        navigationBusy_ = false;
        return;
    }

    if (const auto enterRes = candidate->OnEnter(destination); enterRes.HasError()) {
        LOG_ERROR("editor.screens", "Destination OnEnter failed: %s", enterRes.ErrorValue().message.c_str());
        navigationBusy_ = false;
        return;
    }

    if (activeScreen_) {
        activeScreen_->OnLeave();
    }

    const GuiRouteKind prevKind = activeRoute_.kind;
    const GuiRouteRevision prevRev = activeRevision_;
    activeRoute_ = std::move(destination);
    activeRevision_++;
    activeScreen_ = std::move(candidate);
    navigationBusy_ = false;

    LOG_DEBUG("editor.routing", "Route committed: kind=%d revision=%llu", static_cast<int>(activeRoute_.kind),
              static_cast<unsigned long long>(activeRevision_));

    if (engineEvents_) {
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
    if (activeScreen_) {
        activeScreen_->OnUpdate(dt);
    }
}

void GuiScreenHost::Draw()
{
    if (activeScreen_) {
        activeScreen_->Draw();
    }
    if (pendingRequirement_.has_value() && pendingTarget_.has_value()) {
        PresentLeaveDialog(*pendingRequirement_, *pendingTarget_);
    }
}

void GuiScreenHost::ExecuteLeaveResolution(LeaveAction action, const LeaveRequirement &requirement,
                                           const LeaveTarget &target)
{
    if (action == LeaveAction::Stay) {
        pendingRequirement_.reset();
        pendingTarget_.reset();
        return;
    }
    if (!activeScreen_) {
        return;
    }
    const auto res =
        activeScreen_->ResolveLeave(target, LeaveResolution{requirement.subject, requirement.revision, action});
    if (res.HasValue() && res.Value().disposition == LeaveDisposition::Allow) {
        if (std::holds_alternative<GuiRoute>(target.value)) {
            CommitRoute(std::get<GuiRoute>(target.value));
        } else {
            closeRequested_ = true;
        }
    }
}

void GuiScreenHost::PresentLeaveDialog(const LeaveRequirement &requirement, const LeaveTarget &target)
{
    ImGui::OpenPopup("Unsaved Changes");
    const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5F, 0.5F));
    if (!ImGui::BeginPopupModal("Unsaved Changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    if (requirement.kind == LeaveRequirementKind::UnsavedDraft) {
        ImGui::Text("You have unsaved project creation changes.\nDo you want to discard them and leave?");
    } else if (requirement.kind == LeaveRequirementKind::RunningOperation) {
        ImGui::Text("A background operation is running.\nDo you want to cancel the operation and leave?");
    } else {
        ImGui::Text("This screen requires confirmation before leaving.");
    }
    ImGui::Separator();

    for (const auto action : requirement.allowedActions) {
        if (ImGui::Button(GetLeaveActionLabel(action), ImVec2(140, 0))) {
            ImGui::CloseCurrentPopup();
            ExecuteLeaveResolution(action, requirement, target);
        }
        ImGui::SameLine();
    }
    ImGui::NewLine();
    ImGui::EndPopup();
}

} // namespace Horo::Editor
