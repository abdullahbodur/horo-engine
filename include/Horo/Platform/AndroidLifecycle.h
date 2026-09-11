#pragma once

/**
 * @file AndroidLifecycle.h
 * @brief Portable Android host lifecycle observations and owner-thread serialization.
 */

#include "Horo/Foundation/Result.h"

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>

namespace Horo::Platform {
    /** @brief Nonzero generation assigned to one Android Activity lifetime. */
    struct AndroidActivityGeneration {
        std::uint64_t value{}; /**< Zero denotes no Activity. */
        auto operator<=>(const AndroidActivityGeneration &) const = default;
    };

    /** @brief Nonzero generation assigned to one native-window lifetime. */
    struct AndroidWindowGeneration {
        std::uint64_t value{}; /**< Zero denotes no native window. */
        auto operator<=>(const AndroidWindowGeneration &) const = default;
    };

    /** @brief Nonzero generation assigned to one admitted presentation lifetime. */
    struct AndroidPresentationGeneration {
        std::uint64_t value{}; /**< Zero denotes no presentation. */
        auto operator<=>(const AndroidPresentationGeneration &) const = default;
    };

    /** @brief Process-level state owned by the Android application host. */
    enum class AndroidProcessState : std::uint8_t {
        Created,
        Running,   /**< Application work is admitted; a paused Activity returns here only after ActivityResumed. */
        Suspended, /**< Background/paused state; ActivityStarted visibility alone does not resume application work. */
        ShuttingDown,
        Stopped
    };

    /** @brief State of the current Activity generation. */
    enum class AndroidActivityState : std::uint8_t {
        Absent,
        Created,
        Started,
        Resumed,
        Paused,
        Stopped,
        Destroyed
    };

    /** @brief Typed callback observation accepted by AndroidLifecycleController. */
    struct AndroidLifecycleEvent {
        /** @brief Closed callback vocabulary; native callback types never cross this boundary. */
        enum class Kind : std::uint8_t {
            ProcessCreated,
            ActivityCreated,
            ActivityStarted,
            ActivityResumed,
            ActivityPaused,
            ActivityStopped,
            ActivityDestroyed,
            WindowAvailable,
            WindowLost,
            PresentationAvailable,
            PresentationLost,
            FinalShutdown,
        };

        Kind kind{Kind::ProcessCreated};              /**< Observed lifecycle change. */
        AndroidActivityGeneration activity{};         /**< Activity generation relevant to the observation. */
        AndroidWindowGeneration window{};             /**< Window generation relevant to the observation. */
        AndroidPresentationGeneration presentation{}; /**< Presentation generation relevant to the observation. */
    };

    /** @brief Immutable owner-thread lifecycle snapshot for portable runtime consumers. */
    struct AndroidLifecycleSnapshot {
        std::uint64_t revision{};                                    /**< Monotonic committed-state revision. */
        std::uint64_t processGeneration{};                           /**< Nonzero after ProcessCreated commits. */
        AndroidProcessState process{AndroidProcessState::Created};   /**< Current process state. */
        AndroidActivityGeneration activityGeneration{};              /**< Most recently admitted Activity generation. */
        AndroidActivityState activity{AndroidActivityState::Absent}; /**< Current Activity state. */
        AndroidWindowGeneration windowGeneration{};                  /**< Most recently admitted native-window generation. */
        AndroidPresentationGeneration presentationGeneration{};      /**< Most recently admitted presentation generation. */
        bool hasWindow{};                                            /**< Whether the current window generation is usable. */
        bool hasPresentation{};                                      /**< Whether presentation is admitted for the current window. */
        std::size_t queuedObservationCount{};                        /**< Observations awaiting owner-thread commit. */
        std::uint64_t duplicateObservationCount{};                   /**< Idempotent duplicate observations seen. */
        std::uint64_t rejectedObservationCount{};                    /**< Stale or illegal observations rejected. */
    };

    /**
     * @brief Bounded callback queue and deterministic owner-thread Android lifecycle state machine.
     *
     * Enqueue may be called by native callback threads. Drain, Snapshot and CompleteShutdown
     * belong to the application owner thread captured by Create. FinalShutdown closes callback
     * admission and enters ShuttingDown; the composition root cancels/drains owned work and uses
     * the existing reverse-registered runtime teardown before calling CompleteShutdown.
     */
    class AndroidLifecycleController final {
        struct ConstructionToken final {};

    public:
        static constexpr std::size_t MaximumQueueCapacity = 64;

        /**
         * @brief Creates one lifecycle authority.
         * @param ownerThread Application owner thread.
         * @param queueCapacity Bounded observation capacity in [1, MaximumQueueCapacity].
         * @return Controller or a typed invalid-capacity error.
         */
        [[nodiscard]] static Result<std::unique_ptr<AndroidLifecycleController>> Create(std::thread::id ownerThread,
                                                                                        std::size_t queueCapacity = MaximumQueueCapacity);

        AndroidLifecycleController(const AndroidLifecycleController &) = delete;
        AndroidLifecycleController &operator=(const AndroidLifecycleController &) = delete;

        /**
         * @brief Enqueues one callback observation without applying application state.
         * @param event Portable observation with generation evidence.
         * @return Success, queue saturation, invalid generation, or closed-admission error.
         */
        [[nodiscard]] Result<void> Enqueue(const AndroidLifecycleEvent &event);

        /**
         * @brief Constructs through the private capability used by Create.
         * @param token Unforgeable class-private construction capability.
         * @param ownerThread Application owner thread.
         * @param queueCapacity Validated bounded observation capacity.
         */
        AndroidLifecycleController(ConstructionToken, std::thread::id ownerThread, std::size_t queueCapacity) noexcept;

        /**
         * @brief Applies all observations present at the owner-thread cutoff.
         * @return Number of committed or idempotently consumed observations, or the first typed rejection after draining the cutoff.
         *
         * The cutoff is drained completely even when an observation is rejected, so a malformed
         * callback cannot strand later lifecycle work. The first rejection is returned.
         */
        [[nodiscard]] Result<std::size_t> Drain();

        /** @brief Returns the current immutable owner-thread snapshot. @return Snapshot by value or an affinity error. */
        [[nodiscard]] Result<AndroidLifecycleSnapshot> Snapshot() const;

        /**
         * @brief Marks reverse dependency teardown complete after FinalShutdown.
         * @return Success, affinity failure, or invalid-state failure.
         */
        [[nodiscard]] Result<void> CompleteShutdown();

    private:
        [[nodiscard]] Result<void> Apply(const AndroidLifecycleEvent &event);
        [[nodiscard]] Result<void> ApplyProcessCreated(const AndroidLifecycleEvent &event);
        [[nodiscard]] Result<void> ApplyActivityCreated(const AndroidLifecycleEvent &event);
        [[nodiscard]] Result<void> ApplyActivityTransition(const AndroidLifecycleEvent &event);
        [[nodiscard]] Result<void> ApplyActivityDestroyed(const AndroidLifecycleEvent &event);
        [[nodiscard]] Result<void> ApplyWindowAvailable(const AndroidLifecycleEvent &event);
        [[nodiscard]] Result<void> ApplyWindowLost(const AndroidLifecycleEvent &event);
        [[nodiscard]] Result<void> ApplyPresentationAvailable(const AndroidLifecycleEvent &event);
        [[nodiscard]] Result<void> ApplyPresentationLost(const AndroidLifecycleEvent &event);
        [[nodiscard]] Result<void> ApplyFinalShutdown(const AndroidLifecycleEvent &event);
        [[nodiscard]] Result<void> Commit() noexcept;
        [[nodiscard]] Result<void> Duplicate() noexcept;
        [[nodiscard]] bool IsOwnerThread() const noexcept;
        void RetirePresentation() noexcept;
        void RetireWindowAndPresentation() noexcept;

        mutable std::mutex queueMutex_;
        std::array<AndroidLifecycleEvent, MaximumQueueCapacity> queue_{};
        std::size_t queueCapacity_{};
        std::size_t queueHead_{};
        std::size_t queueSize_{};
        std::thread::id ownerThread_;
        AndroidLifecycleSnapshot state_{};
        bool admissionOpen_{true};
    };
}  // namespace Horo::Platform
