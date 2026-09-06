#include "Horo/Foundation/JobSystem.h"

#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

namespace {
    const Horo::ErrorCodeDescriptor TestFailure{
        .domain = Horo::ErrorDomainId("test.job_system"),
        .code = Horo::ErrorCode("test.job_system.child_failed"),
        .defaultSeverity = Horo::ErrorSeverity::Error,
        .summary = "Test child failed.",
        .remediationHint = "Inspect the test.",
    };

    [[nodiscard]] Horo::Result<void> WaitForPublishedWorkerJob(const std::atomic<const Horo::JobHandle *> &published) {
        const Horo::JobHandle *handle = nullptr;
        while ((handle = published.load(std::memory_order_acquire)) == nullptr)
            std::this_thread::yield();
        return handle->Wait({.waitPolicy = Horo::WaitPolicy::WorkerOnly, .timeout = Horo::Duration::FromMilliseconds(100)});
    }

    [[nodiscard]] bool IsCapacityDeadlock(const Horo::Result<void> &result) {
        return result.HasError() && result.ErrorValue().code.Value() == "job.wait_capacity_deadlock";
    }

    TEST_CASE("Submitted Job Reaches Succeeded Terminal State", "[unit][foundation]") {
        Horo::JobSystem jobs{Horo::JobSystemConfig{.workerCount = 1, .maxQueuedJobs = 4}};
        std::atomic executed{false};
        const auto submitted = jobs.Submit(Horo::JobDescriptor{}, [&executed](const Horo::CancellationToken &) {
            executed.store(true);
        });
        REQUIRE((submitted.HasValue()));
        REQUIRE((submitted.Value().Wait().HasValue()));

        const Horo::JobSnapshot snapshot = jobs.Query(submitted.Value().Id());
        REQUIRE((executed.load()));
        REQUIRE((snapshot.state == Horo::JobState::Succeeded));
        jobs.Shutdown(Horo::ShutdownPolicy::Drain);
    }

    TEST_CASE("Bounded Queue Rejects Overflow", "[unit][foundation]") {
        Horo::JobSystem jobs{Horo::JobSystemConfig{.workerCount = 0, .maxQueuedJobs = 1}};
        REQUIRE((jobs.Submit(Horo::JobDescriptor{}, [](const Horo::CancellationToken &) {
        }).HasValue()));
        const auto rejected = jobs.Submit(Horo::JobDescriptor{}, [](const Horo::CancellationToken &) {
        });
        REQUIRE((rejected.HasError()));
        REQUIRE((rejected.ErrorValue().code.Value() == "job.queue_full"));
        jobs.Shutdown(Horo::ShutdownPolicy::Cancel);
    }

    TEST_CASE("Queued Job Can Be Cancelled By Id", "[unit][foundation]") {
        Horo::JobSystem jobs{Horo::JobSystemConfig{.workerCount = 0, .maxQueuedJobs = 1}};
        const auto submitted = jobs.Submit(Horo::JobDescriptor{}, [](const Horo::CancellationToken &) {
        });
        REQUIRE((submitted.HasValue()));
        REQUIRE((jobs.RequestCancel(submitted.Value().Id()).HasValue()));
        REQUIRE((jobs.Query(submitted.Value().Id()).state == Horo::JobState::Cancelled));
        REQUIRE((submitted.Value().Wait().HasError()));
        jobs.Shutdown(Horo::ShutdownPolicy::Cancel);
    }

    TEST_CASE("Result Returning Job Preserves Typed Failure", "[unit][foundation]") {
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

    TEST_CASE("Terminal Jobs Release Callback Ownership", "[unit][foundation][jobs][lifetime]") {
        Horo::JobSystem jobs{Horo::JobSystemConfig{.workerCount = 1, .maxQueuedJobs = 2}};
        auto completedLifetime = std::make_shared<int>(1);
        const std::weak_ptr completedProbe = completedLifetime;
        auto completed = jobs.Submit({}, [lifetime = std::move(completedLifetime)](const Horo::CancellationToken &) {
            REQUIRE(lifetime != nullptr);
        });
        REQUIRE((completed.HasValue()));
        REQUIRE((completed.Value().Wait().HasValue()));
        REQUIRE((completedProbe.expired()));

        Horo::JobSystem queuedJobs{Horo::JobSystemConfig{.workerCount = 0, .maxQueuedJobs = 1}};
        auto cancelledLifetime = std::make_shared<int>(2);
        const std::weak_ptr cancelledProbe = cancelledLifetime;
        auto cancelled = queuedJobs.Submit({}, [lifetime = std::move(cancelledLifetime)](const Horo::CancellationToken &) {
            REQUIRE(lifetime != nullptr);
        });
        REQUIRE((cancelled.HasValue()));
        REQUIRE((queuedJobs.RequestCancel(cancelled.Value().Id()).HasValue()));
        REQUIRE((cancelled.Value().Wait().HasError()));
        REQUIRE((cancelledProbe.expired()));

        queuedJobs.Shutdown(Horo::ShutdownPolicy::Cancel);
        jobs.Shutdown(Horo::ShutdownPolicy::Drain);
    }

    TEST_CASE("Bounded Wait Times Out Without Changing Running Job State", "[unit][foundation][jobs][wait]") {
        Horo::JobSystem jobs{Horo::JobSystemConfig{.workerCount = 1, .maxQueuedJobs = 1}};
        std::atomic started{false};
        std::atomic release{false};
        auto submitted = jobs.Submit({}, [&started, &release](const Horo::CancellationToken &) {
            started.store(true);
            while (!release.load())
                std::this_thread::yield();
        });
        REQUIRE((submitted.HasValue()));
        while (!started.load())
            std::this_thread::yield();

        const auto timedOut =
            submitted.Value().Wait({.waitPolicy = Horo::WaitPolicy::MainThreadPumpAllowed, .timeout = Horo::Duration::FromMilliseconds(1)});
        REQUIRE((timedOut.HasError()));
        REQUIRE((timedOut.ErrorValue().code.Value() == "job.wait_timed_out"));
        REQUIRE((jobs.Query(submitted.Value().Id()).state == Horo::JobState::Running));

        release.store(true);
        REQUIRE((submitted.Value().Wait().HasValue()));
        jobs.Shutdown(Horo::ShutdownPolicy::Drain);
    }

    TEST_CASE("Bounded Wait Validates Affinity Before Terminal Observation", "[unit][foundation][jobs][wait]") {
        Horo::JobSystem jobs{Horo::JobSystemConfig{.workerCount = 1, .maxQueuedJobs = 2}};
        auto completed = jobs.Submit({}, [](const Horo::CancellationToken &) {
        });
        REQUIRE((completed.HasValue()));
        REQUIRE((completed.Value().Wait().HasValue()));

        const auto ownerForbidden = completed.Value().Wait(
            {.waitPolicy = Horo::WaitPolicy::ForbiddenOnOwnerThread, .timeout = Horo::Duration::FromMilliseconds(10)});
        REQUIRE((ownerForbidden.HasError()));
        REQUIRE((ownerForbidden.ErrorValue().code.Value() == "job.wait_forbidden"));
        const auto nonWorker =
            completed.Value().Wait({.waitPolicy = Horo::WaitPolicy::WorkerOnly, .timeout = Horo::Duration::FromMilliseconds(10)});
        REQUIRE((nonWorker.HasError()));
        REQUIRE((nonWorker.ErrorValue().code.Value() == "job.wait_forbidden"));
        const auto invalidPolicy =
            completed.Value().Wait({.waitPolicy = static_cast<Horo::WaitPolicy>(255), .timeout = Horo::Duration::FromMilliseconds(10)});
        REQUIRE((invalidPolicy.HasError()));
        REQUIRE((invalidPolicy.ErrorValue().code.Value() == "job.wait_forbidden"));
        REQUIRE((completed.Value()
                     .Wait({.waitPolicy = Horo::WaitPolicy::MainThreadPumpAllowed, .timeout = Horo::Duration::FromMilliseconds(10)})
                     .HasValue()));

        Horo::JobSystem otherJobs{Horo::JobSystemConfig{.workerCount = 1, .maxQueuedJobs = 1}};
        auto otherCompleted = otherJobs.Submit({}, [](const Horo::CancellationToken &) {
        });
        REQUIRE((otherCompleted.HasValue()));
        REQUIRE((otherCompleted.Value().Wait().HasValue()));

        std::atomic workerAccepted{false};
        std::atomic foreignWorkerRejected{false};
        auto verifier =
            jobs.Submit({}, [&completed, &otherCompleted, &workerAccepted, &foreignWorkerRejected](const Horo::CancellationToken &) {
            workerAccepted.store(completed.Value()
                                     .Wait({.waitPolicy = Horo::WaitPolicy::WorkerOnly, .timeout = Horo::Duration::FromMilliseconds(10)})
                                     .HasValue());
            const auto foreign =
                otherCompleted.Value().Wait({.waitPolicy = Horo::WaitPolicy::WorkerOnly, .timeout = Horo::Duration::FromMilliseconds(10)});
            foreignWorkerRejected.store(foreign.HasError() && foreign.ErrorValue().code.Value() == "job.wait_forbidden");
        });
        REQUIRE((verifier.HasValue()));
        REQUIRE((verifier.Value().Wait().HasValue()));
        REQUIRE((workerAccepted.load()));
        REQUIRE((foreignWorkerRejected.load()));
        otherJobs.Shutdown(Horo::ShutdownPolicy::Drain);
        jobs.Shutdown(Horo::ShutdownPolicy::Drain);
    }

    TEST_CASE("Bounded Owner Wait Helps Only Its Exact Queued Record", "[unit][foundation][jobs][wait]") {
        Horo::JobSystem jobs{Horo::JobSystemConfig{.workerCount = 0, .maxQueuedJobs = 2}};
        std::atomic unrelatedExecuted{false};
        std::atomic awaitedExecuted{false};
        const auto unrelated = jobs.Submit({}, [&unrelatedExecuted](const Horo::CancellationToken &) {
            unrelatedExecuted.store(true);
        });
        auto awaited = jobs.Submit({}, [&awaitedExecuted](const Horo::CancellationToken &) {
            awaitedExecuted.store(true);
        });
        REQUIRE((unrelated.HasValue()));
        REQUIRE((awaited.HasValue()));

        const auto result =
            awaited.Value().Wait({.waitPolicy = Horo::WaitPolicy::MainThreadPumpAllowed, .timeout = Horo::Duration::FromMilliseconds(100)});
        REQUIRE((result.HasValue()));
        REQUIRE((awaitedExecuted.load()));
        REQUIRE_FALSE((unrelatedExecuted.load()));
        REQUIRE((jobs.Query(unrelated.Value().Id()).state == Horo::JobState::Queued));

        auto replacement = jobs.Submit({}, [](const Horo::CancellationToken &) {
        });
        REQUIRE((replacement.HasValue()));

        REQUIRE((jobs.RequestCancel(unrelated.Value().Id()).HasValue()));
        REQUIRE((jobs.RequestCancel(replacement.Value().Id()).HasValue()));
        jobs.Shutdown(Horo::ShutdownPolicy::Cancel);
    }

    TEST_CASE("Worker Wait Helps Its Exact Queued Dependency", "[unit][foundation][jobs][wait]") {
        Horo::JobSystem jobs{Horo::JobSystemConfig{.workerCount = 1, .maxQueuedJobs = 2}};
        std::atomic<const Horo::JobHandle *> dependencyHandle{};
        std::atomic dependencyExecuted{false};
        std::atomic dependencyWaitSucceeded{false};

        auto parent = jobs.Submit({}, [&](const Horo::CancellationToken &) {
            dependencyWaitSucceeded.store(WaitForPublishedWorkerJob(dependencyHandle).HasValue());
        });
        REQUIRE((parent.HasValue()));
        auto dependency = jobs.Submit({}, [&dependencyExecuted](const Horo::CancellationToken &) {
            dependencyExecuted.store(true);
        });
        REQUIRE((dependency.HasValue()));
        dependencyHandle.store(&dependency.Value());

        REQUIRE((parent.Value().Wait().HasValue()));
        REQUIRE((dependencyWaitSucceeded.load()));
        REQUIRE((dependencyExecuted.load()));
        jobs.Shutdown(Horo::ShutdownPolicy::Drain);
    }

    TEST_CASE("Worker Self Wait Returns Capacity Deadlock Without Blocking", "[unit][foundation][jobs][wait]") {
        Horo::JobSystem jobs{Horo::JobSystemConfig{.workerCount = 1, .maxQueuedJobs = 2}};
        std::atomic<const Horo::JobHandle *> selfHandle{};
        std::atomic selfWaitRejected{false};
        auto submitted = jobs.Submit({}, [&](const Horo::CancellationToken &) {
            selfWaitRejected.store(IsCapacityDeadlock(WaitForPublishedWorkerJob(selfHandle)));
        });
        REQUIRE((submitted.HasValue()));
        selfHandle.store(&submitted.Value());

        REQUIRE((submitted.Value().Wait().HasValue()));
        REQUIRE((selfWaitRejected.load()));
        jobs.Shutdown(Horo::ShutdownPolicy::Drain);
    }

    TEST_CASE("Worker Helping Rejects A Reentrant Wait Cycle", "[unit][foundation][jobs][wait]") {
        Horo::JobSystem jobs{Horo::JobSystemConfig{.workerCount = 1, .maxQueuedJobs = 2}};
        std::atomic<const Horo::JobHandle *> parentHandle{};
        std::atomic<const Horo::JobHandle *> childHandle{};
        std::atomic cycleRejected{false};
        std::atomic childWaitSucceeded{false};

        auto parent = jobs.Submit({}, [&](const Horo::CancellationToken &) {
            childWaitSucceeded.store(WaitForPublishedWorkerJob(childHandle).HasValue());
        });
        REQUIRE((parent.HasValue()));
        parentHandle.store(&parent.Value());
        auto child = jobs.Submit({}, [&](const Horo::CancellationToken &) {
            cycleRejected.store(IsCapacityDeadlock(WaitForPublishedWorkerJob(parentHandle)));
        });
        REQUIRE((child.HasValue()));
        childHandle.store(&child.Value());

        REQUIRE((parent.Value().Wait().HasValue()));
        REQUIRE((childWaitSucceeded.load()));
        REQUIRE((cycleRejected.load()));
        jobs.Shutdown(Horo::ShutdownPolicy::Drain);
    }

    TEST_CASE("Timed Out Task Group Join Remains Retryable", "[unit][foundation][jobs][wait]") {
        Horo::JobSystem jobs{Horo::JobSystemConfig{.workerCount = 1, .maxQueuedJobs = 3}};
        std::mutex mutex;
        std::condition_variable condition;
        bool blockerStarted{};
        bool releaseBlocker{};
        auto blocker = jobs.Submit({}, [&](const Horo::CancellationToken &) {
            std::unique_lock lock(mutex);
            blockerStarted = true;
            condition.notify_all();
            condition.wait(lock, [&] {
                return releaseBlocker;
            });
        });
        REQUIRE((blocker.HasValue()));
        {
            std::unique_lock lock(mutex);
            condition.wait(lock, [&] {
                return blockerStarted;
            });
        }

        Horo::TaskGroup group(jobs);
        std::atomic childExecuted{false};
        REQUIRE((group
                     .Spawn({}, [&childExecuted](const Horo::CancellationToken &) {
            childExecuted.store(true);
            return Horo::Result<void>::Success();
        }).HasValue()));

        std::string waitError;
        std::thread waiter([&] {
            const auto joined =
                group.Join({.waitPolicy = Horo::WaitPolicy::ForbiddenOnOwnerThread, .timeout = Horo::Duration::FromMilliseconds(10)});
            if (joined.HasError())
                waitError = joined.ErrorValue().code.Value();
        });
        waiter.join();
        REQUIRE((waitError == "job.wait_timed_out"));
        REQUIRE_FALSE((childExecuted.load()));

        {
            std::lock_guard lock(mutex);
            releaseBlocker = true;
        }
        condition.notify_all();
        REQUIRE((blocker.Value().Wait().HasValue()));
        REQUIRE((group.Join().HasValue()));
        REQUIRE((group.Join().HasValue()));
        REQUIRE((childExecuted.load()));
        jobs.Shutdown(Horo::ShutdownPolicy::Drain);
    }

    TEST_CASE("Bounded Task Group Join Preserves First Failure In Spawn Order", "[unit][foundation][jobs][wait]") {
        Horo::JobSystem jobs{Horo::JobSystemConfig{.workerCount = 0, .maxQueuedJobs = 2}};
        Horo::TaskGroup group(jobs, Horo::TaskGroupFailurePolicy::CollectAll);
        Horo::Error firstError = Horo::MakeError(TestFailure);
        firstError.code = Horo::ErrorCode("test.job_system.bounded_first");
        Horo::Error secondError = Horo::MakeError(TestFailure);
        secondError.code = Horo::ErrorCode("test.job_system.bounded_second");
        REQUIRE((group
                     .Spawn({}, [firstError](const Horo::CancellationToken &) {
            return Horo::Result<void>::Failure(firstError);
        }).HasValue()));
        REQUIRE((group
                     .Spawn({}, [secondError](const Horo::CancellationToken &) {
            return Horo::Result<void>::Failure(secondError);
        }).HasValue()));

        const auto joined =
            group.Join({.waitPolicy = Horo::WaitPolicy::MainThreadPumpAllowed, .timeout = Horo::Duration::FromMilliseconds(100)});
        REQUIRE((joined.HasError()));
        REQUIRE((joined.ErrorValue().code.Value() == "test.job_system.bounded_first"));
        REQUIRE((group.Join().ErrorValue().code.Value() == "test.job_system.bounded_first"));
        jobs.Shutdown(Horo::ShutdownPolicy::Drain);
    }

    TEST_CASE("Task Group Destructor Cancels And Drains After Bounded Timeout", "[unit][foundation][jobs][wait][lifetime]") {
        Horo::JobSystem jobs{Horo::JobSystemConfig{.workerCount = 1, .maxQueuedJobs = 1}};
        std::atomic started{false};
        std::atomic stopped{false};
        auto group = std::make_unique<Horo::TaskGroup>(jobs);
        const Horo::JobFunction cancellationProbe = [&](const Horo::CancellationToken &cancellation) {
            started.store(true, std::memory_order_release);
            for (; !cancellation.IsCancellationRequested(); std::this_thread::yield()) {
            }
            stopped.store(true, std::memory_order_release);
            return Horo::Result<void>::Success();
        };
        REQUIRE((group->Spawn({}, cancellationProbe).HasValue()));
        while (!started.load(std::memory_order_acquire))
            std::this_thread::yield();
        const auto timedOut =
            group->Join({.waitPolicy = Horo::WaitPolicy::MainThreadPumpAllowed, .timeout = Horo::Duration::FromMilliseconds(1)});
        REQUIRE((timedOut.HasError()));
        REQUIRE((timedOut.ErrorValue().code.Value() == "job.wait_timed_out"));
        group.reset();
        REQUIRE((stopped.load()));
        jobs.Shutdown(Horo::ShutdownPolicy::Drain);
    }

    TEST_CASE("Task Group Collect All Returns Failures In Spawn Order", "[unit][foundation]") {
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
        REQUIRE((group
                     .Spawn({},
                            [](const Horo::CancellationToken &) {
            return Horo::Result<void>::Success();
        })
                     .ErrorValue()
                     .code.Value() == "job.task_group_closed"));
        jobs.Shutdown(Horo::ShutdownPolicy::Drain);
    }

    TEST_CASE("Task Group Fail Fast Cancels Accepted Siblings", "[unit][foundation]") {
        Horo::JobSystem jobs{Horo::JobSystemConfig{.workerCount = 2, .maxQueuedJobs = 8}};
        Horo::TaskGroup group(jobs, Horo::TaskGroupFailurePolicy::FailFast);
        std::mutex mutex;
        std::condition_variable started;
        bool siblingStarted = false;
        std::atomic siblingObservedCancellation{false};

        REQUIRE((group
                     .Spawn({}, [&](const Horo::CancellationToken &cancellation) {
            {
                std::lock_guard lock(mutex);
                siblingStarted = true;
            }
            started.notify_one();
            while (!cancellation.IsCancellationRequested())
                std::this_thread::yield();
            siblingObservedCancellation.store(true);
            return Horo::Result<void>::Failure(Horo::MakeError(TestFailure));
        }).HasValue()));
        REQUIRE((group
                     .Spawn({}, [&](const Horo::CancellationToken &) {
            std::unique_lock lock(mutex);
            started.wait(lock, [&] {
                return siblingStarted;
            });
            return Horo::Result<void>::Failure(Horo::MakeError(TestFailure));
        }).HasValue()));

        REQUIRE((group.Join().HasError()));
        REQUIRE((siblingObservedCancellation.load()));
        jobs.Shutdown(Horo::ShutdownPolicy::Drain);
    }

    TEST_CASE("Rejected Task Group Child Is Not Joined", "[unit][foundation]") {
        Horo::JobSystem jobs{Horo::JobSystemConfig{.workerCount = 0, .maxQueuedJobs = 1}};
        Horo::TaskGroup group(jobs, Horo::TaskGroupFailurePolicy::CollectAll);
        REQUIRE((group
                     .Spawn({}, [](const Horo::CancellationToken &) {
            return Horo::Result<void>::Success();
        }).HasValue()));
        const auto rejected = group.Spawn({}, [](const Horo::CancellationToken &) {
            return Horo::Result<void>::Success();
        });
        REQUIRE((rejected.HasError()));
        REQUIRE((rejected.ErrorValue().code.Value() == "job.queue_full"));
        group.RequestCancel();
        REQUIRE((group.Join().HasError()));
        jobs.Shutdown(Horo::ShutdownPolicy::Cancel);
    }

    TEST_CASE("Parent Cancellation Flows To Task Group Children", "[unit][foundation]") {
        Horo::JobSystem jobs{Horo::JobSystemConfig{.workerCount = 1, .maxQueuedJobs = 4}};
        Horo::CancellationSource parent;
        parent.RequestCancellation();
        Horo::TaskGroup group(jobs, Horo::TaskGroupFailurePolicy::FailFast, parent.Token());
        std::atomic executed{false};
        REQUIRE((group
                     .Spawn({}, [&executed](const Horo::CancellationToken &) {
            executed.store(true);
            return Horo::Result<void>::Success();
        }).HasValue()));
        REQUIRE((group.Join().HasError()));
        REQUIRE((!executed.load()));
        jobs.Shutdown(Horo::ShutdownPolicy::Drain);
    }

    TEST_CASE("Destructor Cancels And Joins Children", "[unit][foundation]") {
        Horo::JobSystem jobs{Horo::JobSystemConfig{.workerCount = 1, .maxQueuedJobs = 4}};
        std::atomic started{false};
        std::atomic stopped{false};
        {
            Horo::TaskGroup group(jobs);
            REQUIRE((group
                         .Spawn({}, [&started, &stopped](const Horo::CancellationToken &cancellation) {
                started.store(true);
                while (!cancellation.IsCancellationRequested())
                    std::this_thread::yield();
                stopped.store(true);
                return Horo::Result<void>::Success();
            }).HasValue()));
            while (!started.load())
                std::this_thread::yield();
        }
        REQUIRE((stopped.load()));
        jobs.Shutdown(Horo::ShutdownPolicy::Drain);
    }
}  // namespace
