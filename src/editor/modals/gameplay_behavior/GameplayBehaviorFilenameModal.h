#pragma once

#include "Horo/Editor/EditorGuiContext.h"
#include "Horo/Editor/EditorModalHost.h"
#include "Horo/Editor/EditorTheme.h"
#include "editor/modals/gameplay_behavior/GameplayBehaviorFilenameModalState.h"

#include <functional>
#include <string>

namespace Horo::Editor {
    /** @brief ImGui presentation for the pure gameplay-behavior filename workflow. */
    class GameplayBehaviorFilenameModal final : public EditorModal {
    public:
        static constexpr std::uint64_t kModalId = 0x47504C415942464EULL;
        using CreateCallback = std::function<void(CreateGameplayBehaviorRequest)>;

        GameplayBehaviorFilenameModal(const EditorGuiContext &context, GameplayBehaviorKind kind, std::string destination,
                                       std::string baseName, CreateCallback onCreate);

        [[nodiscard]] ModalId Id() const override;
        [[nodiscard]] ModalPresentation Presentation() const override;
        [[nodiscard]] ModalClosePolicy ClosePolicy() const override;
        [[nodiscard]] Result<void> OnOpen(EditorModalContext &) override;
        [[nodiscard]] ModalFrameResult Draw() override;
        [[nodiscard]] CloseDecision CanClose(ModalCloseReason) override;

        [[nodiscard]] const GameplayBehaviorFilenameModalState &State() const noexcept;

    private:
        const EditorGuiContext &context_;
        GameplayBehaviorFilenameModalState state_;
        CreateCallback onCreate_;
    };
}  // namespace Horo::Editor
