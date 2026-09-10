#include "Horo/Platform/AndroidLifecycle.h"

#include "Horo/Platform/PlatformErrors.h"

#include <optional>
#include <utility>

namespace Horo::Platform {
    namespace {
        [[nodiscard]] Result<void> Failure(const ErrorCodeDescriptor &descriptor) {
            return Result<void>::Failure(MakeError(descriptor));
        }

        [[nodiscard]] bool RequiresActivity(const AndroidLifecycleEvent::Kind kind) noexcept {
            using enum AndroidLifecycleEvent::Kind;
            return kind == ActivityCreated || kind == ActivityStarted || kind == ActivityResumed || kind == ActivityPaused ||
                   kind == ActivityStopped || kind == ActivityDestroyed || kind == WindowAvailable;
        }

        [[nodiscard]] bool RequiresWindow(const AndroidLifecycleEvent::Kind kind) noexcept {
            using enum AndroidLifecycleEvent::Kind;
            return kind == WindowAvailable || kind == WindowLost || kind == PresentationAvailable;
        }

        [[nodiscard]] bool RequiresPresentation(const AndroidLifecycleEvent::Kind kind) noexcept {
            using enum AndroidLifecycleEvent::Kind;
            return kind == PresentationAvailable || kind == PresentationLost;
        }
    }  // namespace

    AndroidLifecycleController::AndroidLifecycleController(const std::thread::id ownerThread, const std::size_t queueCapacity) noexcept
        : queueCapacity_(queueCapacity), ownerThread_(ownerThread) {}

    /** @copydoc AndroidLifecycleController::Create */
    Result<std::unique_ptr<AndroidLifecycleController>> AndroidLifecycleController::Create(const std::thread::id ownerThread,
                                                                                           const std::size_t queueCapacity) {
        if (ownerThread == std::thread::id{} || queueCapacity == 0 || queueCapacity > MaximumQueueCapacity) {
            return Result<std::unique_ptr<AndroidLifecycleController>>::Failure(MakeError(PlatformErrors::InvalidLifecycleConfiguration));
        }
        return Result<std::unique_ptr<AndroidLifecycleController>>::Success(
            std::unique_ptr<AndroidLifecycleController>(new AndroidLifecycleController(ownerThread, queueCapacity)));
    }

    /** @copydoc AndroidLifecycleController::Enqueue */
    Result<void> AndroidLifecycleController::Enqueue(const AndroidLifecycleEvent event) {
        using enum AndroidLifecycleEvent::Kind;
        if ((RequiresActivity(event.kind) && event.activity.value == 0) || (RequiresWindow(event.kind) && event.window.value == 0) ||
            (RequiresPresentation(event.kind) && event.presentation.value == 0)) {
            return Failure(PlatformErrors::InvalidLifecycleGeneration);
        }

        std::lock_guard lock(queueMutex_);
        if (!admissionOpen_)
            return Failure(PlatformErrors::LifecycleAdmissionClosed);
        if (queueSize_ == queueCapacity_)
            return Failure(PlatformErrors::LifecycleQueueSaturated);
        const std::size_t tail = (queueHead_ + queueSize_) % queueCapacity_;
        queue_[tail] = event;
        ++queueSize_;
        if (event.kind == FinalShutdown)
            admissionOpen_ = false;
        return Result<void>::Success();
    }

    /** @copydoc AndroidLifecycleController::Drain */
    Result<std::size_t> AndroidLifecycleController::Drain() {
        if (!IsOwnerThread()) {
            return Result<std::size_t>::Failure(MakeError(PlatformErrors::LifecycleOwnerThreadRequired));
        }

        std::size_t cutoff{};
        {
            std::lock_guard lock(queueMutex_);
            cutoff = queueSize_;
        }

        std::size_t consumed{};
        std::optional<Error> firstError;
        while (consumed < cutoff) {
            AndroidLifecycleEvent event;
            {
                std::lock_guard lock(queueMutex_);
                event = queue_[queueHead_];
                queueHead_ = (queueHead_ + 1) % queueCapacity_;
                --queueSize_;
            }
            Result<void> applied = Apply(event);
            if (applied.HasError()) {
                ++state_.rejectedObservationCount;
                if (!firstError.has_value())
                    firstError = applied.ErrorValue();
            }
            ++consumed;
        }
        if (firstError.has_value())
            return Result<std::size_t>::Failure(std::move(*firstError));
        return Result<std::size_t>::Success(consumed);
    }

    /** @copydoc AndroidLifecycleController::Snapshot */
    Result<AndroidLifecycleSnapshot> AndroidLifecycleController::Snapshot() const {
        if (!IsOwnerThread())
            return Result<AndroidLifecycleSnapshot>::Failure(MakeError(PlatformErrors::LifecycleOwnerThreadRequired));
        AndroidLifecycleSnapshot result = state_;
        {
            std::lock_guard lock(queueMutex_);
            result.queuedObservationCount = queueSize_;
        }
        return Result<AndroidLifecycleSnapshot>::Success(result);
    }

    /** @copydoc AndroidLifecycleController::CompleteShutdown */
    Result<void> AndroidLifecycleController::CompleteShutdown() {
        if (!IsOwnerThread())
            return Failure(PlatformErrors::LifecycleOwnerThreadRequired);
        if (state_.process != AndroidProcessState::ShuttingDown)
            return Failure(PlatformErrors::InvalidLifecycleTransition);
        state_.process = AndroidProcessState::Stopped;
        ++state_.revision;
        return Result<void>::Success();
    }

    Result<void> AndroidLifecycleController::Apply(const AndroidLifecycleEvent &event) {
        if (state_.process == AndroidProcessState::ShuttingDown || state_.process == AndroidProcessState::Stopped)
            return event.kind == AndroidLifecycleEvent::Kind::FinalShutdown ? Duplicate()
                                                                            : Failure(PlatformErrors::StaleLifecycleGeneration);

        using Handler = Result<void> (AndroidLifecycleController::*)(const AndroidLifecycleEvent &);
        static constexpr std::array<Handler, 12> handlers{
            &AndroidLifecycleController::ApplyProcessCreated,     &AndroidLifecycleController::ApplyActivityCreated,
            &AndroidLifecycleController::ApplyActivityTransition, &AndroidLifecycleController::ApplyActivityTransition,
            &AndroidLifecycleController::ApplyActivityTransition, &AndroidLifecycleController::ApplyActivityTransition,
            &AndroidLifecycleController::ApplyActivityDestroyed,  &AndroidLifecycleController::ApplyWindowAvailable,
            &AndroidLifecycleController::ApplyWindowLost,         &AndroidLifecycleController::ApplyPresentationAvailable,
            &AndroidLifecycleController::ApplyPresentationLost,   &AndroidLifecycleController::ApplyFinalShutdown,
        };
        const std::size_t index = static_cast<std::size_t>(event.kind);
        if (index >= handlers.size())
            return Failure(PlatformErrors::InvalidLifecycleTransition);
        return (this->*handlers[index])(event);
    }

    Result<void> AndroidLifecycleController::ApplyProcessCreated(const AndroidLifecycleEvent &) {
        if (state_.processGeneration != 0)
            return Duplicate();
        state_.processGeneration = 1;
        state_.process = AndroidProcessState::Running;
        return Commit();
    }

    Result<void> AndroidLifecycleController::ApplyActivityCreated(const AndroidLifecycleEvent &event) {
        if (state_.processGeneration == 0)
            return Failure(PlatformErrors::InvalidLifecycleTransition);
        if (event.activity == state_.activityGeneration && state_.activity == AndroidActivityState::Created)
            return Duplicate();
        if (event.activity <= state_.activityGeneration)
            return Failure(PlatformErrors::StaleLifecycleGeneration);
        RetireWindowAndPresentation();
        state_.activityGeneration = event.activity;
        state_.activity = AndroidActivityState::Created;
        return Commit();
    }

    Result<void> AndroidLifecycleController::ApplyActivityTransition(const AndroidLifecycleEvent &event) {
        struct Transition {
            AndroidActivityState firstSource;
            AndroidActivityState secondSource;
            AndroidActivityState target;
            AndroidProcessState process;
            bool updatesProcess;
        };
        using enum AndroidActivityState;
        static constexpr std::array transitions{
            Transition{Created, Stopped, Started, AndroidProcessState::Running, false},
            Transition{Started, Paused, Resumed, AndroidProcessState::Running, true},
            Transition{Resumed, Resumed, Paused, AndroidProcessState::Suspended, true},
            Transition{Paused, Started, Stopped, AndroidProcessState::Suspended, true},
        };
        const auto transitionIndex =
            static_cast<std::size_t>(event.kind) - static_cast<std::size_t>(AndroidLifecycleEvent::Kind::ActivityStarted);
        if (transitionIndex >= transitions.size())
            return Failure(PlatformErrors::InvalidLifecycleTransition);
        if (event.activity != state_.activityGeneration)
            return Failure(PlatformErrors::StaleLifecycleGeneration);
        const Transition &transition = transitions[transitionIndex];
        if (state_.activity == transition.target)
            return Duplicate();
        if (state_.activity != transition.firstSource && state_.activity != transition.secondSource)
            return Failure(PlatformErrors::InvalidLifecycleTransition);
        state_.activity = transition.target;
        if (transition.updatesProcess)
            state_.process = transition.process;
        return Commit();
    }

    Result<void> AndroidLifecycleController::ApplyActivityDestroyed(const AndroidLifecycleEvent &event) {
        if (event.activity != state_.activityGeneration)
            return Failure(PlatformErrors::StaleLifecycleGeneration);
        if (state_.activity == AndroidActivityState::Destroyed)
            return Duplicate();
        if (state_.activity != AndroidActivityState::Stopped && state_.activity != AndroidActivityState::Created)
            return Failure(PlatformErrors::InvalidLifecycleTransition);
        RetireWindowAndPresentation();
        state_.activity = AndroidActivityState::Destroyed;
        state_.process = AndroidProcessState::Suspended;
        return Commit();
    }

    Result<void> AndroidLifecycleController::ApplyWindowAvailable(const AndroidLifecycleEvent &event) {
        if (event.activity != state_.activityGeneration)
            return Failure(PlatformErrors::StaleLifecycleGeneration);
        if (state_.activity == AndroidActivityState::Destroyed || state_.activity == AndroidActivityState::Absent)
            return Failure(PlatformErrors::InvalidLifecycleTransition);
        if (event.window == state_.windowGeneration && state_.hasWindow)
            return Duplicate();
        if (event.window <= state_.windowGeneration)
            return Failure(PlatformErrors::StaleLifecycleGeneration);
        RetirePresentation();
        state_.windowGeneration = event.window;
        state_.hasWindow = true;
        return Commit();
    }

    Result<void> AndroidLifecycleController::ApplyWindowLost(const AndroidLifecycleEvent &event) {
        if (event.window != state_.windowGeneration || !state_.hasWindow)
            return event.window == state_.windowGeneration ? Duplicate() : Failure(PlatformErrors::StaleLifecycleGeneration);
        RetireWindowAndPresentation();
        return Commit();
    }

    Result<void> AndroidLifecycleController::ApplyPresentationAvailable(const AndroidLifecycleEvent &event) {
        if (event.window != state_.windowGeneration || !state_.hasWindow)
            return Failure(PlatformErrors::StaleLifecycleGeneration);
        if (event.presentation == state_.presentationGeneration && state_.hasPresentation)
            return Duplicate();
        if (event.presentation <= state_.presentationGeneration)
            return Failure(PlatformErrors::StaleLifecycleGeneration);
        state_.presentationGeneration = event.presentation;
        state_.hasPresentation = true;
        return Commit();
    }

    Result<void> AndroidLifecycleController::ApplyPresentationLost(const AndroidLifecycleEvent &event) {
        if (event.presentation != state_.presentationGeneration || !state_.hasPresentation)
            return event.presentation == state_.presentationGeneration ? Duplicate() : Failure(PlatformErrors::StaleLifecycleGeneration);
        RetirePresentation();
        return Commit();
    }

    Result<void> AndroidLifecycleController::ApplyFinalShutdown(const AndroidLifecycleEvent &) {
        RetireWindowAndPresentation();
        state_.process = AndroidProcessState::ShuttingDown;
        state_.activity = state_.activityGeneration.value == 0 ? AndroidActivityState::Absent : AndroidActivityState::Destroyed;
        return Commit();
    }

    Result<void> AndroidLifecycleController::Commit() noexcept {
        ++state_.revision;
        return Result<void>::Success();
    }

    Result<void> AndroidLifecycleController::Duplicate() noexcept {
        ++state_.duplicateObservationCount;
        return Result<void>::Success();
    }

    bool AndroidLifecycleController::IsOwnerThread() const noexcept {
        return std::this_thread::get_id() == ownerThread_;
    }

    void AndroidLifecycleController::RetirePresentation() noexcept {
        state_.hasPresentation = false;
    }

    void AndroidLifecycleController::RetireWindowAndPresentation() noexcept {
        RetirePresentation();
        state_.hasWindow = false;
    }
}  // namespace Horo::Platform
