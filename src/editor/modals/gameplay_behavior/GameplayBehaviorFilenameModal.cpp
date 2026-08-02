#include "editor/modals/gameplay_behavior/GameplayBehaviorFilenameModal.h"

#include "Horo/Editor/EditorUiComponents.h"
#include "Horo/Editor/Localization/ILocalizationService.h"

#include <imgui.h>
#include <utility>

namespace Horo::Editor {
    namespace {
        const char *ExtensionFor(const GameplayBehaviorKind kind) {
            return kind == GameplayBehaviorKind::Lua ? ".horo_script" : ".cpp";
        }
    }

    GameplayBehaviorFilenameModal::GameplayBehaviorFilenameModal(const EditorGuiContext &context, const GameplayBehaviorKind kind,
                                                                   std::string destination, std::string baseName, CreateCallback onCreate)
        : context_(context), state_(kind, std::move(destination), std::move(baseName)), onCreate_(std::move(onCreate)) {}

    ModalId GameplayBehaviorFilenameModal::Id() const { return ModalId{kModalId}; }

    ModalPresentation GameplayBehaviorFilenameModal::Presentation() const {
        return {.size = ModalSizePolicy::Compact, .dimWorkspace = true};
    }

    ModalClosePolicy GameplayBehaviorFilenameModal::ClosePolicy() const {
        return {.allowCloseButton = true, .allowEscape = true, .allowOutsideClick = false, .allowApplicationShutdown = true};
    }

    Result<void> GameplayBehaviorFilenameModal::OnOpen(EditorModalContext &) { return Result<void>::Success(); }

    ModalFrameResult GameplayBehaviorFilenameModal::Draw() {
        const std::string title = context_.localization.Get("editor", "workspace.gameplay_behavior.create.title");
        Ui::ScopedModalShell modal({.id = "GameplayBehaviorFilename", .title = title.c_str(), .requestedSize = {520.0F, 320.0F},
                                    .viewportPadding = 48.0F, .minimumWidth = 420.0F, .minimumHeight = 260.0F,
                                    .footerHeight = Theme::Layout::FooterH, .showClose = true, .titleFontSize = 14.0F},
                                   context_.theme.fonts);

        bool confirm = false;
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{24.0F, 20.0F});
        ImGui::PushStyleColor(ImGuiCol_ChildBg, Theme::Bg1());
        ImGui::BeginChild("##GameplayBehaviorBody", {0.0F, modal.BodyHeight()}, false, ImGuiWindowFlags_AlwaysUseWindowPadding);

        const std::string typeLabel = context_.localization.Get("editor", "workspace.gameplay_behavior.create.type");
        const std::string destinationLabel = context_.localization.Get("editor", "workspace.gameplay_behavior.create.destination");
        const std::string filenameLabel = context_.localization.Get("editor", "workspace.gameplay_behavior.create.filename");
        const std::string kind = context_.localization.Get(
            "editor", state_.Kind() == GameplayBehaviorKind::Lua ? "workspace.gameplay_behavior.create.lua" : "workspace.gameplay_behavior.create.native");
        Ui::FieldLabel(typeLabel.c_str(), context_.theme.fonts);
        ImGui::TextUnformatted(kind.c_str());
        ImGui::Dummy({0.0F, 8.0F});
        Ui::FieldLabel(destinationLabel.c_str(), context_.theme.fonts);
        ImGui::PushStyleColor(ImGuiCol_Text, Theme::Muted());
        ImGui::TextWrapped("%s", state_.Destination().c_str());
        ImGui::PopStyleColor();
        ImGui::Dummy({0.0F, 8.0F});
        Ui::FieldLabel(filenameLabel.c_str(), context_.theme.fonts);
        if (Ui::InputTextControl("##GameplayBehaviorFilename", state_.MutableBaseName(), 256, context_.theme.fonts,
                                 state_.Validation().HasError())) {
            confirm = true;
        }
        state_.SetBaseName(state_.BaseName());
        ImGui::SameLine(0.0F, 0.0F);
        ImGui::TextUnformatted(ExtensionFor(state_.Kind()));
        if (state_.Validation().HasError()) {
            Ui::ErrorText(context_.localization.Get("editor", "workspace.gameplay_behavior.create.invalid_filename").c_str(),
                          context_.theme.fonts);
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();

        modal.BeginFooter({24.0F, 12.0F});
        const std::string cancel = context_.localization.Get("editor", "workspace.gameplay_behavior.create.cancel");
        const std::string create = context_.localization.Get("editor", "workspace.gameplay_behavior.create.confirm");
        if (Ui::Button({.label = cancel.c_str(), .variant = Ui::ButtonVariant::Secondary, .font = context_.theme.fonts.sans}))
            return ModalFrameResult::RequestClose(ModalCloseReason::Cancelled);
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 90.0F);
        if (Ui::Button({.label = create.c_str(), .enabled = !state_.Validation().HasError(), .font = context_.theme.fonts.sans}))
            confirm = true;
        modal.EndFooter();

        if (modal.CloseRequested())
            return ModalFrameResult::RequestClose(ModalCloseReason::Cancelled);
        if (confirm) {
            if (const auto request = state_.Confirm(); request.has_value()) {
                if (onCreate_)
                    onCreate_(*request);
                return ModalFrameResult::RequestClose(ModalCloseReason::Completed);
            }
        }
        return ModalFrameResult::None();
    }

    CloseDecision GameplayBehaviorFilenameModal::CanClose(ModalCloseReason) { return CloseDecision::Allow; }

    const GameplayBehaviorFilenameModalState &GameplayBehaviorFilenameModal::State() const noexcept { return state_; }
}  // namespace Horo::Editor
