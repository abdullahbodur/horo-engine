#include "Horo/Foundation/JobSystem.h"

#include "../FoundationErrors.h"
#include "Horo/Foundation/Telemetry/Operation.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <exception>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Horo {
    namespace {
        [[nodiscard]] Error MakeJobError(const ErrorCodeDescriptor &descriptor, const char *message) {
            return MakeError(descriptor, message);
        }

        [[nodiscard]] bool IsTerminal(const JobState state) noexcept {
            using enum JobState;
            return state == Succeeded || state == Failed || state == Cancelled;
        }
    }  // namespace

    struct JobRecord {
        JobRecord(const JobId jobId, const JobDescriptor &descriptor, JobFunction jobWork, const void *scheduler)
            : id(jobId), cancellation(descriptor.parentCancellation), work(std::move(jobWork)), schedulerIdentity(scheduler),
              submittingThread(std::this_thread::get_id()) {}

        [[nodiscard]] std::mutex &Mutex() const noexcept {
            return mutex_;
        }

        JobId id;
        std::condition_variable completed;
        JobState state = JobState::Queued;
        std::optional<Error> error;
        CancellationSource cancellation;
        JobFunction work;
        Telemetry::OperationContext operationContext = Telemetry::CaptureOperationContext();
        const void *schedulerIdentity{};
        std::thread::id submittingThread;
        mutable std::mutex mutex_;  // NOSONAR(cpp:S8379) Controlled via Mutex() accessor.
    };

    struct JobSystem::State {
        explicit State(const JobSystemConfig value) : config(value) {}

        JobSystemConfig config;
        std::mutex mutex;
        std::condition_variable workAvailable;
        bool accepting = true;
        bool stopping = false;
        JobId nextId = 1;
        std::deque<std::shared_ptr<JobRecord>> queue;
        std::unordered_map<JobId, std::shared_ptr<JobRecord>> jobs;
        std::vector<std::thread> workers;  // NOSONAR(cpp:S6168) std::jthread not supported by AppleClang libc++ without experimental flags

        std::mutex shutdownMutex;
    };

    namespace {
        thread_local const void *activeSchedulerIdentity{};

        void SetTerminalState(const std::shared_ptr<JobRecord> &record, const JobState state, std::optional<Error> error = std::nullopt) {
            std::lock_guard lock(record->Mutex());
            if (IsTerminal(record->state))
                return;
            record->state = state;
            record->error = std::move(error);
            record->work = {};
            record->completed.notify_all();
        }

        void ExecuteJobRecord(const std::shared_ptr<JobRecord> &record) {
            const Telemetry::ScopedOperationContext operationContext{record->operationContext};
            try {
                JobFunction work;
                {
                    std::lock_guard lock(record->Mutex());
                    work = std::move(record->work);
                }
                Result<void> outcome = work(record->cancellation.Token());
                work = {};
                if (outcome.HasError()) {
                    const bool cancelled = record->cancellation.Token().IsCancellationRequested() &&
                                           outcome.ErrorValue().code.Value() == JobErrors::Cancelled.code.Value();
                    SetTerminalState(record, cancelled ? JobState::Cancelled : JobState::Failed, outcome.ErrorValue());
                } else if (record->cancellation.Token().IsCancellationRequested())
                    SetTerminalState(record, JobState::Cancelled, MakeJobError(JobErrors::Cancelled, "Job cancellation was requested."));
                else
                    SetTerminalState(record, JobState::Succeeded);
            } catch (const std::runtime_error &exception) {  // NOSONAR(cpp:S1181)
                SetTerminalState(record, JobState::Failed, MakeJobError(JobErrors::Failed, exception.what()));
            } catch (const std::logic_error &exception) {  // NOSONAR(cpp:S1181)
                SetTerminalState(record, JobState::Failed, MakeJobError(JobErrors::Failed, exception.what()));
            } catch (const std::bad_alloc &exception) {  // NOSONAR(cpp:S1181)
                SetTerminalState(record, JobState::Failed, MakeJobError(JobErrors::Failed, exception.what()));
            } catch (const std::exception &exception) {  // NOSONAR(cpp:S1181)
                SetTerminalState(record, JobState::Failed, MakeJobError(JobErrors::Failed, exception.what()));
            } catch (...) {  // NOSONAR(cpp:S1181)
                SetTerminalState(record, JobState::Failed, MakeJobError(JobErrors::Failed, "Job callback threw an unknown exception."));
            }
        }

        [[nodiscard]] Result<void> ValidateBoundedWait(const JobRecord &record, const WaitPolicy policy) {
            switch (policy) {
                case WaitPolicy::MainThreadPumpAllowed:
                    return Result<void>::Success();
                case WaitPolicy::WorkerOnly:
                    if (activeSchedulerIdentity == record.schedulerIdentity)
                        return Result<void>::Success();
                    return Result<void>::Failure(
                        MakeJobError(JobErrors::WaitForbidden, "WorkerOnly requires a worker executing on the owning job system."));
                case WaitPolicy::ForbiddenOnOwnerThread:
                    if (record.submittingThread != std::this_thread::get_id())
                        return Result<void>::Success();
                    return Result<void>::Failure(
                        MakeJobError(JobErrors::WaitForbidden, "The submitting owner thread is forbidden from waiting for this job."));
            }
            return Result<void>::Failure(MakeJobError(JobErrors::WaitForbidden, "The bounded wait policy is not recognized."));
        }

        [[nodiscard]] Result<void> TerminalResult(const JobRecord &record) {
            return record.state == JobState::Succeeded
                       ? Result<void>::Success()
                       : Result<void>::Failure(record.error.value_or(
                             MakeJobError(JobErrors::Failed, "Terminal job state did not retain its required error payload.")));
        }

        [[nodiscard]] Result<void> WaitBounded(const std::shared_ptr<JobRecord> &record, const JoinOptions &options) {
            if (const Result<void> validated = ValidateBoundedWait(*record, options.waitPolicy); validated.HasError())
                return validated;

            const auto timeout = std::chrono::nanoseconds(std::max<std::int64_t>(0, options.timeout.ToNanoseconds()));
            std::unique_lock lock(record->Mutex());
            if (!record->completed.wait_for(lock, timeout, [record] {
                return IsTerminal(record->state);
            }))
                return Result<void>::Failure(MakeJobError(JobErrors::WaitTimedOut, "The job did not complete before its wait deadline."));
            return TerminalResult(*record);
        }
    }  // namespace

    void JobSystem::RunWorker(const std::shared_ptr<State> &state) {
        for (;;) {
            std::shared_ptr<JobRecord> record;
            {
                std::unique_lock lock(state->mutex);
                state->workAvailable.wait(lock, [&state] {
                    return state->stopping || !state->queue.empty();
                });
                if (state->queue.empty()) {
                    if (state->stopping)
                        return;
                    continue;
                }
                record = std::move(state->queue.front());
                state->queue.pop_front();
            }

            {
                std::lock_guard lock(record->Mutex());
                if (record->state != JobState::Queued)
                    continue;
                record->state = JobState::Running;
            }

            ExecuteJobRecord(record);
        }
    }

    JobSystem::JobSystem(const JobSystemConfig config) : m_state(std::make_shared<State>(config)) {
        for (std::size_t index = 0; index < config.workerCount; ++index)
            m_state->workers.emplace_back([state = m_state] {
                activeSchedulerIdentity = state.get();
                RunWorker(state);
                activeSchedulerIdentity = nullptr;
            });
    }

    JobSystem::~JobSystem() {
        Shutdown(ShutdownPolicy::Cancel);
    }

    Result<JobHandle> JobSystem::Submit(JobDescriptor descriptor, std::function<void(const CancellationToken &)> work) const {
        return SubmitResult(std::move(descriptor), [work = std::move(work)](const CancellationToken &cancellation) {
            work(cancellation);
            return Result<void>::Success();
        });
    }

    Result<JobHandle> JobSystem::SubmitResult(JobDescriptor descriptor, JobFunction work) const {
        std::lock_guard lock(m_state->mutex);
        if (!m_state->accepting)
            return Result<JobHandle>::Failure(MakeJobError(JobErrors::Shutdown, "Job system is no longer accepting work."));
        if (m_state->queue.size() >= m_state->config.maxQueuedJobs)
            return Result<JobHandle>::Failure(MakeJobError(JobErrors::QueueFull, "Job queue is at capacity."));

        auto record = std::make_shared<JobRecord>(m_state->nextId++, descriptor, std::move(work), m_state.get());
        m_state->jobs.try_emplace(record->id, record);
        m_state->queue.push_back(record);
        m_state->workAvailable.notify_one();
        return Result<JobHandle>::Success(JobHandle(std::move(record)));
    }

    Result<void> JobSystem::RequestCancel(const JobId id) const {
        std::shared_ptr<JobRecord> record;
        {
            std::lock_guard lock(m_state->mutex);
            const auto found = m_state->jobs.find(id);
            if (found == m_state->jobs.end())
                return Result<void>::Failure(MakeJobError(JobErrors::NotFound, "Job identifier is not known by this job system."));
            record = found->second;
        }
        record->cancellation.RequestCancellation();
        {
            std::lock_guard lock(record->Mutex());
            if (record->state == JobState::Queued) {
                record->state = JobState::Cancelled;
                record->error = MakeJobError(JobErrors::Cancelled, "Job was cancelled before execution.");
                record->work = {};
                record->completed.notify_all();
            }
        }
        return Result<void>::Success();
    }

    JobSnapshot JobSystem::Query(const JobId id) const {
        std::shared_ptr<JobRecord> record;
        {
            std::lock_guard lock(m_state->mutex);
            const auto found = m_state->jobs.find(id);
            if (found == m_state->jobs.end())
                return JobSnapshot{.id = id};
            record = found->second;
        }

        std::lock_guard lock(record->Mutex());
        return JobSnapshot{.id = record->id, .state = record->state, .error = record->error};
    }

    /** @copydoc JobSystem::WorkerCount */
    std::size_t JobSystem::WorkerCount() const noexcept {
        return m_state->config.workerCount;
    }

    void JobSystem::Shutdown(const ShutdownPolicy policy) const {
        std::lock_guard shutdownLock(m_state->shutdownMutex);
        {
            std::lock_guard lock(m_state->mutex);
            m_state->accepting = false;
            m_state->stopping = true;
            if (policy == ShutdownPolicy::Cancel) {
                for (const auto &record : m_state->queue) {
                    record->cancellation.RequestCancellation();
                    SetTerminalState(record, JobState::Cancelled, MakeJobError(JobErrors::Cancelled, "Job was cancelled during shutdown."));
                }
                m_state->queue.clear();
                for (const auto &[id, record] : m_state->jobs) {
                    (void)id;
                    record->cancellation.RequestCancellation();
                }
            }
        }
        m_state->workAvailable.notify_all();
        std::ranges::for_each(m_state->workers, [](std::thread &worker) {  // NOSONAR(cpp:S6168) std::jthread not supported by AppleClang
                                                                           // libc++ without experimental flags
            if (worker.joinable()) {
                worker.join();
            }
        });
    }

    Result<void> JobHandle::Wait() const {
        if (!m_record)
            return Result<void>::Failure(MakeJobError(JobErrors::InvalidHandle, "Cannot wait on an invalid job handle."));

        const auto record = m_record;
        std::unique_lock lock(record->Mutex());
        record->completed.wait(lock, [record] {
            return IsTerminal(record->state);
        });
        return TerminalResult(*record);
    }

    /** @copydoc JobHandle::Wait(const JoinOptions &) */
    Result<void> JobHandle::Wait(const JoinOptions &options) const {
        if (!m_record)
            return Result<void>::Failure(MakeJobError(JobErrors::InvalidHandle, "Cannot wait on an invalid job handle."));
        return WaitBounded(m_record, options);
    }

    JobId JobHandle::Id() const noexcept {
        return m_record ? m_record->id : 0;
    }

    struct TaskGroup::State {
        State(JobSystem &jobSystem, const TaskGroupFailurePolicy failurePolicy, const CancellationToken &parentCancellation)
            : jobs(jobSystem), policy(failurePolicy), cancellation(parentCancellation) {}

        JobSystem &jobs;
        TaskGroupFailurePolicy policy;
        CancellationSource cancellation;
        std::mutex mutex;
        std::mutex joinMutex;
        bool accepting = true;
        bool joined = false;
        std::optional<Error> joinError;
        std::vector<std::shared_ptr<JobHandle>> children;
    };

    void TaskGroup::CancelChildren(const std::shared_ptr<State> &state) {
        std::vector<JobId> childIds;
        {
            std::lock_guard lock(state->mutex);
            state->accepting = false;
            state->cancellation.RequestCancellation();
            childIds.reserve(state->children.size());
            for (const auto &child : state->children)
                childIds.push_back(child->Id());
        }
        for (const JobId childId : childIds)
            static_cast<void>(state->jobs.RequestCancel(childId));
    }

    TaskGroup::TaskGroup(JobSystem &jobs, const TaskGroupFailurePolicy policy, const CancellationToken parentCancellation)
        : m_state(std::make_shared<State>(jobs, policy, parentCancellation)) {}

    TaskGroup::~TaskGroup() {
        try {
            RequestCancel();
            static_cast<void>(Join());
        } catch (...) {  // NOSONAR(cpp:S2486) A destructor cannot report or propagate cleanup failures.
            // Destructors must swallow any unexpected exceptions per noexcept contract
        }
    }

    Result<JobId> TaskGroup::Spawn(JobDescriptor descriptor, JobFunction work) const {
        std::lock_guard lock(m_state->mutex);
        if (!m_state->accepting)
            return Result<JobId>::Failure(MakeJobError(JobErrors::TaskGroupClosed, "Task group admission is closed."));

        descriptor.parentCancellation = m_state->cancellation.Token();
        const std::weak_ptr weakState = m_state;
        Result<JobHandle> submitted =
            m_state->jobs.SubmitResult(std::move(descriptor), [weakState, work = std::move(work)](const CancellationToken &cancellation) {
            if (cancellation.IsCancellationRequested())
                return Result<void>::Failure(MakeJobError(JobErrors::Cancelled, "Task group child was cancelled before execution."));
            Result<void> outcome = work(cancellation);
            if (outcome.HasError()) {
                if (const auto state = weakState.lock(); state && state->policy == TaskGroupFailurePolicy::FailFast)
                    CancelChildren(state);
            }
            return outcome;
        });
        if (submitted.HasError())
            return Result<JobId>::Failure(submitted.ErrorValue());

        auto child = std::make_shared<JobHandle>(std::move(submitted).Value());
        const JobId childId = child->Id();
        m_state->children.push_back(std::move(child));
        return Result<JobId>::Success(childId);
    }

    void TaskGroup::RequestCancel() const {
        CancelChildren(m_state);
    }

    Result<void> TaskGroup::Join() const {
        std::lock_guard joinLock(m_state->joinMutex);
        if (m_state->cancellation.Token().IsCancellationRequested())
            CancelChildren(m_state);

        std::vector<std::shared_ptr<JobHandle>> children;
        {
            std::lock_guard lock(m_state->mutex);
            if (m_state->joined)
                return m_state->joinError.has_value() ? Result<void>::Failure(*m_state->joinError) : Result<void>::Success();
            m_state->accepting = false;
            children = m_state->children;
        }

        std::optional<Error> firstError;
        for (const auto &child : children) {
            const Result<void> waited = child->Wait();
            if (waited.HasError() && !firstError.has_value())
                firstError = waited.ErrorValue();
        }
        {
            std::lock_guard lock(m_state->mutex);
            m_state->joined = true;
            m_state->joinError = firstError;
        }
        return firstError.has_value() ? Result<void>::Failure(*firstError) : Result<void>::Success();
    }
}  // namespace Horo
