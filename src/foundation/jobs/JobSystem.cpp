#include "Horo/Foundation/JobSystem.h"

#include <condition_variable>
#include <deque>
#include <exception>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Horo
{
    namespace
    {
        [[nodiscard]] Error MakeJobError(const char *code, const char *message)
        {
            return Error{
                .code = ErrorCode{code},
                .domain = ErrorDomainId{"horo.foundation.jobs"},
                .severity = ErrorSeverity::Error,
                .message = message,
            };
        }

        [[nodiscard]] bool IsTerminal(const JobState state) noexcept
        {
            return state == JobState::Succeeded || state == JobState::Failed || state == JobState::Cancelled;
        }
    } // namespace

    struct JobRecord
    {
        JobRecord(const JobId jobId, const JobDescriptor &, std::function<void(const CancellationToken &)> jobWork)
            : id(jobId), work(std::move(jobWork)) {}

        [[nodiscard]] std::mutex& Mutex() const noexcept { return mutex; }

        private:
        mutable std::mutex mutex;

        public:
            JobId id;
            std::condition_variable completed;
        JobState state = JobState::Queued;
        std::optional<Error> error;
        CancellationSource cancellation;
        std::function<void(const CancellationToken &)> work;
    };

    struct JobSystem::State
    {
        explicit State(const JobSystemConfig value) : config(value) {}

        JobSystemConfig config;
        std::mutex mutex;
        std::condition_variable workAvailable;
        bool accepting = true;
        bool stopping = false;
        JobId nextId = 1;
        std::deque<std::shared_ptr<JobRecord>> queue;
        std::unordered_map<JobId, std::shared_ptr<JobRecord>> jobs;
        std::vector<std::thread> workers;
        std::mutex shutdownMutex;
    };

    namespace
    {
        void SetTerminalState(const std::shared_ptr<JobRecord> &record, const JobState state, std::optional<Error> error = std::nullopt)
        {
            std::lock_guard lock(record->Mutex());
            if (IsTerminal(record->state)) return;
            record->state = state;
            record->error = std::move(error);
            record->completed.notify_all();
        }
    } // namespace

    void JobSystem::RunWorker(const std::shared_ptr<State> &state)
    {
        for (;;)
        {
            std::shared_ptr<JobRecord> record;
            {
                std::unique_lock lock(state->mutex);
                state->workAvailable.wait(lock, [&state] { return state->stopping || !state->queue.empty(); });
                if (state->queue.empty())
                {
                    if (state->stopping) return;
                    continue;
                }
                record = std::move(state->queue.front());
                state->queue.pop_front();
            }

            {
                std::lock_guard lock(record->Mutex());
                if (record->state != JobState::Queued) continue;
                record->state = JobState::Running;
            }

            try
            {
                record->work(record->cancellation.Token());
                if (record->cancellation.Token().IsCancellationRequested())
                    SetTerminalState(record, JobState::Cancelled, MakeJobError("job.cancelled", "Job cancellation was requested."));
                else
                    SetTerminalState(record, JobState::Succeeded);
            }
            catch (const std::exception &exception)
            {
                SetTerminalState(record, JobState::Failed, MakeJobError("job.failed", exception.what()));
            }
            catch (...)
            {
                SetTerminalState(record, JobState::Failed, MakeJobError("job.failed", "Job callback threw an unknown exception."));
            }
        }
    }

    JobSystem::JobSystem(const JobSystemConfig config) : m_state(std::make_shared<State>(config))
    {
        for (std::size_t index = 0; index < config.workerCount; ++index)
            m_state->workers.emplace_back([state = m_state] { RunWorker(state); });
    }

    JobSystem::~JobSystem()
    {
        Shutdown(ShutdownPolicy::Cancel);
    }

    Result<JobHandle> JobSystem::Submit(JobDescriptor descriptor, std::function<void(const CancellationToken &)> work)
    {
        std::lock_guard lock(m_state->mutex);
        if (!m_state->accepting)
            return Result<JobHandle>::Failure(MakeJobError("job.shutdown", "Job system is no longer accepting work."));
        if (m_state->queue.size() >= m_state->config.maxQueuedJobs)
            return Result<JobHandle>::Failure(MakeJobError("job.queue_full", "Job queue is at capacity."));

        auto record = std::make_shared<JobRecord>(m_state->nextId++, descriptor, std::move(work));
        m_state->jobs.try_emplace(record->id, record);
        m_state->queue.push_back(record);
        m_state->workAvailable.notify_one();
        return Result<JobHandle>::Success(JobHandle(std::move(record)));
    }

    Result<void> JobSystem::RequestCancel(const JobId id)
    {
        std::shared_ptr<JobRecord> record;
        {
            std::lock_guard lock(m_state->mutex);
            const auto found = m_state->jobs.find(id);
            if (found == m_state->jobs.end())
                return Result<void>::Failure(MakeJobError("job.not_found", "Job identifier is not known by this job system."));
            record = found->second;
        }
        record->cancellation.RequestCancellation();
        {
            std::lock_guard lock(record->Mutex());
            if (record->state == JobState::Queued)
            {
                record->state = JobState::Cancelled;
                record->error = MakeJobError("job.cancelled", "Job was cancelled before execution.");
                record->completed.notify_all();
            }
        }
        return Result<void>::Success();
    }

    JobSnapshot JobSystem::Query(const JobId id) const
    {
        std::shared_ptr<JobRecord> record;
        {
            std::lock_guard lock(m_state->mutex);
            const auto found = m_state->jobs.find(id);
            if (found == m_state->jobs.end()) return JobSnapshot{.id = id};
            record = found->second;
        }

        std::lock_guard lock(record->Mutex());
        return JobSnapshot{.id = record->id, .state = record->state, .error = record->error};
    }

    void JobSystem::Shutdown(const ShutdownPolicy policy)
    {
        std::lock_guard shutdownLock(m_state->shutdownMutex);
        {
            std::lock_guard lock(m_state->mutex);
            m_state->accepting = false;
            m_state->stopping = true;
            if (policy == ShutdownPolicy::Cancel)
            {
                for (const auto &record : m_state->queue)
                {
                    record->cancellation.RequestCancellation();
                    SetTerminalState(record, JobState::Cancelled, MakeJobError("job.cancelled", "Job was cancelled during shutdown."));
                }
                m_state->queue.clear();
                for (const auto &[id, record] : m_state->jobs)
                {
                    (void)id;
                    record->cancellation.RequestCancellation();
                }
            }
        }
        m_state->workAvailable.notify_all();
        for (auto &worker : m_state->workers)
            if (worker.joinable()) worker.join();
    }

    Result<void> JobHandle::Wait() const
    {
        if (!m_record)
            return Result<void>::Failure(MakeJobError("job.invalid_handle", "Cannot wait on an invalid job handle."));

        const auto record = m_record;
        std::unique_lock lock(record->Mutex());
        record->completed.wait(lock, [record] { return IsTerminal(record->state); });
        if (record->state == JobState::Succeeded) return Result<void>::Success();
        return Result<void>::Failure(*record->error);
    }

    JobId JobHandle::Id() const noexcept
    {
        return m_record ? m_record->id : 0;
    }
} // namespace Horo
