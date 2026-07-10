#include "Horo/Editor/EditorModalHost.h"

#include <algorithm>
#include <utility>

namespace Horo::Editor
{
namespace
{

[[nodiscard]] Error MakeModalHostError(ModalHostError error)
{
    const char *code = "editor.modal_host.invalid_modal";
    const char *message = "The modal request is invalid.";
    switch (error)
    {
    case ModalHostError::Busy:
        code = "editor.modal_host.busy";
        message = "Another root modal is already active.";
        break;
    case ModalHostError::InvalidModal:
        break;
    case ModalHostError::DuplicateId:
        code = "editor.modal_host.duplicate_id";
        message = "The modal ID is already active.";
        break;
    case ModalHostError::ParentNotTop:
        code = "editor.modal_host.parent_not_top";
        message = "Only the current top modal may push a child.";
        break;
    case ModalHostError::ModalNotTop:
        code = "editor.modal_host.modal_not_top";
        message = "Only the current top modal may close.";
        break;
    case ModalHostError::StackLimitReached:
        code = "editor.modal_host.stack_limit_reached";
        message = "The modal stack has reached its configured depth limit.";
        break;
    case ModalHostError::CloseDenied:
        code = "editor.modal_host.close_denied";
        message = "A modal denied the requested close.";
        break;
    }
    return Error{ErrorCode{code}, ErrorDomainId{"horo.editor.modal_host"}, ErrorSeverity::Error, message};
}

} // namespace

/** @copydoc EditorModalHost::EditorModalHost */
EditorModalHost::EditorModalHost(EditorDataBus &events, std::size_t maximumDepth)
    : m_events(events), m_context{.events = m_events, .modals = *this}, m_maximumDepth(std::max<std::size_t>(maximumDepth, 1))
{
}

EditorModalHost::~EditorModalHost()
{
    ForceDetachAllForShutdown();
}

/** @copydoc EditorModalHost::OpenRoot */
Result<void> EditorModalHost::OpenRoot(std::unique_ptr<EditorModal> modal)
{
    if (HasOpenModal()) return ErrorFor(ModalHostError::Busy);
    if (!modal) return ErrorFor(ModalHostError::InvalidModal);
    if (const Result<void> validation = ValidateModalForPush(*modal); validation.HasError()) return validation;
    m_stack.push_back(Entry{.modal = std::move(modal)});
    return Result<void>::Success();
}

/** @copydoc EditorModalHost::PushChild */
Result<void> EditorModalHost::PushChild(ModalId parentId, std::unique_ptr<EditorModal> modal)
{
    if (!modal) return ErrorFor(ModalHostError::InvalidModal);
    if (m_stack.empty() || m_stack.back().modal->Id() != parentId) return ErrorFor(ModalHostError::ParentNotTop);
    if (const Result<void> validation = ValidateModalForPush(*modal); validation.HasError()) return validation;
    m_pendingChildOpens.push_back(Entry{.modal = std::move(modal)});
    return Result<void>::Success();
}

/** @copydoc EditorModalHost::RequestClose */
Result<void> EditorModalHost::RequestClose(ModalId modalId, ModalCloseReason reason)
{
    if (m_stack.empty() || m_stack.back().modal->Id() != modalId) return ErrorFor(ModalHostError::ModalNotTop);
    if (m_stack.back().opened && m_stack.back().modal->CanClose(reason) != CloseDecision::Allow)
        return ErrorFor(ModalHostError::CloseDenied);
    m_pendingCloseReasons.push_back(reason);
    return Result<void>::Success();
}

/** @copydoc EditorModalHost::HasOpenModal */
bool EditorModalHost::HasOpenModal() const noexcept
{
    return !m_stack.empty();
}

/** @copydoc EditorModalHost::TopModalId */
std::optional<ModalId> EditorModalHost::TopModalId() const
{
    if (m_stack.empty()) return std::nullopt;
    return m_stack.back().modal->Id();
}

/** @copydoc EditorModalHost::InteractionScope */
EditorInteractionScope EditorModalHost::InteractionScope() const noexcept
{
    if (m_stack.empty()) return {};
    return EditorInteractionScope{.kind = EditorInteractionScopeKind::Modal, .modalId = m_stack.back().modal->Id()};
}

/** @copydoc EditorModalHost::OnUpdate */
void EditorModalHost::OnUpdate(float dt)
{
    CommitPendingOpens();
    for (Entry &entry : m_stack)
    {
        if (entry.opened) entry.modal->OnUpdate(dt);
    }
    CommitPendingCloses();
}

/** @copydoc EditorModalHost::Draw */
void EditorModalHost::Draw()
{
    CommitPendingOpens();
    if (!m_stack.empty() && m_stack.back().opened)
    {
        const ModalFrameResult result = m_stack.back().modal->Draw();
        if (const std::optional<ModalCloseReason> close = result.CloseRequest(); close.has_value())
            (void)RequestClose(m_stack.back().modal->Id(), *close);
    }
    CommitPendingCloses();
}

/** @copydoc EditorModalHost::RequestCloseAllForShutdown */
Result<void> EditorModalHost::RequestCloseAllForShutdown()
{
    for (auto it = m_stack.rbegin(); it != m_stack.rend(); ++it)
    {
        if (it->opened && it->modal->CanClose(ModalCloseReason::ApplicationShutdown) != CloseDecision::Allow)
            return ErrorFor(ModalHostError::CloseDenied);
    }
    m_pendingCloseReasons.assign(m_stack.size(), ModalCloseReason::ApplicationShutdown);
    return Result<void>::Success();
}

/** @copydoc EditorModalHost::ForceDetachAllForShutdown */
void EditorModalHost::ForceDetachAllForShutdown()
{
    m_pendingCloseReasons.clear();
    m_pendingChildOpens.clear();
    while (!m_stack.empty())
    {
        RemoveTop(ModalCloseReason::ApplicationShutdown);
    }
}

Result<void> EditorModalHost::ValidateModalForPush(const EditorModal &modal) const
{
    if (!modal.Id().IsValid()) return ErrorFor(ModalHostError::InvalidModal);
    if (m_stack.size() + m_pendingChildOpens.size() >= m_maximumDepth) return ErrorFor(ModalHostError::StackLimitReached);
    if (ContainsId(modal.Id())) return ErrorFor(ModalHostError::DuplicateId);
    return Result<void>::Success();
}

Result<void> EditorModalHost::ErrorFor(ModalHostError error) const
{
    return Result<void>::Failure(MakeModalHostError(error));
}

void EditorModalHost::CommitPendingOpens()
{
    for (Entry &entry : m_pendingChildOpens)
    {
        m_stack.push_back(std::move(entry));
    }
    m_pendingChildOpens.clear();

    std::size_t index = 0;
    while (index < m_stack.size())
    {
        Entry &entry = m_stack[index];
        if (entry.opened)
        {
            ++index;
            continue;
        }
        const Result<void> result = entry.modal->OnOpen(m_context);
        if (result.HasError())
        {
            entry.modal->OnClose(ModalCloseReason::Cancelled);
            m_stack.erase(m_stack.begin() + static_cast<std::ptrdiff_t>(index));
            continue;
        }
        entry.opened = true;
        ++index;
    }
}

void EditorModalHost::CommitPendingCloses()
{
    while (!m_pendingCloseReasons.empty() && !m_stack.empty())
    {
        const ModalCloseReason reason = m_pendingCloseReasons.back();
        m_pendingCloseReasons.pop_back();
        RemoveTop(reason);
    }
    if (m_stack.empty()) m_pendingCloseReasons.clear();
}

void EditorModalHost::RemoveTop(ModalCloseReason reason)
{
    Entry &entry = m_stack.back();
    entry.modal->OnClose(reason);
    m_stack.pop_back();
}

bool EditorModalHost::ContainsId(ModalId id) const noexcept
{
    const auto hasId = [id](const Entry &entry) { return entry.modal->Id() == id; };
    return std::any_of(m_stack.begin(), m_stack.end(), hasId) ||
           std::any_of(m_pendingChildOpens.begin(), m_pendingChildOpens.end(), hasId);
}
} // namespace Horo::Editor
