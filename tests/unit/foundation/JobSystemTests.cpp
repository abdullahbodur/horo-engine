#include <catch2/catch_test_macros.hpp>

#include "Horo/Foundation/JobSystem.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

namespace
{
const Horo::ErrorCodeDescriptor TestFailure{
    .domain = Horo::ErrorDomainId("test.job_system"),
    .code = Horo::ErrorCode("test.job_system.child_failed"),
    .defaultSeverity = Horo::ErrorSeverity::Error,
    .summary = "Test child failed.",
    .remediationHint = "Inspect the test.",
};

TEST_CASE("Submitted Job Reaches Succeeded Terminal State", "[unit][foundation]")
{
    Horo::JobSystem jobs{Horo::JobSystemConfig{.workerCount = 1, .maxQueuedJobs = 4}};
    std::atomic executed{false};
    const auto submitted =
        jobs.Submit(Horo::JobDescriptor{}, [&executed](const Horo::CancellationToken &) { executed.store(true); });
    REQUIRE((submitted.HasValue()));
    REQUIRE((submitted.Value().Wait().HasValue()));

    const Horo::JobSnapshot snapshot = jobs.Query(submitted.Value().Id());
    REQUIRE((executed.load()));
    REQUIRE((snapshot.state == Horo::JobState::Succeeded));
    jobs.Shutdown(Horo::ShutdownPolicy::Drain);
}

TEST_CASE("Bounded Queue Rejects Overflow", "[unit][foundation]")
{
    Horo::JobSystem jobs{Horo::JobSystemConfig{.workerCount = 0, .maxQueuedJobs = 1}};
    REQUIRE((jobs.Submit(Horo::JobDescriptor{}, [](const Horo::CancellationToken &) {}).HasValue()));
    const auto rejected = jobs.Submit(Horo::JobDescriptor{}, [](const Horo::CancellationToken &) {});
    REQUIRE((rejected.HasError()));
    REQUIRE((rejected.ErrorValue().code.Value() == "job.queue_full"));
    jobs.Shutdown(Horo::ShutdownPolicy::Cancel);
}

TEST_CASE("Queued Job Can Be Cancelled By Id", "[unit][foundation]")
{
    Horo::JobSystem jobs{Horo::JobSystemConfig{.workerCount = 0, .maxQueuedJobs = 1}};
    const auto submitted = jobs.Submit(Horo::JobDescriptor{}, [](const Horo::CancellationToken &) {});
    REQUIRE((submitted.HasValue()));
    REQUIRE((jobs.RequestCancel(submitted.Value().Id()).HasValue()));
    REQUIRE((jobs.Query(submitted.Value().Id()).state == Horo::JobState::Cancelled));
    REQUIRE((submitted.Value().Wait().HasError()));
    jobs.Shutdown(Horo::ShutdownPolicy::Cancel);
}

TEST_CASE("Result Returning Job Preserves Typed Failure", "[unit][foundation]")
{
    Horo::JobSystem jobs{Horo::JobSystemConfig{.workerCount = 1, .maxQueuedJobs = 4}};
    const auto submitted = jobs.SubmitResult(Horo::JobDescriptor{}, [](const Horo::CancellationToken &) {
        return Horo::Result<void>::Failure(Horo::MakeError(TestFailure));
    });
    REQUIRE((submitted.HasValue()));
    const auto waited = submitted.Value().Wait();
    REQUIRE((waited.HasError()));
    REQUIRE((waited.ErrorValue().code.Value() == "test.job_system.child_failed"));
    REQUIRE((jobs.Query(submitted.Value().Id()).state == Horo::JobState::Failed));
    jobs.Shutdown(Horo::ShutdownPolicy::Drain);
}

TEST_CASE("Task Group Collect All Returns Failures In Spawn Order", "[unit][foundation]")
{
    Horo::JobSystem jobs{Horo::JobSystemConfig{.workerCount = 2, .maxQueuedJobs = 8}};
    Horo::TaskGroup group(jobs, Horo::TaskGroupFailurePolicy::CollectAll);
    std::atomic completed{0};

    const auto first = group.Spawn({}, [&completed](const Horo::CancellationToken &) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        completed.fetch_add(1);
        Horo::Error error = Horo::MakeError(TestFailure);
        error.code = Horo::ErrorCode("test.job_system.first");
        return Horo::Result<void>::Failure(std::move(error));
    });
    const auto second = group.Spawn({}, [&completed](const Horo::CancellationToken &) {
        completed.fetch_add(1);
        Horo::Error error = Horo::MakeError(TestFailure);
        error.code = Horo::ErrorCode("test.job_system.second");
        return Horo::Result<void>::Failure(std::move(error));
    });
    REQUIRE((first.HasValue()));
    REQUIRE((second.HasValue()));

    const auto joined = group.Join();
    REQUIRE((joined.HasError()));
    REQUIRE((joined.ErrorValue().code.Value() == "test.job_system.first"));
    REQUIRE((completed.load() == 2));
    const auto joinedAgain = group.Join();
    REQUIRE((joinedAgain.HasError()));
    REQUIRE((joinedAgain.ErrorValue().code.Value() == "test.job_system.first"));
    REQUIRE((group.Spawn({}, [](const Horo::CancellationToken &) { return Horo::Result<void>::Success(); })
                 .ErrorValue()
                 .code.Value() == "job.task_group_closed"));
    jobs.Shutdown(Horo::ShutdownPolicy::Drain);
}

TEST_CASE("Task Group Fail Fast Cancels Accepted Siblings", "[unit][foundation]")
{
    Horo::JobSystem jobs{Horo::JobSystemConfig{.workerCount = 2, .maxQueuedJobs = 8}};
    Horo::TaskGroup group(jobs, Horo::TaskGroupFailurePolicy::FailFast);
    std::mutex mutex;
    std::condition_variable started;
    bool siblingStarted = false;
    std::atomic siblingObservedCancellation{false};

    REQUIRE((group
                 .Spawn({},
                        [&](const Horo::CancellationToken &cancellation) {
                            {
                                std::lock_guard lock(mutex);
                                siblingStarted = true;
                            }
                            started.notify_one();
                            while (!cancellation.IsCancellationRequested())
                                std::this_thread::yield();
                            siblingObservedCancellation.store(true);
                            return Horo::Result<void>::Failure(Horo::MakeError(TestFailure));
                        })
                 .HasValue()));
    REQUIRE((group
                 .Spawn({},
                        [&](const Horo::CancellationToken &) {
                            std::unique_lock lock(mutex);
                            started.wait(lock, [&] { return siblingStarted; });
                            return Horo::Result<void>::Failure(Horo::MakeError(TestFailure));
                        })
                 .HasValue()));

    REQUIRE((group.Join().HasError()));
    REQUIRE((siblingObservedCancellation.load()));
    jobs.Shutdown(Horo::ShutdownPolicy::Drain);
}

TEST_CASE("Rejected Task Group Child Is Not Joined", "[unit][foundation]")
{
    Horo::JobSystem jobs{Horo::JobSystemConfig{.workerCount = 0, .maxQueuedJobs = 1}};
    Horo::TaskGroup group(jobs, Horo::TaskGroupFailurePolicy::CollectAll);
    REQUIRE(
        (group.Spawn({}, [](const Horo::CancellationToken &) { return Horo::Result<void>::Success(); }).HasValue()));
    const auto rejected =
        group.Spawn({}, [](const Horo::CancellationToken &) { return Horo::Result<void>::Success(); });
    REQUIRE((rejected.HasError()));
    REQUIRE((rejected.ErrorValue().code.Value() == "job.queue_full"));
    group.RequestCancel();
    REQUIRE((group.Join().HasError()));
    jobs.Shutdown(Horo::ShutdownPolicy::Cancel);
}

TEST_CASE("Parent Cancellation Flows To Task Group Children", "[unit][foundation]")
{
    Horo::JobSystem jobs{Horo::JobSystemConfig{.workerCount = 1, .maxQueuedJobs = 4}};
    Horo::CancellationSource parent;
    parent.RequestCancellation();
    Horo::TaskGroup group(jobs, Horo::TaskGroupFailurePolicy::FailFast, parent.Token());
    std::atomic executed{false};
    REQUIRE((group
                 .Spawn({},
                        [&executed](const Horo::CancellationToken &) {
                            executed.store(true);
                            return Horo::Result<void>::Success();
                        })
                 .HasValue()));
    REQUIRE((group.Join().HasError()));
    REQUIRE((!executed.load()));
    jobs.Shutdown(Horo::ShutdownPolicy::Drain);
}

TEST_CASE("Destructor Cancels And Joins Children", "[unit][foundation]")
{
    Horo::JobSystem jobs{Horo::JobSystemConfig{.workerCount = 1, .maxQueuedJobs = 4}};
    std::atomic started{false};
    std::atomic stopped{false};
    {
        Horo::TaskGroup group(jobs);
        REQUIRE((group
                     .Spawn({},
                            [&started, &stopped](const Horo::CancellationToken &cancellation) {
                                started.store(true);
                                while (!cancellation.IsCancellationRequested())
                                    std::this_thread::yield();
                                stopped.store(true);
                                return Horo::Result<void>::Success();
                            })
                     .HasValue()));
        while (!started.load())
            std::this_thread::yield();
    }
    REQUIRE((stopped.load()));
    jobs.Shutdown(Horo::ShutdownPolicy::Drain);
}
} // namespace
