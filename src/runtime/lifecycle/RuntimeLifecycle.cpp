#include "Horo/Runtime/RuntimeLifecycle.h"

#include "RuntimeErrors.h"

#include <exception>
#include <utility>

namespace Horo::Runtime {
    namespace {
        [[nodiscard]] Result<void> StateFailure() {
            return Result<void>::Failure(MakeError(RuntimeErrors::InvalidLifecycleState));
        }

        template<typename Callback>
        [[nodiscard]] Result<void> ContainParticipantException(Callback &&callback) {
            try {
                return callback();
            } catch (const std::exception &exception) {
                return Result<void>::Failure(MakeError(RuntimeErrors::UnexpectedException, exception.what()));
            } catch (...) {
                return Result<void>::Failure(MakeError(RuntimeErrors::UnexpectedException));
            }
        }
    } // namespace

    RuntimeLifecycle::~RuntimeLifecycle() {
        Shutdown();
    }

    /** @copydoc RuntimeLifecycle::AddParticipant */
    Result<void> RuntimeLifecycle::AddParticipant(std::unique_ptr<RuntimeLifecycleParticipant> participant) {
        if (state_ != RuntimeLifecycleState::Created)
            return StateFailure();
        if (!participant) {
            return Result<void>::Failure(MakeError(RuntimeErrors::NullParticipant));
        }
        participants_.push_back(std::move(participant));
        return Result<void>::Success();
    }

    /** @copydoc RuntimeLifecycle::Startup */
    Result<void> RuntimeLifecycle::Startup(const CancellationToken &cancellation) {
        if (state_ != RuntimeLifecycleState::Created)
            return StateFailure();
        state_ = RuntimeLifecycleState::Initializing;
        for (auto &participant: participants_) {
            if (cancellation.IsCancellationRequested()) {
                state_ = RuntimeLifecycleState::Failed;
                Shutdown();
                return Result<void>::Failure(MakeError(RuntimeErrors::Cancelled));
            }
            Result<void> result = ContainParticipantException([&] { return participant->Startup(cancellation); });
            if (result.HasError()) {
                state_ = RuntimeLifecycleState::Failed;
                Shutdown();
                return result;
            }
            ++startedParticipantCount_;
        }
        state_ = RuntimeLifecycleState::Ready;
        state_ = RuntimeLifecycleState::Running;
        return Result<void>::Success();
    }

    /** @copydoc RuntimeLifecycle::DispatchPhase */
    Result<void> RuntimeLifecycle::DispatchPhase(const RuntimePhase phase, const FrameContext &context) {
        if (state_ != RuntimeLifecycleState::Running && state_ != RuntimeLifecycleState::Suspended)
            return StateFailure();
        for (std::size_t index = 0; index < startedParticipantCount_; ++index) {
            Result<void> result =
                    ContainParticipantException([&] { return participants_[index]->OnPhase(phase, context); });
            if (result.HasError())
                return result;
        }
        return Result<void>::Success();
    }

    /** @copydoc RuntimeLifecycle::DispatchFixedUpdate */
    Result<void> RuntimeLifecycle::DispatchFixedUpdate(const FixedStepContext &context) {
        if (state_ != RuntimeLifecycleState::Running)
            return StateFailure();
        for (std::size_t index = 0; index < startedParticipantCount_; ++index) {
            Result<void> result = ContainParticipantException([&] {
                return participants_[index]->OnFixedUpdate(context);
            });
            if (result.HasError())
                return result;
        }
        return Result<void>::Success();
    }

    /** @copydoc RuntimeLifecycle::Suspend */
    Result<void> RuntimeLifecycle::Suspend() {
        if (state_ != RuntimeLifecycleState::Running)
            return StateFailure();
        state_ = RuntimeLifecycleState::Suspended;
        return Result<void>::Success();
    }

    /** @copydoc RuntimeLifecycle::Resume */
    Result<void> RuntimeLifecycle::Resume() {
        if (state_ != RuntimeLifecycleState::Suspended)
            return StateFailure();
        state_ = RuntimeLifecycleState::Running;
        return Result<void>::Success();
    }

    /** @copydoc RuntimeLifecycle::MarkFailed */
    void RuntimeLifecycle::MarkFailed() noexcept {
        if (state_ == RuntimeLifecycleState::Running || state_ == RuntimeLifecycleState::Suspended ||
            state_ == RuntimeLifecycleState::Ready) {
            state_ = RuntimeLifecycleState::Failed;
        }
    }

    /** @copydoc RuntimeLifecycle::Shutdown */
    void RuntimeLifecycle::Shutdown() noexcept {
        if (state_ == RuntimeLifecycleState::Stopped || state_ == RuntimeLifecycleState::Stopping)
            return;
        state_ = RuntimeLifecycleState::Stopping;
        while (startedParticipantCount_ > 0) {
            --startedParticipantCount_;
            participants_[startedParticipantCount_]->Shutdown();
        }
        state_ = RuntimeLifecycleState::Stopped;
    }

    /** @copydoc RuntimeLifecycle::State */
    RuntimeLifecycleState RuntimeLifecycle::State() const noexcept {
        return state_;
    }
} // namespace Horo::Runtime
