#include "Horo/Foundation/ModuleDescriptor.h"

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <string_view>
#include <utility>
#include <vector>

namespace {
    using namespace Horo;

    int g_activateCalls = 0;
    int g_deactivateCalls = 0;

    Result<void> Activate(ModuleActivationContext &) noexcept {
        ++g_activateCalls;
        return Result<void>::Success();
    }

    void Deactivate(ModuleActivationContext &) noexcept {
        ++g_deactivateCalls;
    }

    [[nodiscard]] ModuleDescriptor MakeModule(std::string id, const ModuleContractVersion version = {1, 0, 0}) {
        ModuleDescriptor descriptor;
        descriptor.id = ModuleId{std::move(id)};
        descriptor.version = version;
        return descriptor;
    }

    [[nodiscard]] std::vector<std::string_view> OrderOf(const ValidatedModuleGraph &graph) {
        std::vector<std::string_view> order;
        order.reserve(graph.initializationOrder.size());
        for (const ModuleId &id : graph.initializationOrder)
            order.emplace_back(id.value);
        return order;
    }

    TEST_CASE("Module descriptors produce a deterministic provider-first graph", "[unit][foundation][modules]") {
        ModuleDescriptor editor = MakeModule("horo.editor");
        editor.dependencies.push_back(ModuleDependency{.module = ModuleId{"horo.runtime"}, .minimumVersion = {1, 2, 0}});
        editor.requiredCapabilities.push_back(ModuleCapabilityId{"horo.project.read"});
        editor.resourceBudgets.push_back(ModuleResourceBudget{.id = "horo.editor.background_jobs",
                                                              .kind = ModuleResourceBudgetKind::ConcurrentJobs,
                                                              .limit = 4,
                                                              .affinity = ModuleThreadAffinity::Worker});
        editor.resourceBudgets.push_back(ModuleResourceBudget{.id = "horo.editor.foreground_jobs",
                                                              .kind = ModuleResourceBudgetKind::ConcurrentJobs,
                                                              .limit = 1,
                                                              .affinity = ModuleThreadAffinity::Main});
        editor.observability.push_back(
            ModuleObservabilityDescriptor{.kind = ModuleObservabilityKind::LogCategory, .id = "horo.editor.lifecycle"});
        editor.lifecycle = ModuleLifecycleCallbacks{.activate = &Activate, .deactivate = &Deactivate};

        ModuleDescriptor application = MakeModule("horo.application");
        application.dependencies.push_back(ModuleDependency{.module = ModuleId{"horo.foundation"}});
        application.providedCapabilities.push_back(ModuleCapabilityId{"horo.project.read"});

        ModuleDescriptor runtime = MakeModule("horo.runtime", {1, 2, 1});
        runtime.dependencies.push_back(ModuleDependency{.module = ModuleId{"horo.foundation"}});
        runtime.dependencies.push_back(ModuleDependency{.module = ModuleId{"horo.render.null"}, .kind = ModuleDependencyKind::Optional});

        ModuleDescriptor renderNull = MakeModule("horo.render.null");
        renderNull.dependencies.push_back(ModuleDependency{.module = ModuleId{"horo.foundation"}});

        const std::array descriptors{editor, runtime, application, MakeModule("horo.foundation"), renderNull};
        g_activateCalls = 0;
        g_deactivateCalls = 0;

        const Result<ValidatedModuleGraph> result = ValidateModuleGraph(descriptors);

        REQUIRE(result.HasValue());
        REQUIRE(OrderOf(result.Value()) ==
                std::vector<std::string_view>{"horo.foundation", "horo.application", "horo.render.null", "horo.runtime", "horo.editor"});
        REQUIRE(g_activateCalls == 0);
        REQUIRE(g_deactivateCalls == 0);
    }

    TEST_CASE("Module descriptor graph rejects invalid inputs before composition", "[unit][foundation][modules]") {
        SECTION("duplicate module identity") {
            const std::array descriptors{MakeModule("horo.foundation"), MakeModule("horo.foundation")};
            const auto result = ValidateModuleGraph(descriptors);
            REQUIRE(result.HasError());
            REQUIRE(result.ErrorValue().code.Value() == "foundation.module.duplicate_identity");
        }

        SECTION("missing required dependency") {
            ModuleDescriptor module = MakeModule("horo.runtime");
            module.dependencies.push_back(ModuleDependency{.module = ModuleId{"horo.foundation"}});
            const std::array descriptors{module};
            const auto result = ValidateModuleGraph(descriptors);
            REQUIRE(result.HasError());
            REQUIRE(result.ErrorValue().code.Value() == "foundation.module.missing_dependency");
        }

        SECTION("outdated dependency contract") {
            ModuleDescriptor module = MakeModule("horo.runtime");
            module.dependencies.push_back(ModuleDependency{.module = ModuleId{"horo.foundation"}, .minimumVersion = {2, 0, 0}});
            const std::array descriptors{MakeModule("horo.foundation", {1, 9, 9}), module};
            const auto result = ValidateModuleGraph(descriptors);
            REQUIRE(result.HasError());
            REQUIRE(result.ErrorValue().code.Value() == "foundation.module.incompatible_dependency");
        }

        SECTION("present outdated optional dependency contract") {
            ModuleDescriptor module = MakeModule("horo.runtime");
            module.dependencies.push_back(ModuleDependency{.module = ModuleId{"horo.render.opengl"},
                                                           .minimumVersion = {2, 0, 0},
                                                           .kind = ModuleDependencyKind::Optional});
            const std::array descriptors{MakeModule("horo.render.opengl", {1, 9, 9}), module};
            const auto result = ValidateModuleGraph(descriptors);
            REQUIRE(result.HasError());
            REQUIRE(result.ErrorValue().code.Value() == "foundation.module.incompatible_dependency");
        }

        SECTION("missing required capability") {
            ModuleDescriptor module = MakeModule("horo.editor");
            module.requiredCapabilities.push_back(ModuleCapabilityId{"horo.project.read"});
            const std::array descriptors{module};
            const auto result = ValidateModuleGraph(descriptors);
            REQUIRE(result.HasError());
            REQUIRE(result.ErrorValue().code.Value() == "foundation.module.missing_capability");
        }

        SECTION("dependency cycle") {
            ModuleDescriptor left = MakeModule("horo.left");
            left.dependencies.push_back(ModuleDependency{.module = ModuleId{"horo.right"}});
            ModuleDescriptor right = MakeModule("horo.right");
            right.dependencies.push_back(ModuleDependency{.module = ModuleId{"horo.left"}});
            const std::array descriptors{left, right};
            const auto result = ValidateModuleGraph(descriptors);
            REQUIRE(result.HasError());
            REQUIRE(result.ErrorValue().code.Value() == "foundation.module.dependency_cycle");
        }

        SECTION("unpaired lifecycle callback") {
            ModuleDescriptor module = MakeModule("horo.runtime");
            module.lifecycle.activate = &Activate;
            const std::array descriptors{module};
            const auto result = ValidateModuleGraph(descriptors);
            REQUIRE(result.HasError());
            REQUIRE(result.ErrorValue().code.Value() == "foundation.module.invalid_descriptor");
        }
    }

    TEST_CASE("Optional absent modules do not invalidate a descriptor graph", "[unit][foundation][modules]") {
        ModuleDescriptor runtime = MakeModule("horo.runtime");
        runtime.dependencies.push_back(ModuleDependency{.module = ModuleId{"horo.render.opengl"},
                                                        .minimumVersion = {1, 0, 0},
                                                        .kind = ModuleDependencyKind::Optional});
        const std::array descriptors{runtime};

        const auto result = ValidateModuleGraph(descriptors);

        REQUIRE(result.HasValue());
        REQUIRE(OrderOf(result.Value()) == std::vector<std::string_view>{"horo.runtime"});
    }
}  // namespace
