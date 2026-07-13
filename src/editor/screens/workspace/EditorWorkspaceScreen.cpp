#include "Horo/Editor/DefaultScreenFactories.h"
#include "Horo/Editor/EditorGuiContext.h"
#include "Horo/Editor/EditorServiceRegistry.h"
#include "Horo/Editor/GuiScreenHost.h"
#include "Horo/Editor/ScreenRegistry.h"
#include "Horo/Editor/WorkspacePanelRegistry.h"
#include "Horo/Foundation/Logging/Logger.h"

#include "EditorWorkspaceView.h"
#include "Horo/Editor/EditorWorkspaceController.h"

#include <memory>
#include <string>

namespace Horo::Editor {
    namespace {
        class EditorWorkspaceScreen final : public GuiScreen {
        public:
            explicit EditorWorkspaceScreen(const EditorServiceRegistry &services)
                : host_(services.Get<GuiScreenHost>()), context_(services.GetConst<EditorGuiContext>()),
                  registry_(services.Get<WorkspacePanelRegistry>()), view_(context_, registry_) {
            }

            ScreenId Id() const override {
                return static_cast<ScreenId>(GuiRouteKind::EditorWorkspace);
            }

            Result<void> OnEnter(const GuiRoute &route) override {
                std::string projectRoot;
                if (std::holds_alternative<EditorWorkspaceRouteParameters>(route.parameters)) {
                    const auto &params = std::get<EditorWorkspaceRouteParameters>(route.parameters);
                    projectRoot = params.projectRoot;
                }

                controller_ = std::make_unique<EditorWorkspaceController>(std::move(projectRoot));
                LOG_INFO("editor.workspace", "EditorWorkspaceScreen entered for '%s'",
                         controller_->ViewModel().projectRoot.c_str());

                // Attach panels to the workspace
                PanelContext pctx{controller_->DataBus()};
                registry_.AttachAll(pctx);

                return Result<void>::Success();
            }

            void OnUpdate(float /*dt*/) override {
                if (controller_) {
                    controller_->UpdateFps(ImGui::GetIO().Framerate);
                }
            }

            void Draw() override {
                if (!controller_)
                    return;

                EditorWorkspaceViewCommandData cmd;
                view_.Draw(controller_->ViewModel(), cmd);

                if (cmd.command != EditorWorkspaceViewCommand::None) {
                    controller_->ProcessCommand(cmd);

                    // Handle cross-screen navigation requests directly here (or in a coordinator)
                    if (cmd.command == EditorWorkspaceViewCommand::ReturnToWelcome) {
                        static_cast<void>(host_.Navigate(GuiRoute{GuiRouteKind::Welcome, WelcomeRouteParameters{}}));
                    }
                }
            }

            LeaveDecision CanLeave(const LeaveTarget &) const override {
                using enum LeaveDisposition;
                using enum LeaveAction;
                if (controller_ && controller_->ViewModel().isDirty) {
                    LeaveRequirement req{
                        .kind = LeaveRequirementKind::DirtyDocument,
                        .subject = 1,
                        .revision = 1,
                        .allowedActions = {Save, Discard, Stay}
                    };
                    return LeaveDecision{.disposition = RequireResolution, .requirement = req};
                }
                return LeaveDecision{.disposition = Allow, .requirement = std::nullopt};
            }

            Result<LeaveDecision> ResolveLeave(const LeaveTarget &, const LeaveResolution &resolution) override {
                using enum LeaveDisposition;
                using enum LeaveAction;
                if (resolution.action == Save || resolution.action == Discard) {
                    // Force reset dirty flag so we can leave
                    if (controller_) {
                        EditorWorkspaceViewCommandData cmd;
                        cmd.command = EditorWorkspaceViewCommand::SaveScene;
                        controller_->ProcessCommand(cmd);
                    }
                    return Result<LeaveDecision>::Success(LeaveDecision{
                        .disposition = Allow, .requirement = std::nullopt
                    });
                }
                return Result<LeaveDecision>::Success(LeaveDecision{.disposition = Deny, .requirement = std::nullopt});
            }

            void OnLeave() override {
                LOG_INFO("editor.workspace", "EditorWorkspaceScreen leaving.");
                registry_.DetachAll();
                controller_.reset();
            }

        private:
            GuiScreenHost &host_;
            const EditorGuiContext &context_;
            WorkspacePanelRegistry &registry_;
            EditorWorkspaceView view_;
            std::unique_ptr<EditorWorkspaceController> controller_;
        };
    } // namespace

    void RegisterEditorWorkspaceScreen(ScreenRegistry &registry) {
        registry.Register(GuiRouteKind::EditorWorkspace, [](const EditorServiceRegistry &services, const GuiRoute &) {
            return std::make_unique<EditorWorkspaceScreen>(services);
        });
    }
} // namespace Horo::Editor
