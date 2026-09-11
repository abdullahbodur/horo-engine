#include "AndroidLifecycleAdapter.h"
#include "Horo/Platform/AndroidLifecycle.h"

#include <array>
#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <thread>
#include <vector>

namespace {
    using namespace Horo;
    using namespace Horo::Platform;

    [[nodiscard]] std::unique_ptr<AndroidLifecycleController> MakeController(const std::size_t capacity = 64) {
        auto created = AndroidLifecycleController::Create(std::this_thread::get_id(), capacity);
        REQUIRE(created.HasValue());
        return std::move(created).Value();
    }

    void Enqueue(AndroidLifecycleController &controller, const AndroidLifecycleEvent event) {
        REQUIRE(controller.Enqueue(event).HasValue());
    }

    [[nodiscard]] AndroidLifecycleSnapshot Snapshot(AndroidLifecycleController &controller) {
        auto snapshot = controller.Snapshot();
        REQUIRE(snapshot.HasValue());
        return snapshot.Value();
    }

    void ColdLaunch(AndroidLifecycleController &controller, const bool presentation = true) {
        constexpr AndroidActivityGeneration activity{1};
        constexpr AndroidWindowGeneration window{1};
        Enqueue(controller, {.kind = AndroidLifecycleEvent::Kind::ProcessCreated});
        Enqueue(controller, {.kind = AndroidLifecycleEvent::Kind::ActivityCreated, .activity = activity});
        Enqueue(controller, {.kind = AndroidLifecycleEvent::Kind::ActivityStarted, .activity = activity});
        Enqueue(controller, {.kind = AndroidLifecycleEvent::Kind::ActivityResumed, .activity = activity});
        if (presentation) {
            Enqueue(controller, {.kind = AndroidLifecycleEvent::Kind::WindowAvailable, .activity = activity, .window = window});
            Enqueue(controller, {.kind = AndroidLifecycleEvent::Kind::PresentationAvailable,
                                 .window = window,
                                 .presentation = AndroidPresentationGeneration{1}});
        }
        REQUIRE(controller.Drain().HasValue());
    }

    TEST_CASE("Android lifecycle cold launch and background resume preserve process identity", "[unit][platform][android]") {
        auto controller = MakeController();
        ColdLaunch(*controller);
        auto running = Snapshot(*controller);
        REQUIRE(running.processGeneration == 1);
        REQUIRE(running.process == AndroidProcessState::Running);
        REQUIRE(running.activity == AndroidActivityState::Resumed);
        REQUIRE(running.hasPresentation);

        Enqueue(*controller, {.kind = AndroidLifecycleEvent::Kind::ActivityPaused, .activity = AndroidActivityGeneration{1}});
        Enqueue(*controller, {.kind = AndroidLifecycleEvent::Kind::ActivityStopped, .activity = AndroidActivityGeneration{1}});
        REQUIRE(controller->Drain().HasValue());
        const auto background = Snapshot(*controller);
        REQUIRE(background.process == AndroidProcessState::Suspended);
        REQUIRE(background.processGeneration == running.processGeneration);

        Enqueue(*controller, {.kind = AndroidLifecycleEvent::Kind::ActivityStarted, .activity = AndroidActivityGeneration{1}});
        REQUIRE(controller->Drain().HasValue());
        REQUIRE(Snapshot(*controller).process == AndroidProcessState::Suspended);
        Enqueue(*controller, {.kind = AndroidLifecycleEvent::Kind::ActivityResumed, .activity = AndroidActivityGeneration{1}});
        REQUIRE(controller->Drain().HasValue());
        const auto resumed = Snapshot(*controller);
        REQUIRE(resumed.process == AndroidProcessState::Running);
        REQUIRE(resumed.processGeneration == running.processGeneration);
        REQUIRE(resumed.activityGeneration == running.activityGeneration);
    }

    TEST_CASE("Activity replacement retires old surfaces without replacing durable process identity", "[unit][platform][android]") {
        auto controller = MakeController();
        ColdLaunch(*controller);
        const auto original = Snapshot(*controller);

        Enqueue(*controller, {.kind = AndroidLifecycleEvent::Kind::ActivityPaused, .activity = AndroidActivityGeneration{1}});
        Enqueue(*controller, {.kind = AndroidLifecycleEvent::Kind::ActivityStopped, .activity = AndroidActivityGeneration{1}});
        Enqueue(*controller, {.kind = AndroidLifecycleEvent::Kind::ActivityDestroyed, .activity = AndroidActivityGeneration{1}});
        Enqueue(*controller, {.kind = AndroidLifecycleEvent::Kind::ActivityCreated, .activity = AndroidActivityGeneration{2}});
        REQUIRE(controller->Drain().HasValue());
        const auto replacement = Snapshot(*controller);
        REQUIRE(replacement.processGeneration == original.processGeneration);
        REQUIRE(replacement.activityGeneration == AndroidActivityGeneration{2});
        REQUIRE_FALSE(replacement.hasWindow);
        REQUIRE_FALSE(replacement.hasPresentation);
        REQUIRE(replacement.windowGeneration == AndroidWindowGeneration{1});
        REQUIRE(replacement.presentationGeneration == AndroidPresentationGeneration{1});

        Enqueue(*controller, {.kind = AndroidLifecycleEvent::Kind::WindowLost, .window = AndroidWindowGeneration{1}});
        const auto staleWindow = controller->Drain();
        REQUIRE(staleWindow.HasValue());  // duplicate retirement is idempotent

        Enqueue(*controller, {.kind = AndroidLifecycleEvent::Kind::ActivityResumed, .activity = AndroidActivityGeneration{1}});
        const auto staleActivity = controller->Drain();
        REQUIRE(staleActivity.HasError());
        REQUIRE(staleActivity.ErrorValue().code.Value() == "android_lifecycle.stale_generation");
    }

    TEST_CASE("Window and presentation generations reject ABA and stale access", "[unit][platform][android]") {
        auto controller = MakeController();
        ColdLaunch(*controller);
        Enqueue(*controller, {.kind = AndroidLifecycleEvent::Kind::WindowLost, .window = AndroidWindowGeneration{1}});
        Enqueue(*controller, {.kind = AndroidLifecycleEvent::Kind::WindowAvailable,
                              .activity = AndroidActivityGeneration{1},
                              .window = AndroidWindowGeneration{2}});
        Enqueue(*controller, {.kind = AndroidLifecycleEvent::Kind::PresentationAvailable,
                              .window = AndroidWindowGeneration{2},
                              .presentation = AndroidPresentationGeneration{2}});
        REQUIRE(controller->Drain().HasValue());

        Enqueue(*controller, {.kind = AndroidLifecycleEvent::Kind::WindowLost, .window = AndroidWindowGeneration{1}});
        auto staleWindow = controller->Drain();
        REQUIRE(staleWindow.HasError());
        REQUIRE(staleWindow.ErrorValue().code.Value() == "android_lifecycle.stale_generation");
        auto current = Snapshot(*controller);
        REQUIRE(current.hasWindow);
        REQUIRE(current.hasPresentation);

        Enqueue(*controller, {.kind = AndroidLifecycleEvent::Kind::PresentationLost, .presentation = AndroidPresentationGeneration{1}});
        auto stalePresentation = controller->Drain();
        REQUIRE(stalePresentation.HasError());
        REQUIRE(Snapshot(*controller).hasPresentation);
    }

    TEST_CASE("Duplicates are idempotent and illegal ordering is a typed rejection", "[unit][platform][android]") {
        auto controller = MakeController();
        Enqueue(*controller, {.kind = AndroidLifecycleEvent::Kind::ProcessCreated});
        Enqueue(*controller, {.kind = AndroidLifecycleEvent::Kind::ProcessCreated});
        Enqueue(*controller, {.kind = AndroidLifecycleEvent::Kind::ActivityCreated, .activity = AndroidActivityGeneration{1}});
        Enqueue(*controller, {.kind = AndroidLifecycleEvent::Kind::ActivityCreated, .activity = AndroidActivityGeneration{1}});
        REQUIRE(controller->Drain().HasValue());
        REQUIRE(Snapshot(*controller).duplicateObservationCount == 2);

        Enqueue(*controller, {.kind = AndroidLifecycleEvent::Kind::ActivityPaused, .activity = AndroidActivityGeneration{1}});
        Enqueue(*controller, {.kind = AndroidLifecycleEvent::Kind::ActivityStarted, .activity = AndroidActivityGeneration{1}});
        auto drained = controller->Drain();
        REQUIRE(drained.HasError());
        REQUIRE(drained.ErrorValue().code.Value() == "android_lifecycle.invalid_transition");
        const auto after = Snapshot(*controller);
        REQUIRE(after.activity == AndroidActivityState::Started);
        REQUIRE(after.rejectedObservationCount == 1);
        REQUIRE(after.queuedObservationCount == 0);
    }

    TEST_CASE("Required generations and queue capacity fail closed", "[unit][platform][android]") {
        REQUIRE(AndroidLifecycleController::Create({}, 2).HasError());
        REQUIRE(AndroidLifecycleController::Create(std::this_thread::get_id(), 0).HasError());
        REQUIRE(AndroidLifecycleController::Create(std::this_thread::get_id(), AndroidLifecycleController::MaximumQueueCapacity + 1)
                    .HasError());

        auto controller = MakeController(2);
        const auto invalid = controller->Enqueue({.kind = AndroidLifecycleEvent::Kind::ActivityCreated});
        REQUIRE(invalid.HasError());
        REQUIRE(invalid.ErrorValue().code.Value() == "android_lifecycle.invalid_generation");
        Enqueue(*controller, {.kind = AndroidLifecycleEvent::Kind::ProcessCreated});
        Enqueue(*controller, {.kind = AndroidLifecycleEvent::Kind::ActivityCreated, .activity = AndroidActivityGeneration{1}});
        const auto saturated =
            controller->Enqueue({.kind = AndroidLifecycleEvent::Kind::ActivityStarted, .activity = AndroidActivityGeneration{1}});
        REQUIRE(saturated.HasError());
        REQUIRE(saturated.ErrorValue().code.Value() == "android_lifecycle.queue_saturated");
        REQUIRE(controller->Drain().HasValue());
    }

    TEST_CASE("Every legal Activity transition is admitted deterministically", "[unit][platform][android]") {
        using Kind = AndroidLifecycleEvent::Kind;

        struct LegalPath {
            std::array<Kind, 3> events;
            std::size_t count;
            AndroidActivityState expected;
        };

        constexpr std::array paths{
            LegalPath{{Kind::ActivityStarted}, 1, AndroidActivityState::Started},
            LegalPath{{Kind::ActivityDestroyed}, 1, AndroidActivityState::Destroyed},
            LegalPath{{Kind::ActivityStarted, Kind::ActivityResumed}, 2, AndroidActivityState::Resumed},
            LegalPath{{Kind::ActivityStarted, Kind::ActivityStopped}, 2, AndroidActivityState::Stopped},
            LegalPath{{Kind::ActivityStarted, Kind::ActivityResumed, Kind::ActivityPaused}, 3, AndroidActivityState::Paused},
            LegalPath{{Kind::ActivityStarted, Kind::ActivityStopped, Kind::ActivityStarted}, 3, AndroidActivityState::Started},
            LegalPath{{Kind::ActivityStarted, Kind::ActivityStopped, Kind::ActivityDestroyed}, 3, AndroidActivityState::Destroyed},
        };
        for (const LegalPath &path : paths) {
            auto controller = MakeController();
            Enqueue(*controller, {.kind = Kind::ProcessCreated});
            Enqueue(*controller, {.kind = Kind::ActivityCreated, .activity = AndroidActivityGeneration{1}});
            for (std::size_t index = 0; index < path.count; ++index)
                Enqueue(*controller, {.kind = path.events[index], .activity = AndroidActivityGeneration{1}});
            REQUIRE(controller->Drain().HasValue());
            REQUIRE(Snapshot(*controller).activity == path.expected);
        }

        auto resumeFromPause = MakeController();
        ColdLaunch(*resumeFromPause, false);
        Enqueue(*resumeFromPause, {.kind = Kind::ActivityPaused, .activity = AndroidActivityGeneration{1}});
        Enqueue(*resumeFromPause, {.kind = Kind::ActivityResumed, .activity = AndroidActivityGeneration{1}});
        REQUIRE(resumeFromPause->Drain().HasValue());
        REQUIRE(Snapshot(*resumeFromPause).activity == AndroidActivityState::Resumed);
    }

    TEST_CASE("Only the captured owner thread may commit or observe lifecycle state", "[unit][platform][android]") {
        auto controller = MakeController();
        Enqueue(*controller, {.kind = AndroidLifecycleEvent::Kind::ProcessCreated});
        bool drainRejected{};
        bool snapshotRejected{};
        std::thread worker([&] {
            const auto drained = controller->Drain();
            drainRejected = drained.HasError() && drained.ErrorValue().code.Value() == "android_lifecycle.owner_thread_required";
            const auto snapshot = controller->Snapshot();
            snapshotRejected = snapshot.HasError() && snapshot.ErrorValue().code.Value() == "android_lifecycle.owner_thread_required";
        });
        worker.join();
        REQUIRE(drainRejected);
        REQUIRE(snapshotRejected);
        REQUIRE(controller->Drain().HasValue());
    }

    TEST_CASE("Private callback adapter allocates monotonic generations and only enqueues", "[unit][platform][android]") {
        auto controller = MakeController();
        Platform::Android::AndroidLifecycleAdapter adapter(*controller);
        REQUIRE(adapter.OnProcessCreated().HasValue());
        auto activity = adapter.OnActivityCreated();
        REQUIRE(activity.HasValue());
        REQUIRE(activity.Value() == AndroidActivityGeneration{1});
        REQUIRE(Snapshot(*controller).activity == AndroidActivityState::Absent);
        REQUIRE(adapter.OnActivityStarted(activity.Value()).HasValue());
        REQUIRE(adapter.OnActivityResumed(activity.Value()).HasValue());
        auto window = adapter.OnWindowAvailable(activity.Value());
        REQUIRE(window.HasValue());
        auto presentation = adapter.OnPresentationAvailable(window.Value());
        REQUIRE(presentation.HasValue());
        REQUIRE(controller->Drain().HasValue());
        REQUIRE(Snapshot(*controller).hasPresentation);

        REQUIRE(adapter.OnActivityPaused(activity.Value()).HasValue());
        REQUIRE(adapter.OnActivityStopped(activity.Value()).HasValue());
        REQUIRE(adapter.OnActivityDestroyed(activity.Value()).HasValue());
        auto replacement = adapter.OnActivityCreated();
        REQUIRE(replacement.HasValue());
        REQUIRE(replacement.Value() == AndroidActivityGeneration{2});
        REQUIRE(controller->Drain().HasValue());
    }

    TEST_CASE("Final shutdown closes callbacks and waits for explicit reverse teardown completion", "[unit][platform][android]") {
        auto controller = MakeController();
        ColdLaunch(*controller);
        Enqueue(*controller, {.kind = AndroidLifecycleEvent::Kind::FinalShutdown});
        const auto late =
            controller->Enqueue({.kind = AndroidLifecycleEvent::Kind::ActivityPaused, .activity = AndroidActivityGeneration{1}});
        REQUIRE(late.HasError());
        REQUIRE(late.ErrorValue().code.Value() == "android_lifecycle.admission_closed");
        REQUIRE(controller->Drain().HasValue());
        const auto shuttingDown = Snapshot(*controller);
        REQUIRE(shuttingDown.process == AndroidProcessState::ShuttingDown);
        REQUIRE_FALSE(shuttingDown.hasWindow);
        REQUIRE_FALSE(shuttingDown.hasPresentation);
        REQUIRE(controller->CompleteShutdown().HasValue());
        REQUIRE(Snapshot(*controller).process == AndroidProcessState::Stopped);
        REQUIRE(controller->CompleteShutdown().HasError());
    }

    TEST_CASE("Shutdown wins a concurrent late callback race and supports partial initialization", "[unit][platform][android]") {
        auto controller = MakeController();
        Enqueue(*controller, {.kind = AndroidLifecycleEvent::Kind::ProcessCreated});
        Enqueue(*controller, {.kind = AndroidLifecycleEvent::Kind::ActivityCreated, .activity = AndroidActivityGeneration{1}});
        std::atomic<bool> shutdownQueued{};
        bool lateRejected{};
        std::thread callback([&] {
            while (!shutdownQueued.load(std::memory_order_acquire))
                std::this_thread::yield();
            const auto late =
                controller->Enqueue({.kind = AndroidLifecycleEvent::Kind::ActivityStarted, .activity = AndroidActivityGeneration{1}});
            lateRejected = late.HasError() && late.ErrorValue().code.Value() == "android_lifecycle.admission_closed";
        });
        Enqueue(*controller, {.kind = AndroidLifecycleEvent::Kind::FinalShutdown});
        shutdownQueued.store(true, std::memory_order_release);
        callback.join();
        REQUIRE(lateRejected);
        REQUIRE(controller->Drain().HasValue());
        REQUIRE(Snapshot(*controller).process == AndroidProcessState::ShuttingDown);
        REQUIRE(controller->CompleteShutdown().HasValue());

        auto partial = MakeController();
        Enqueue(*partial, {.kind = AndroidLifecycleEvent::Kind::FinalShutdown});
        REQUIRE(partial->Drain().HasValue());
        REQUIRE(partial->CompleteShutdown().HasValue());
        REQUIRE(Snapshot(*partial).processGeneration == 0);
    }

    TEST_CASE("Headless lifecycle runs without a window or presentation generation", "[unit][platform][android]") {
        auto controller = MakeController();
        ColdLaunch(*controller, false);
        const auto headless = Snapshot(*controller);
        REQUIRE(headless.process == AndroidProcessState::Running);
        REQUIRE(headless.activity == AndroidActivityState::Resumed);
        REQUIRE(headless.windowGeneration == AndroidWindowGeneration{});
        REQUIRE(headless.presentationGeneration == AndroidPresentationGeneration{});
        REQUIRE_FALSE(headless.hasWindow);
        REQUIRE_FALSE(headless.hasPresentation);
    }
}  // namespace
