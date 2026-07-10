#pragma once

#include "Horo/Editor/EditorModalHost.h"
#include "Horo/Editor/EditorSettingsService.h"
#include "Horo/Editor/SettingsModalDraft.h"

#include <cstdint>

namespace Horo::Editor::Theme
{
    struct Fonts;
}

namespace Horo::Editor
{
    /** @brief Host-owned editor settings workflow and its transient settings draft. */
    class SettingsModal final : public EditorModal
    {
    public:
        static constexpr std::uint64_t kModalId = 0x53455454494E4753ULL;

        /** @brief Constructs the settings workflow with only its settings authority and presentation assets. */
        SettingsModal(EditorSettingsService &settings, const Theme::Fonts &fonts, std::uintptr_t logo) noexcept;

        [[nodiscard]] ModalId Id() const override;
        [[nodiscard]] ModalPresentation Presentation() const override;
        [[nodiscard]] ModalClosePolicy ClosePolicy() const override;
        [[nodiscard]] Result<void> OnOpen(EditorModalContext &context) override;
        [[nodiscard]] ModalFrameResult Draw() override;
        [[nodiscard]] CloseDecision CanClose(ModalCloseReason reason) override;
        void OnClose(ModalCloseReason reason) override;

        /** @brief Returns the modal-owned draft used by the settings presentation. */
        [[nodiscard]] SettingsState &Draft() noexcept;
        /** @brief Applies the current draft through the authoritative settings service. */
        [[nodiscard]] bool ApplyDraft();

    private:
        EditorSettingsService &m_settings;
        const Theme::Fonts &m_fonts;
        std::uintptr_t m_logo = 0;
        EditorDataBus *m_events = nullptr;
        SettingsState m_draft;
        bool m_revertedPublished = false;
    };
} // namespace Horo::Editor
