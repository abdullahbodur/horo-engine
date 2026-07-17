#include "Horo/Editor/EditorDataBus.h"
#include "Horo/Editor/EditorModalHost.h"

#include <cassert>
#include <memory>

namespace
{

using namespace Horo;
using namespace Horo::Editor;

struct ModalStats
{
    EditorModalContext *context = nullptr;
    int openCalls = 0;
    int updateCalls = 0;
    int drawCalls = 0;
    int closeCalls = 0;
    bool requestCloseOnUpdate = false;
    bool requestCloseOnDraw = false;
    bool remainedOpenAfterUpdateCloseRequest = false;
    bool allowShutdown = true;
    ModalCloseReason closeReason = ModalCloseReason::Cancelled;
};

class RecordingModal final : public EditorModal
{
public:
    RecordingModal(std::uint64_t id, ModalStats &stats) : m_id(ModalId{id}), m_stats(stats) {}

    [[nodiscard]] ModalId Id() const override { return m_id; }
    [[nodiscard]] ModalPresentation Presentation() const override { return {}; }
    [[nodiscard]] ModalClosePolicy ClosePolicy() const override { return {}; }

    Result<void> OnOpen(EditorModalContext &context) override
    {
        m_stats.context = &context;
        ++m_stats.openCalls;
        return Result<void>::Success();
    }

    void OnUpdate(float) override
    {
        ++m_stats.updateCalls;
        if (m_stats.requestCloseOnUpdate)
        {
            const Result<void> result = m_stats.context->modals.RequestClose(m_id, ModalCloseReason::Completed);
            assert(result.HasValue());
            m_stats.remainedOpenAfterUpdateCloseRequest = m_stats.context->modals.HasOpenModal();
        }
    }

    ModalFrameResult Draw() override
    {
        ++m_stats.drawCalls;
        return m_stats.requestCloseOnDraw ? ModalFrameResult::RequestClose(ModalCloseReason::Cancelled) : ModalFrameResult::None();
    }

    [[nodiscard]] CloseDecision CanClose(ModalCloseReason reason) override
    {
        return reason == ModalCloseReason::ApplicationShutdown && !m_stats.allowShutdown ? CloseDecision::Deny
                                                                                          : CloseDecision::Allow;
    }

    void OnClose(ModalCloseReason reason) override
    {
        ++m_stats.closeCalls;
        m_stats.closeReason = reason;
    }

private:
    ModalId m_id;
    ModalStats &m_stats;
};

void AcceptedRootGatesInteractionBeforeItsFirstOpenBoundary()
{
    EditorDataBus events;
    Input::InputRouter input;
    ModalStats stats;
    EditorModalHost host(events, input);

    assert(host.OpenRoot(std::make_unique<RecordingModal>(1, stats)).HasValue());
    assert(host.InteractionScope().kind == EditorInteractionScopeKind::Modal);
    assert(host.InteractionScope().modalId == ModalId{1});
    assert(stats.openCalls == 0);

    host.OnUpdate(0.0F);
    assert(stats.openCalls == 1);
    assert(stats.context != nullptr);
    assert(&stats.context->events == &events);
    assert(&stats.context->modals == &host);
}

void SecondRootIsRejectedAsBusy()
{
    EditorDataBus events;
    Input::InputRouter input;
    ModalStats firstStats;
    ModalStats secondStats;
    EditorModalHost host(events, input);
    assert(host.OpenRoot(std::make_unique<RecordingModal>(1, firstStats)).HasValue());

    const Result<void> result = host.OpenRoot(std::make_unique<RecordingModal>(2, secondStats));
    assert(result.HasError());
    assert(result.ErrorValue().code.Value() == "editor.modal_host.busy");
}

void CloseRequestedDuringUpdateIsDeferredUntilTheFrameBoundary()
{
    EditorDataBus events;
    Input::InputRouter input;
    ModalStats stats{.requestCloseOnUpdate = true};
    EditorModalHost host(events, input);

    assert(host.OpenRoot(std::make_unique<RecordingModal>(1, stats)).HasValue());
    host.OnUpdate(0.0F);

    assert(stats.openCalls == 1);
    assert(stats.remainedOpenAfterUpdateCloseRequest);
    assert(stats.closeCalls == 1);
    assert(!host.HasOpenModal());
}

void CloseRequestedDuringDrawIsDeferredUntilTheDrawBoundary()
{
    EditorDataBus events;
    Input::InputRouter input;
    ModalStats stats{.requestCloseOnDraw = true};
    EditorModalHost host(events, input);

    assert(host.OpenRoot(std::make_unique<RecordingModal>(1, stats)).HasValue());
    host.OnUpdate(0.0F);
    host.Draw();

    assert(stats.openCalls == 1);
    assert(stats.drawCalls == 1);
    assert(stats.closeCalls == 1);
}

void TopAndScopeRestoreOnlyAfterDeferredCloseBoundary()
{
    EditorDataBus events;
    Input::InputRouter input;
    ModalStats stats;
    EditorModalHost host(events, input);
    assert(host.OpenRoot(std::make_unique<RecordingModal>(1, stats)).HasValue());
    host.OnUpdate(0.0F);

    assert(host.RequestClose(ModalId{1}, ModalCloseReason::Cancelled).HasValue());
    assert(host.TopModalId() == ModalId{1});
    assert(host.InteractionScope().kind == EditorInteractionScopeKind::Modal);

    host.OnUpdate(0.0F);
    assert(!host.TopModalId().has_value());
    assert(host.InteractionScope().kind == EditorInteractionScopeKind::Workspace);
}

void OnlyCurrentTopModalMayPushAChild()
{
    EditorDataBus events;
    Input::InputRouter input;
    ModalStats rootStats;
    ModalStats rejectedStats;
    ModalStats childStats;
    EditorModalHost host(events, input);
    assert(host.OpenRoot(std::make_unique<RecordingModal>(1, rootStats)).HasValue());
    host.OnUpdate(0.0F);

    const Result<void> invalid = host.PushChild(ModalId{2}, std::make_unique<RecordingModal>(3, rejectedStats));
    assert(invalid.HasError());
    assert(invalid.ErrorValue().code.Value() == "editor.modal_host.parent_not_top");

    assert(host.PushChild(ModalId{1}, std::make_unique<RecordingModal>(2, childStats)).HasValue());
    host.OnUpdate(0.0F);
    assert(host.TopModalId() == ModalId{2});
}

void ChildPushCommitsAtTheNextHostFrameBoundary()
{
    EditorDataBus events;
    Input::InputRouter input;
    ModalStats rootStats;
    ModalStats childStats;
    EditorModalHost host(events, input);

    assert(host.OpenRoot(std::make_unique<RecordingModal>(1, rootStats)).HasValue());
    host.OnUpdate(0.0F);
    assert(host.PushChild(ModalId{1}, std::make_unique<RecordingModal>(2, childStats)).HasValue());

    assert(host.TopModalId() == ModalId{1});
    assert(host.InteractionScope().modalId == ModalId{1});
    assert(childStats.openCalls == 0);

    host.OnUpdate(0.0F);
    assert(host.TopModalId() == ModalId{2});
    assert(host.InteractionScope().modalId == ModalId{2});
    assert(childStats.openCalls == 1);
}

void ForceDetachClosesEachModalOnceAndDoesNotRepeatCallbacks()
{
    EditorDataBus events;
    Input::InputRouter input;
    ModalStats rootStats;
    ModalStats childStats;
    EditorModalHost host(events, input);

    assert(host.OpenRoot(std::make_unique<RecordingModal>(1, rootStats)).HasValue());
    host.OnUpdate(0.0F);
    assert(host.PushChild(ModalId{1}, std::make_unique<RecordingModal>(2, childStats)).HasValue());
    host.OnUpdate(0.0F);

    host.ForceDetachAllForShutdown();
    assert(rootStats.closeCalls == 1);
    assert(childStats.closeCalls == 1);
    assert(!host.HasOpenModal());
    assert(host.InteractionScope().kind == EditorInteractionScopeKind::Workspace);

    host.ForceDetachAllForShutdown();
    assert(rootStats.closeCalls == 1);
    assert(childStats.closeCalls == 1);
}

void OrderlyShutdownIsSynchronousIdempotentAndStopsNewRequests()
{
    EditorDataBus events;
    Input::InputRouter input;
    ModalStats stats;
    ModalStats rejected;
    EditorModalHost host(events, input);
    assert(host.OpenRoot(std::make_unique<RecordingModal>(1, stats)).HasValue());
    host.OnUpdate(0.0F);

    assert(host.RequestCloseAllForShutdown().HasValue());
    assert(stats.closeCalls == 1);
    assert(stats.closeReason == ModalCloseReason::ApplicationShutdown);
    assert(!host.HasOpenModal());
    assert(host.RequestCloseAllForShutdown().HasValue());
    assert(stats.closeCalls == 1);

    const Result<void> openAfterShutdown = host.OpenRoot(std::make_unique<RecordingModal>(2, rejected));
    assert(openAfterShutdown.HasError());
    assert(openAfterShutdown.ErrorValue().code.Value() == "editor.modal_host.busy");
}

void DeniedShutdownKeepsTheModalAndHostUsable()
{
    EditorDataBus events;
    Input::InputRouter input;
    ModalStats stats{.allowShutdown = false};
    EditorModalHost host(events, input);
    assert(host.OpenRoot(std::make_unique<RecordingModal>(1, stats)).HasValue());
    host.OnUpdate(0.0F);

    const Result<void> shutdown = host.RequestCloseAllForShutdown();
    assert(shutdown.HasError());
    assert(shutdown.ErrorValue().code.Value() == "editor.modal_host.close_denied");
    assert(host.HasOpenModal());
    assert(stats.closeCalls == 0);
    assert(host.RequestClose(ModalId{1}, ModalCloseReason::Cancelled).HasValue());
    host.OnUpdate(0.0F);
    assert(stats.closeCalls == 1);
}

} // namespace

int main()
{
    AcceptedRootGatesInteractionBeforeItsFirstOpenBoundary();
    SecondRootIsRejectedAsBusy();
    CloseRequestedDuringUpdateIsDeferredUntilTheFrameBoundary();
    CloseRequestedDuringDrawIsDeferredUntilTheDrawBoundary();
    TopAndScopeRestoreOnlyAfterDeferredCloseBoundary();
    OnlyCurrentTopModalMayPushAChild();
    ChildPushCommitsAtTheNextHostFrameBoundary();
    ForceDetachClosesEachModalOnceAndDoesNotRepeatCallbacks();
    OrderlyShutdownIsSynchronousIdempotentAndStopsNewRequests();
    DeniedShutdownKeepsTheModalAndHostUsable();
    return 0;
}
