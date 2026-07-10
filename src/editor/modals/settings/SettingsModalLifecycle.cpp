#include "Horo/Editor/SettingsModal.h"
#include "Horo/Editor/EditorSettingsEvents.h"

namespace Horo::Editor
{
    SettingsModal::SettingsModal(EditorSettingsService &settings, const Theme::Fonts &fonts, const std::uintptr_t logo) noexcept
        : m_settings(settings), m_fonts(fonts), m_logo(logo)
    {
    }

    ModalId SettingsModal::Id() const { return ModalId{kModalId}; }
    ModalPresentation SettingsModal::Presentation() const { return {.size = ModalSizePolicy::Large, .dimWorkspace = true}; }
    ModalClosePolicy SettingsModal::ClosePolicy() const { return {.allowCloseButton = true, .allowEscape = true, .allowOutsideClick = false, .allowApplicationShutdown = true}; }

    Result<void> SettingsModal::OnOpen(EditorModalContext &context)
    {
        m_events = &context.events;
        m_revertedPublished = false;
        LoadSettingsForModal(m_draft, m_settings);
        return Result<void>::Success();
    }

    CloseDecision SettingsModal::CanClose(ModalCloseReason reason)
    {
        m_draft.dirty = CollectDraftSettings(m_draft) != m_draft.committed;
        if (reason == ModalCloseReason::Cancelled || !m_draft.dirty) return CloseDecision::Allow;
        return CloseDecision::Deny;
    }

    void SettingsModal::OnClose(ModalCloseReason)
    {
        m_draft.dirty = CollectDraftSettings(m_draft) != m_draft.committed;
        if (m_draft.dirty && !m_revertedPublished && m_events)
        {
            m_events->Publish(EditorSettingsChangedEvent{m_settings.Snapshot().revision, SettingsChangePhase::Reverted, SettingsDomain::All});
            m_revertedPublished = true;
        }
    }

    SettingsState &SettingsModal::Draft() noexcept { return m_draft; }
    bool SettingsModal::ApplyDraft() { return ApplySettings(m_draft, m_settings); }
} // namespace Horo::Editor
