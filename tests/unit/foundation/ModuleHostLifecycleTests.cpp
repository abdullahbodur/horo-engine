#include "Horo/Foundation/ModuleHost.h"
#include "ModuleDescriptorTestUtils.h"

#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {
    using namespace Horo;
    using Horo::Test::MakeModule;

    std::vector<std::string> g_events;
    std::optional<ModuleCallbackLease> g_callbackLease;
    std::atomic<bool> g_callbackObservedCancellation{false};
    std::atomic<bool> g_callbackFinished{false};
    const void *g_callbackObservedBindings = nullptr;
    bool g_drainObservedClosedAdmission = false;
    bool g_deactivateObservedDrain = false;

    void ResetLifecycleLog() {
        g_events.clear();
        g_callbackLease.reset();
        g_callbackObservedCancellation.store(false);
        g_callbackFinished.store(false);
        g_callbackObservedBindings = nullptr;
        g_drainObservedClosedAdmission = false;
        g_deactivateObservedDrain = false;
    }

    Result<void> OrderedActivate(ModuleActivationContext &context) noexcept {
        g_events.push_back("activate:" + context.Module().value);
        return Result<void>::Success();
    }

    void OrderedDrain(ModuleActivationContext &context) noexcept {
        g_events.push_back("drain:" + context.Module().value);
        g_events.push_back(context.Cancellation().IsCancellationRequested() ? "cancelled" : "not-cancelled");
        g_events.push_back(context.AcquireCallbackLease().has_value() ? "admission-open" : "admission-closed");
    }

    void OrderedDeactivate(ModuleActivationContext &context) noexcept {
        g_events.push_back("deactivate:" + context.Module().value);
    }

    Result<void> LeaseActivate(ModuleActivationContext &context) noexcept {
        g_callbackLease = context.AcquireCallbackLease();
        return g_callbackLease.has_value() ? Result<void>::Success() : Result<void>::Failure(Error{});
    }

    void LeaseDrain(ModuleActivationContext &context) noexcept {
        g_drainObservedClosedAdmission = !context.AcquireCallbackLease().has_value();
        g_deactivateObservedDrain = g_callbackFinished.load();
    }

    void LeaseDeactivate(ModuleActivationContext &) noexcept {
        g_deactivateObservedDrain = g_deactivateObservedDrain && g_callbackFinished.load();
    }

    [[nodiscard]] ModuleDescriptor MakeLifecycleModule(std::string id) {
        ModuleDescriptor descriptor = MakeModule(std::move(id));
        descriptor.lifecycle =
            ModuleLifecycleCallbacks{.activate = &OrderedActivate, .drain = &OrderedDrain, .deactivate = &OrderedDeactivate};
        return descriptor;
    }
}  // namespace

TEST_CASE("Module shutdown requests cancellation before dependency-reverse drainage", "[unit][foundation][modules][lifecycle]") {
    ResetLifecycleLog();
    ModuleHost host;

    ModuleDescriptor dependant = MakeLifecycleModule("horo.dependant");
    dependant.dependencies.push_back(ModuleDependency{.module = ModuleId{"horo.provider"}});
    REQUIRE(host.Register(dependant).HasValue());
    REQUIRE(host.Register(MakeLifecycleModule("horo.provider")).HasValue());
    REQUIRE(host.ActivateRegistered(nullptr).HasValue());

    host.DeactivateAll();

    REQUIRE(g_events == std::vector<std::string>{"activate:horo.provider", "activate:horo.dependant", "drain:horo.dependant", "cancelled",
                                                 "admission-closed", "deactivate:horo.dependant", "drain:horo.provider", "cancelled",
                                                 "admission-closed", "deactivate:horo.provider"});
    REQUIRE(host.StateOf(ModuleId{"horo.dependant"}) == ModuleLifecycleState::Stopped);
    REQUIRE(host.StateOf(ModuleId{"horo.provider"}) == ModuleLifecycleState::Stopped);

    const std::size_t eventCount = g_events.size();
    host.DeactivateAll();
    REQUIRE(g_events.size() == eventCount);
}

TEST_CASE("Module shutdown drains admitted callbacks before releasing borrowed bindings", "[unit][foundation][modules][lifecycle]") {
    ResetLifecycleLog();
    ModuleHost host;
    ModuleDescriptor descriptor = MakeModule("horo.async");
    descriptor.lifecycle = ModuleLifecycleCallbacks{.activate = &LeaseActivate, .drain = &LeaseDrain, .deactivate = &LeaseDeactivate};
    REQUIRE(host.Register(descriptor).HasValue());

    const int approvedBinding = 42;
    REQUIRE(host.ActivateRegistered(&approvedBinding).HasValue());
    REQUIRE(g_callbackLease.has_value());

    std::thread callback([lease = std::move(*g_callbackLease)]() mutable {
        while (!lease.Cancellation().IsCancellationRequested())
            std::this_thread::yield();
        g_callbackObservedBindings = lease.Bindings();
        g_callbackObservedCancellation.store(true);
        g_callbackFinished.store(true);
    });
    g_callbackLease.reset();

    host.DeactivateAll();
    callback.join();

    REQUIRE(g_callbackObservedCancellation.load());
    REQUIRE(g_callbackObservedBindings == &approvedBinding);
    REQUIRE(g_callbackFinished.load());
    REQUIRE(g_drainObservedClosedAdmission);
    REQUIRE(g_deactivateObservedDrain);
    REQUIRE(host.StateOf(ModuleId{"horo.async"}) == ModuleLifecycleState::Stopped);
}
