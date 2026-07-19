#pragma once

/**
 * @file RuntimeLifecycle.h
 * @brief Ordered runtime participant startup, frame dispatch, and teardown contracts.
 */

#include "Horo/Runtime/FrameScheduler.h"

#include <memory>
#include <vector>

namespace Horo::Runtime {
    /** @brief Observable state of one runtime lifecycle. */
    enum class RuntimeLifecycleState : std::uint8_t {
        Created,
        Initializing,
        Ready,
        Running,
        Suspended,
        Failed,
        Stopping,
        Stopped,
    };

    /**
     * @brief Explicit participant in host startup, frame phases, and reverse-order shutdown.
     *
     * Expected failures are returned as Result values. Implementations must not throw;
     * the host nevertheless contains unexpected exceptions at this boundary. Startup
     * is transactional: a failure leaves the participant with no live resources.
     */
    class RuntimeLifecycleParticipant {
    public:
        virtual ~RuntimeLifecycleParticipant() = default;

        /**
         * @brief Starts participant-owned resources transactionally.
         * @param cancellation Token valid for this callback; it must not be retained.
         * @return Success or a typed failure after rolling back participant-local partial state.
         */
        [[nodiscard]] virtual Result<void> Startup(const CancellationToken &cancellation) = 0;

        /**
         * @brief Handles one non-fixed phase.
         * @param phase Current canonical phase; never RuntimePhase::FixedUpdate.
         * @param context Frame-owned context valid only for this callback.
         * @return Success or a typed failure that stops the frame.
         */
        [[nodiscard]] virtual Result<void> OnPhase(RuntimePhase phase, const FrameContext &context) = 0;

        /**
         * @brief Handles one attempted fixed tick.
         * @param context Tick context valid only for this callback.
         * @return Success or a typed failure; a failed tick is not committed.
         */
        [[nodiscard]] virtual Result<void> OnFixedUpdate(const FixedStepContext &context) = 0;

        /** @brief Releases resources; called once for each successfully started participant in reverse order. */
        virtual void Shutdown() noexcept = 0;
    };

    /** @brief Owns ordered runtime participants and their explicit lifecycle state machine. */
    class RuntimeLifecycle final {
    public:
        RuntimeLifecycle() = default;

        ~RuntimeLifecycle();

        RuntimeLifecycle(const RuntimeLifecycle &) = delete;

        RuntimeLifecycle &operator=(const RuntimeLifecycle &) = delete;

        /** @brief Adds one owned participant before startup. @param participant Non-null participant. @return Success or
         * typed state/input failure. */
        [[nodiscard]] Result<void> AddParticipant(std::unique_ptr<RuntimeLifecycleParticipant> participant);

        /** @brief Starts participants in registration order. @param cancellation Host cancellation token. @return Success
         * or first startup failure. */
        [[nodiscard]] Result<void> Startup(const CancellationToken &cancellation);

        /**
         * @brief Dispatches one phase in registration order.
         * @param phase Non-fixed canonical phase to dispatch.
         * @param context Frame-owned context valid for the duration of the call.
         * @return Success or the first participant failure.
         */
        [[nodiscard]] Result<void> DispatchPhase(RuntimePhase phase, const FrameContext &context);

        /**
         * @brief Dispatches one fixed tick in registration order.
         * @param context Fixed-step context valid for the duration of the call.
         * @return Success or the first participant failure.
         */
        [[nodiscard]] Result<void> DispatchFixedUpdate(const FixedStepContext &context);

        /** @brief Transitions a running lifecycle to suspended. @return Success or typed state failure. */
        [[nodiscard]] Result<void> Suspend();

        /** @brief Transitions a suspended lifecycle back to running. @return Success or typed state failure. */
        [[nodiscard]] Result<void> Resume();

        /** @brief Marks the running lifecycle failed before teardown. */
        void MarkFailed() noexcept;

        /** @brief Shuts successfully started participants down in reverse order; safe repeatedly. */
        void Shutdown() noexcept;

        /** @brief Returns the current observable lifecycle state. @return Current state by value. */
        [[nodiscard]] RuntimeLifecycleState State() const noexcept;

    private:
        std::vector<std::unique_ptr<RuntimeLifecycleParticipant> > participants_;
        std::size_t startedParticipantCount_{};
        RuntimeLifecycleState state_{RuntimeLifecycleState::Created};
    };
} // namespace Horo::Runtime
