#include "Horo/Runtime/RuntimeHost.h"

#include <utility>

namespace Horo::Runtime
{
    /** @copydoc RuntimeHost::Create */
    Result<std::unique_ptr<RuntimeHost>> RuntimeHost::Create(Clock& clock, FrameSchedulerConfig config)
    {
        auto scheduler = FrameScheduler::Create(clock, config);
        if (scheduler.HasError())
        {
            return Result<std::unique_ptr<RuntimeHost>>::Failure(scheduler.ErrorValue());
        }
        return Result<std::unique_ptr<RuntimeHost>>::Success(
            std::unique_ptr<RuntimeHost>(new RuntimeHost(std::move(scheduler).Value())));
    }

    RuntimeHost::RuntimeHost(std::unique_ptr<FrameScheduler> scheduler) noexcept : scheduler_(std::move(scheduler))
    {
    }

    RuntimeHost::~RuntimeHost()
    {
        Shutdown();
    }

    /** @copydoc RuntimeHost::AddParticipant */
    Result<void> RuntimeHost::AddParticipant(std::unique_ptr<RuntimeLifecycleParticipant> participant)
    {
        return lifecycle_.AddParticipant(std::move(participant));
    }

    /** @copydoc RuntimeHost::Startup */
    Result<void> RuntimeHost::Startup()
    {
        return lifecycle_.Startup(cancellation_.Token());
    }

    /** @copydoc RuntimeHost::RunFrame */
    Result<void> RuntimeHost::RunFrame()
    {
        const RuntimeLifecycleState state = lifecycle_.State();
        const bool suspended = state == RuntimeLifecycleState::Suspended;
        Result<void> result = scheduler_->RunFrame(lifecycle_, cancellation_.Token(), suspended);
        if (result.HasError())
        {
            lifecycle_.MarkFailed();
            Shutdown();
        }
        return result;
    }

    /** @copydoc RuntimeHost::Suspend */
    Result<void> RuntimeHost::Suspend()
    {
        Result<void> result = lifecycle_.Suspend();
        if (result.HasValue())
            scheduler_->ResetClock();
        return result;
    }

    /** @copydoc RuntimeHost::Resume */
    Result<void> RuntimeHost::Resume()
    {
        Result<void> result = lifecycle_.Resume();
        if (result.HasValue())
            scheduler_->ResetClock();
        return result;
    }

    /** @copydoc RuntimeHost::RequestShutdown */
    void RuntimeHost::RequestShutdown() noexcept
    {
        cancellation_.RequestCancellation();
    }

    /** @copydoc RuntimeHost::Shutdown */
    void RuntimeHost::Shutdown() noexcept
    {
        cancellation_.RequestCancellation();
        lifecycle_.Shutdown();
    }

    /** @copydoc RuntimeHost::State */
    RuntimeLifecycleState RuntimeHost::State() const noexcept
    {
        return lifecycle_.State();
    }

    /** @copydoc RuntimeHost::Statistics */
    FrameSchedulerStatistics RuntimeHost::Statistics() const noexcept
    {
        return scheduler_->Statistics();
    }
} // namespace Horo::Runtime
