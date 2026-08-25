#include "Horo/Foundation/ModuleHost.h"

#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {
    using namespace Horo;

    // Module lifecycle callbacks are plain function addresses copied into
    // descriptors, so every piece of state they touch is file-local and outlives
    // all hosts in this translation unit. Each test resets the log first.
    struct ActivationLog {
        std::vector<std::string> activated;
        std::vector<std::string> deactivated;
    };

    ActivationLog g_log;
    int g_instancesAlive = 0;
    std::string g_failModule;
    // Modules observed entering the active set; the rollback order source of truth.
    std::vector<std::string> g_activationCompletions;

    void ResetLog() {
        g_log.activated.clear();
        g_log.deactivated.clear();
        g_instancesAlive = 0;
        g_failModule.clear();
        g_activationCompletions.clear();
    }

    /** @brief Instance type proving attached module state is released with its context. */
    struct CountingInstance final : IModuleInstance {
        CountingInstance() {
            ++g_instancesAlive;
        }

        ~CountingInstance() override {
            --g_instancesAlive;
        }
    };

    Result<void> LogActivate(ModuleActivationContext &context) noexcept {
        g_log.activated.push_back(context.Module().value);
        if (g_failModule == context.Module().value)
            return Result<void>::Failure(Error{});
        (void)context.AttachInstance(std::make_unique<CountingInstance>());
        g_activationCompletions.push_back(context.Module().value);
        return Result<void>::Success();
    }

    void LogDeactivate(ModuleActivationContext &context) noexcept {
        g_log.deactivated.push_back(context.Module().value);
    }

    [[nodiscard]] ModuleDescriptor MakeLoggedModule(std::string id, const ModuleContractVersion version = {1, 0, 0}) {
        ModuleDescriptor descriptor;
        descriptor.id = ModuleId{std::move(id)};
        descriptor.version = version;
        descriptor.lifecycle = ModuleLifecycleCallbacks{.activate = &LogActivate, .deactivate = &LogDeactivate};
        return descriptor;
    }

    [[nodiscard]] ModuleDescriptor MakeModule(std::string id, const ModuleContractVersion version = {1, 0, 0}) {
        ModuleDescriptor descriptor;
        descriptor.id = ModuleId{std::move(id)};
        descriptor.version = version;
        return descriptor;
    }
}  // namespace

TEST_CASE("Composition registers and activates modules in validated order", "[unit][foundation][modules][composition]") {
    ResetLog();
    ModuleHost host;

    ModuleDescriptor editor = MakeLoggedModule("horo.editor");
    editor.dependencies.push_back(ModuleDependency{.module = ModuleId{"horo.foundation"}});

    REQUIRE(host.Register(editor).HasValue());
    REQUIRE(host.Register(MakeLoggedModule("horo.foundation")).HasValue());

    // Registration is inert: no callback runs before ActivateRegistered.
    REQUIRE(g_log.activated.empty());

    const Result<std::size_t> activated = host.ActivateRegistered(nullptr);
    REQUIRE(activated.HasValue());
    REQUIRE(activated.Value() == 2);
    REQUIRE(host.HasActiveModules());
    REQUIRE(g_log.activated == std::vector<std::string>{"horo.foundation", "horo.editor"});
}

TEST_CASE("Failed composition rolls back active modules in reverse order", "[unit][foundation][modules][composition]") {
    ResetLog();
    g_failModule = "horo.failing";

    ModuleHost host;
    ModuleDescriptor failing = MakeLoggedModule("horo.failing");
    failing.dependencies.push_back(ModuleDependency{.module = ModuleId{"horo.second"}});
    ModuleDescriptor second = MakeLoggedModule("horo.second");
    second.dependencies.push_back(ModuleDependency{.module = ModuleId{"horo.first"}});

    REQUIRE(host.Register(MakeLoggedModule("horo.first")).HasValue());
    REQUIRE(host.Register(second).HasValue());
    REQUIRE(host.Register(failing).HasValue());

    const Result<std::size_t> activated = host.ActivateRegistered(nullptr);
    REQUIRE(activated.HasError());
    // The failed module never joined the active set; earlier modules were
    // deactivated in reverse activation order, leaving nothing partially active.
    REQUIRE(g_log.activated == std::vector<std::string>{"horo.first", "horo.second", "horo.failing"});
    REQUIRE(g_log.deactivated == std::vector<std::string>{"horo.second", "horo.first"});
    REQUIRE_FALSE(host.HasActiveModules());
}

TEST_CASE("Rejected composition leaves registrations untouched for retry", "[unit][foundation][modules][composition]") {
    ResetLog();
    ModuleHost host;
    ModuleDescriptor missingDependency = MakeModule("horo.consumer");
    missingDependency.dependencies.push_back(ModuleDependency{.module = ModuleId{"horo.absent"}});
    REQUIRE(host.Register(missingDependency).HasValue());

    // Graph validation fails before any callback: no module was activated.
    REQUIRE(host.ActivateRegistered(nullptr).HasError());
    REQUIRE(g_log.activated.empty());
    REQUIRE_FALSE(host.HasActiveModules());

    ModuleDescriptor provider = MakeModule("horo.absent");
    REQUIRE(host.Register(provider).HasValue());
    const Result<std::size_t> retried = host.ActivateRegistered(nullptr);
    REQUIRE(retried.HasValue());
    REQUIRE(retried.Value() == 2);
}

TEST_CASE("Headless composition never activates GUI-only modules", "[unit][foundation][modules][composition]") {
    ResetLog();
    ModuleHost host;

    // A GUI adapter requires a window surface capability no headless module provides.
    ModuleDescriptor renderNull = MakeModule("horo.render.null");
    renderNull.providedCapabilities.push_back(ModuleCapabilityId{"horo.render.device"});
    ModuleDescriptor gui = MakeModule("horo.gui");
    gui.requiredCapabilities.push_back(ModuleCapabilityId{"horo.window.surface"});
    gui.lifecycle = ModuleLifecycleCallbacks{.activate = &LogActivate, .deactivate = &LogDeactivate};

    REQUIRE(host.Register(renderNull).HasValue());
    REQUIRE(host.Register(gui).HasValue());
    REQUIRE(host.ActivateRegistered(nullptr).HasError());
    REQUIRE(g_log.activated.empty());

    // The headless composition root simply never registers the GUI descriptor.
    ModuleHost headless;
    REQUIRE(headless.Register(renderNull).HasValue());
    const Result<std::size_t> activated = headless.ActivateRegistered(nullptr);
    REQUIRE(activated.HasValue());
    REQUIRE(activated.Value() == 1);
}

TEST_CASE("Registration rejects duplicates and repeated dependencies", "[unit][foundation][modules][composition]") {
    ResetLog();
    ModuleHost host;
    REQUIRE(host.Register(MakeModule("horo.a")).HasValue());
    REQUIRE(host.Register(MakeModule("horo.a")).HasError());

    ModuleDescriptor repeated = MakeModule("horo.b");
    repeated.dependencies.push_back(ModuleDependency{.module = ModuleId{"horo.a"}});
    repeated.dependencies.push_back(ModuleDependency{.module = ModuleId{"horo.a"}});
    REQUIRE(host.Register(repeated).HasError());
}

TEST_CASE("Deactivation releases attached instances exactly once", "[unit][foundation][modules][composition]") {
    ResetLog();
    ModuleHost host;
    REQUIRE(host.Register(MakeLoggedModule("horo.owner")).HasValue());
    REQUIRE(host.ActivateRegistered(nullptr).HasValue());
    REQUIRE(g_instancesAlive > 0);

    host.DeactivateAll();
    REQUIRE(g_log.deactivated == std::vector<std::string>{"horo.owner"});
    REQUIRE(g_instancesAlive == 0);
    REQUIRE_FALSE(host.HasActiveModules());

    // Idempotent teardown.
    host.DeactivateAll();
    REQUIRE(g_instancesAlive == 0);
}
