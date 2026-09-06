#pragma once

#include "Horo/Foundation/CancellationToken.h"
#include "Horo/Foundation/Result.h"
#include "Horo/Foundation/Time.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace Horo {
    using JobId = std::uint64_t;

    /** @brief Lifecycle state of an accepted job. */
    enum class JobState : std::uint8_t {
        Queued,
        Running,
        Succeeded,
        Failed,
        Cancelled,
    };

    /** @brief Snapshot of an accepted job's immutable identity and current state. */
    struct JobSnapshot {
        JobId id = 0;
        JobState state = JobState::Queued;
        std::optional<Error> error;
    };

    /** @brief Result-returning unit of scheduled work. */
    using JobFunction = std::function<Result<void>(const CancellationToken &)>;

    /** @brief Submission metadata retained by the job system. */
    struct JobDescriptor {
        CancellationToken parentCancellation; /**< Optional parent operation cancellation. */
    };

    /** @brief Fixed scheduling limits for one JobSystem instance. */
    struct JobSystemConfig {
        std::size_t workerCount = 1;
        std::size_t maxQueuedJobs = 1024;
    };

    /** @brief Defines how queued work is treated during shutdown. */
    enum class ShutdownPolicy : std::uint8_t {
        Drain,
        Cancel,
    };

    /** @brief Selects the caller-affinity rule for one bounded wait. */
    enum class WaitPolicy : std::uint8_t {
        WorkerOnly,
        MainThreadPumpAllowed,
        ForbiddenOnOwnerThread,
        OwnerThreadBlockAllowed,
    };

    /** @brief Explicit finite policy for a bounded job wait. */
    struct JoinOptions {
        WaitPolicy waitPolicy{WaitPolicy::WorkerOnly}; /**< Caller-affinity rule checked before observing completion. */
        Duration timeout{};                            /**< Maximum duration of this wait. */
    };

    struct JobRecord;

    /** @brief Move-only reference to a durable accepted job record. */
    class JobHandle {
    public:
        JobHandle() = default;
        JobHandle(const JobHandle &) = delete;
        JobHandle &operator=(const JobHandle &) = delete;
        JobHandle(JobHandle &&) noexcept = default;
        JobHandle &operator=(JobHandle &&) noexcept = default;

        /** @brief Waits until the job reaches its single terminal state. */
        [[nodiscard]] Result<void> Wait() const;
        /**
         * @brief Waits under a finite affinity policy, optionally helping only this exact queued record.
         * @param options Caller-affinity rule and maximum wait duration. `MainThreadPumpAllowed` and `WorkerOnly` may claim
         * this record for inline execution; `OwnerThreadBlockAllowed` waits without pumping. Unrelated queued jobs are never pumped.
         * @return Terminal job result, or a typed forbidden, deadlock-risk or timeout error. Policy validation occurs even
         * if the job is already terminal. A timeout does not change the job lifecycle, so the handle remains safely retryable.
         */
        [[nodiscard]] Result<void> Wait(const JoinOptions &options) const;
        /** @brief Returns the stable identifier assigned at successful submission. */
        [[nodiscard]] JobId Id() const noexcept;

    private:
        friend class JobSystem;
        friend class TaskGroup;

        explicit JobHandle(std::shared_ptr<JobRecord> record) : m_record(std::move(record)) {}

        std::shared_ptr<JobRecord> m_record;
    };

    /** @brief Bounded worker queue with owned, joinable worker threads. */
    class JobSystem {
    public:
        explicit JobSystem(JobSystemConfig config = {});
        ~JobSystem();
        JobSystem(const JobSystem &) = delete;
        JobSystem &operator=(const JobSystem &) = delete;
        JobSystem(JobSystem &&) = delete;
        JobSystem &operator=(JobSystem &&) = delete;

        /** @brief Queues work or returns a typed overload error without creating a job record. */
        [[nodiscard]] Result<JobHandle> Submit(JobDescriptor descriptor, std::function<void(const CancellationToken &)> work) const;
        /**
         * @brief Queues result-returning work without translating typed failures into exceptions.
         * @param descriptor Submission metadata including optional parent cancellation.
         * @param work Owned callback executed by one worker.
         * @return Move-only accepted-job handle or a typed admission failure.
         */
        [[nodiscard]] Result<JobHandle> SubmitResult(JobDescriptor descriptor, JobFunction work) const;
        /** @brief Requests cooperative cancellation; queued work becomes terminal immediately. */
        [[nodiscard]] Result<void> RequestCancel(JobId id) const;
        /** @brief Returns the latest state for an accepted job. */
        [[nodiscard]] JobSnapshot Query(JobId id) const;
        /** @brief Returns the immutable worker count configured for this scheduler. */
        [[nodiscard]] std::size_t WorkerCount() const noexcept;
        /** @brief Stops submissions, then drains or cooperatively cancels work and joins all workers. */
        void Shutdown(ShutdownPolicy policy) const;

    private:
        struct State;
        static void RunWorker(const std::shared_ptr<State> &state);
        std::shared_ptr<State> m_state;
    };

    /** @brief Failure propagation policy for one structured child-work scope. */
    enum class TaskGroupFailurePolicy : std::uint8_t {
        FailFast,
        CollectAll,
    };

    /** @brief Operation-owned structured concurrency scope over an injected JobSystem. */
    class TaskGroup {
    public:
        /**
         * @brief Creates a child-work scope.
         * @param jobs Job system that outlives this group.
         * @param policy Child failure propagation policy.
         * @param parentCancellation Optional parent operation cancellation.
         */
        explicit TaskGroup(JobSystem &jobs, TaskGroupFailurePolicy policy = TaskGroupFailurePolicy::FailFast,
                           CancellationToken parentCancellation = {});
        /** @brief Cancels and joins every accepted child before releasing the scope. */
        ~TaskGroup();
        TaskGroup(const TaskGroup &) = delete;
        TaskGroup &operator=(const TaskGroup &) = delete;
        TaskGroup(TaskGroup &&) = delete;
        TaskGroup &operator=(TaskGroup &&) = delete;

        /**
         * @brief Admits one result-returning child job while the group is open.
         * @param descriptor Child submission metadata; its parent cancellation is replaced by the group token.
         * @param work Owned child callback.
         * @return Accepted child identifier or a typed admission failure.
         */
        [[nodiscard]] Result<JobId> Spawn(JobDescriptor descriptor, JobFunction work) const;
        /** @brief Closes admission and requests cooperative cancellation for all accepted children. */
        void RequestCancel() const;
        /**
         * @brief Closes admission and joins every accepted child.
         * @return Success or the first child error in deterministic spawn order. Repeated calls return the same result.
         */
        [[nodiscard]] Result<void> Join() const;
        /**
         * @brief Closes admission and joins every accepted child under one finite deadline.
         * @param options Caller-affinity rule and maximum duration shared by the complete join.
         * @return Success, the first child error in spawn order, or a typed wait-control error. A wait-control error
         * leaves the closed group retryable so its owner can cancel and drain it safely.
         */
        [[nodiscard]] Result<void> Join(const JoinOptions &options) const;

    private:
        struct State;

        struct ChildJoinOutcome {
            std::optional<Error> firstError;
            std::optional<Error> interruption;
        };

        static void CancelChildren(const std::shared_ptr<State> &state);
        static ChildJoinOutcome WaitForChildren(const std::vector<std::shared_ptr<JobRecord>> &children, const JoinOptions *options);
        [[nodiscard]] Result<void> JoinImpl(const JoinOptions *options) const;
        std::shared_ptr<State> m_state;
    };
}  // namespace Horo
