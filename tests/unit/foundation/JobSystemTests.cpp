#include "Horo/Foundation/JobSystem.h"

#include <atomic>
#include <cassert>

namespace
{

void SubmittedJobReachesSucceededTerminalState()
{
    Horo::JobSystem jobs{Horo::JobSystemConfig{.workerCount = 1, .maxQueuedJobs = 4}};
    std::atomic<bool> executed{false};
    const auto submitted = jobs.Submit(Horo::JobDescriptor{}, [&executed](const Horo::CancellationToken &) { executed.store(true); });
    assert(submitted.HasValue());
    assert(submitted.Value().Wait().HasValue());

    const Horo::JobSnapshot snapshot = jobs.Query(submitted.Value().Id());
    assert(executed.load());
    assert(snapshot.state == Horo::JobState::Succeeded);
    jobs.Shutdown(Horo::ShutdownPolicy::Drain);
}

void BoundedQueueRejectsOverflow()
{
    Horo::JobSystem jobs{Horo::JobSystemConfig{.workerCount = 0, .maxQueuedJobs = 1}};
    assert(jobs.Submit(Horo::JobDescriptor{}, [](const Horo::CancellationToken &) {}).HasValue());
    const auto rejected = jobs.Submit(Horo::JobDescriptor{}, [](const Horo::CancellationToken &) {});
    assert(rejected.HasError());
    assert(rejected.ErrorValue().code.Value() == "job.queue_full");
    jobs.Shutdown(Horo::ShutdownPolicy::Cancel);
}

void QueuedJobCanBeCancelledById()
{
    Horo::JobSystem jobs{Horo::JobSystemConfig{.workerCount = 0, .maxQueuedJobs = 1}};
    const auto submitted = jobs.Submit(Horo::JobDescriptor{}, [](const Horo::CancellationToken &) {});
    assert(submitted.HasValue());
    assert(jobs.RequestCancel(submitted.Value().Id()).HasValue());
    assert(jobs.Query(submitted.Value().Id()).state == Horo::JobState::Cancelled);
    assert(submitted.Value().Wait().HasError());
    jobs.Shutdown(Horo::ShutdownPolicy::Cancel);
}

} // namespace

int main()
{
    SubmittedJobReachesSucceededTerminalState();
    BoundedQueueRejectsOverflow();
    QueuedJobCanBeCancelledById();
    return 0;
}
