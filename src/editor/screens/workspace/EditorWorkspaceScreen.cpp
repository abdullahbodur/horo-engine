#include "Horo/Editor/DefaultScreenFactories.h"
#include "Horo/Editor/EditorGuiContext.h"
#include "Horo/Editor/EditorServiceRegistry.h"
#include "Horo/Editor/GuiScreenHost.h"
#include "Horo/Editor/Localization/ILocalizationService.h"
#include "Horo/Editor/ScreenRegistry.h"
#include "Horo/Editor/WorkspacePanelRegistry.h"
#include "Horo/Foundation/Logging/Logger.h"

#include "EditorWorkspaceView.h"
#include "Horo/Editor/EditorWorkspaceController.h"

#include <memory>
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
    explicit EditorWorkspaceScreen(const EditorServiceRegistry &services)
        : host_(services.Get<GuiScreenHost>()), context_(services.GetConst<EditorGuiContext>()),
          registry_(services.Get<WorkspacePanelRegistry>()), statusItems_(services.Get<EditorStatusItemRegistry>()),
          view_(context_, registry_, services.Get<std::uintptr_t>())
    {
    }

    ScreenId Id() const override
    {
        return static_cast<ScreenId>(GuiRouteKind::EditorWorkspace);
    }

    Result<void> OnEnter(const GuiRoute &route) override
    {
        std::string projectRoot;
        if (std::holds_alternative<EditorWorkspaceRouteParameters>(route.parameters))
        {
            const auto &params = std::get<EditorWorkspaceRouteParameters>(route.parameters);
            projectRoot = params.projectRoot;
        }

        controller_ = std::make_unique<EditorWorkspaceController>(std::move(projectRoot));
        LOG_INFO("editor.workspace", "EditorWorkspaceScreen entered for '%s'",
                 controller_->ViewModel().projectRoot.c_str());

        PanelContext panelContext{controller_->DataBus()};
        registry_.AttachAll(panelContext);
        UpdateStatusItems();
        return Result<void>::Success();
    }

    void OnUpdate(float /*dt*/) override
    {
        if (controller_)
        {
            controller_->UpdateFps(ImGui::GetIO().Framerate);
        }
    }

    void Draw(const GuiContentRegion &contentRegion) override
    {
        if (!controller_)
        {
            return;
        }

        EditorWorkspaceViewCommandData command;
        view_.Draw(controller_->ViewModel(), command, contentRegion);

        if (command.menuAction != EditorMenuAction::None)
        {
            host_.DispatchMenuAction(command.menuAction);
        }

        if (command.command != EditorWorkspaceViewCommand::None)
        {
            controller_->ProcessCommand(command);
            if (command.command == EditorWorkspaceViewCommand::ReturnToWelcome)
            {
                static_cast<void>(host_.Navigate(GuiRoute{GuiRouteKind::Welcome, WelcomeRouteParameters{}}));
            }
        }

        UpdateStatusItems();
    }

    void CollectActivePanelIds(std::vector<std::string_view> &output) const override
    {
        if (!controller_)
        {
            return;
        }
        const EditorWorkspaceViewModel &viewModel = controller_->ViewModel();
        const auto append = [&output](const std::string &panelId) {
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

    bool HandleMenuAction(const EditorMenuAction action) override
    {
        if (!controller_ || action != EditorMenuAction::SaveScene)
        {
            return false;
        }
        EditorWorkspaceViewCommandData command;
        command.command = EditorWorkspaceViewCommand::SaveScene;
        controller_->ProcessCommand(command);
        return true;
    }

    LeaveDecision CanLeave(const LeaveTarget &) const override
    {
        using enum LeaveAction;
        using enum LeaveDisposition;
        if (controller_ && controller_->ViewModel().isDirty)
        {
            LeaveRequirement requirement{.kind = LeaveRequirementKind::DirtyDocument,
                                         .subject = 1,
                                         .revision = 1,
                                         .allowedActions = {Save, Discard, Stay}};
            return LeaveDecision{.disposition = RequireResolution, .requirement = requirement};
        }
        return LeaveDecision{.disposition = Allow, .requirement = std::nullopt};
    }

    Result<LeaveDecision> ResolveLeave(const LeaveTarget &, const LeaveResolution &resolution) override
    {
        using enum LeaveAction;
        using enum LeaveDisposition;
        if (resolution.subject != 1 || resolution.revision != 1)
        {
            return Result<LeaveDecision>::Failure(Error{.code = ErrorCode{"navigation.stale_leave_subject"},
                                                        .domain = ErrorDomainId{"horo.editor.workspace"},
                                                        .severity = ErrorSeverity::Error,
                                                        .message = "Workspace leave requirement is stale."});
        }
        if (resolution.action == Save)
        {
            if (controller_)
            {
                EditorWorkspaceViewCommandData command;
                command.command = EditorWorkspaceViewCommand::SaveScene;
                controller_->ProcessCommand(command);
            }
            return Result<LeaveDecision>::Success(LeaveDecision{.disposition = Allow, .requirement = std::nullopt});
        }
        if (resolution.action == Discard)
        {
            return Result<LeaveDecision>::Success(LeaveDecision{.disposition = Allow, .requirement = std::nullopt});
        }
        if (resolution.action == Stay)
        {
            return Result<LeaveDecision>::Success(LeaveDecision{.disposition = Deny, .requirement = std::nullopt});
        }
        return Result<LeaveDecision>::Failure(Error{.code = ErrorCode{"navigation.leave_action_not_allowed"},
                                                    .domain = ErrorDomainId{"horo.editor.workspace"},
                                                    .severity = ErrorSeverity::Error,
                                                    .message = "Workspace leave action is not allowed."});
    }

    void OnLeave() override
    {
        LOG_INFO("editor.workspace", "EditorWorkspaceScreen leaving.");
        static_cast<void>(statusItems_.Update("horo.status.document", EditorStatusItemContent{.available = false}));
        static_cast<void>(statusItems_.Update("horo.status.selection", EditorStatusItemContent{.available = false}));
        registry_.DetachAll();
        controller_.reset();
    }

  private:
    void UpdateStatusItems()
    {
        if (!controller_)
        {
            return;
        }
        const EditorWorkspaceViewModel &viewModel = controller_->ViewModel();
        static_cast<void>(statusItems_.Update(
            "horo.status.document",
            EditorStatusItemContent{
                .iconResourceId = "horo.status.document",
                .label = context_.localization.Get("editor", viewModel.isDirty ? "status.document.unsaved"
                                                                               : "status.document.saved"),
                .tone = viewModel.isDirty ? EditorStatusItemTone::Warning : EditorStatusItemTone::Success,
                .available = true}));
        static_cast<void>(statusItems_.Update(
            "horo.status.selection",
            EditorStatusItemContent{.value = context_.localization.Get("editor", viewModel.selectedIndex >= 0
                                                                                     ? "status.selection.one"
                                                                                     : "status.selection.none"),
                                    .available = true}));
    }

    GuiScreenHost &host_;
    const EditorGuiContext &context_;
    WorkspacePanelRegistry &registry_;
    EditorStatusItemRegistry &statusItems_;
    EditorWorkspaceView view_;
    std::unique_ptr<EditorWorkspaceController> controller_;
};
} // namespace

void RegisterEditorWorkspaceScreen(ScreenRegistry &registry)
{
    registry.Register(GuiRouteKind::EditorWorkspace, [](const EditorServiceRegistry &services, const GuiRoute &) {
        return std::make_unique<EditorWorkspaceScreen>(services);
    });
}
} // namespace Horo::Editor
