#include "Horo/Editor/EditorSettingsEvents.h"
#include "Horo/Editor/SettingsModal.h"
#include "Horo/Foundation/Logging/LogContext.h"
#include "Horo/Foundation/Logging/Logger.h"
#include <memory>

namespace Horo::Editor
{
SettingsModal::SettingsModal(const EditorGuiContext &context, EditorSettingsService &settings,
                             const std::uintptr_t logo) noexcept
    : m_context(context), m_settings(settings), m_logo(logo)
{
}

ModalId SettingsModal::Id() const
{
    return ModalId{kModalId};
}

ModalPresentation SettingsModal::Presentation() const
{
    return {.size = ModalSizePolicy::Large, .dimWorkspace = true};
}

ModalClosePolicy SettingsModal::ClosePolicy() const
{
    return {
        .allowCloseButton = true, .allowEscape = true, .allowOutsideClick = false, .allowApplicationShutdown = true};
}

Result<void> SettingsModal::OnOpen(EditorModalContext &context)
{
    m_events = &context.events;
    m_revertedPublished = false;
    LoadSettingsForModal(m_draft, m_settings);
    const auto rev = m_settings.Snapshot().revision;
    m_logCtx = std::make_unique<Log::LogContext>("modal", "settings", "modal_id", std::to_string(kModalId), "revision",
                                                 std::to_string(rev));
    LOG_INFO("editor.settings", "SettingsModal opened (revision=%llu).", rev);
    return Result<void>::Success();
}

CloseDecision SettingsModal::CanClose(const ModalCloseReason reason)
{
    m_draft.dirty = CollectDraftSettings(m_draft) != m_draft.committed;
    if (reason == ModalCloseReason::Cancelled || !m_draft.dirty)
        return CloseDecision::Allow;
    return CloseDecision::Deny;
}

void SettingsModal::OnClose(const ModalCloseReason reason)
{
    m_draft.dirty = CollectDraftSettings(m_draft) != m_draft.committed;
    const char *reasonStr = (reason == ModalCloseReason::Completed)   ? "completed"
                            : (reason == ModalCloseReason::Cancelled) ? "cancelled"
                                                                      : "app_shutdown";
    LOG_INFO("editor.settings", "SettingsModal closed (reason=%s, dirty=%s).", reasonStr, m_draft.dirty ? "yes" : "no");
    if (m_draft.dirty && !m_revertedPublished && m_events)
    {
        LOG_DEBUG("editor.settings", "Settings reverted — publishing Reverted event.");
        m_events->Publish(EditorSettingsChangedEvent{m_settings.Snapshot().revision, SettingsChangePhase::Reverted,
                                                     SettingsDomain::All});
        m_revertedPublished = true;
    }
    m_logCtx.reset(); // pop MDC frame
}

SettingsState &SettingsModal::Draft() noexcept
{
    return m_draft;
}
bool SettingsModal::ApplyDraft()
{
    const bool ok = ApplySettings(m_draft, m_settings);
    LOG_INFO("editor.settings", "Settings applied (success=%s, revision=%llu).", ok ? "yes" : "no",
             m_settings.Snapshot().revision);
    return ok;
}
} // namespace Horo::Editor
