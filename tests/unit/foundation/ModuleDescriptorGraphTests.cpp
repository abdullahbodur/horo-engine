#include "Horo/Foundation/ModuleDescriptor.h"
#include "ModuleDescriptorTestUtils.h"

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <utility>
#include <vector>

namespace {
    using namespace Horo;
    using Horo::Test::MakeModule;

    [[nodiscard]] std::vector<std::string> OrderOf(const ValidatedModuleGraph &graph) {
        std::vector<std::string> order;
        order.reserve(graph.initializationOrder.size());
        for (const ModuleId &id : graph.initializationOrder)
            order.push_back(id.value);
        return order;
    }

    template <std::size_t N>
    void RequireOrder(const std::array<ModuleDescriptor, N> &descriptors, const std::vector<std::string> &expectedOrder) {
        const auto result = ValidateModuleGraph(descriptors);
        REQUIRE(result.HasValue());
        REQUIRE(OrderOf(result.Value()) == expectedOrder);
    }

    TEST_CASE("Module descriptors produce a deterministic provider-first graph", "[unit][foundation][modules]") {
        ModuleDescriptor editor = MakeModule("horo.editor");
        editor.dependencies.push_back(ModuleDependency{.module = ModuleId{"horo.runtime"}, .minimumVersion = {1, 2, 0}});
        editor.requiredCapabilities.push_back(ModuleCapabilityId{"horo.project.read"});

        ModuleDescriptor application = MakeModule("horo.application");
        application.dependencies.push_back(ModuleDependency{.module = ModuleId{"horo.foundation"}});
        application.providedCapabilities.push_back(ModuleCapabilityId{"horo.project.read"});

        ModuleDescriptor runtime = MakeModule("horo.runtime", {1, 2, 1});
        runtime.dependencies.push_back(ModuleDependency{.module = ModuleId{"horo.foundation"}});
        runtime.dependencies.push_back(ModuleDependency{.module = ModuleId{"horo.render.null"}, .kind = ModuleDependencyKind::Optional});

        ModuleDescriptor renderNull = MakeModule("horo.render.null");
        renderNull.dependencies.push_back(ModuleDependency{.module = ModuleId{"horo.foundation"}});

        const std::array descriptors{editor, runtime, application, MakeModule("horo.foundation"), renderNull};
        RequireOrder(descriptors, {"horo.foundation", "horo.application", "horo.render.null", "horo.runtime", "horo.editor"});
    }

    TEST_CASE("Module descriptor graph rejects invalid module dependencies", "[unit][foundation][modules]") {
        SECTION("duplicate module identity") {
            const std::array descriptors{MakeModule("horo.foundation"), MakeModule("horo.foundation")};
            const auto result = ValidateModuleGraph(descriptors);
            REQUIRE(result.HasError());
            REQUIRE(result.ErrorValue().code.Value() == "foundation.module.duplicate_identity");
        }

        SECTION("missing required dependency") {
            ModuleDescriptor module = MakeModule("horo.runtime");
            module.dependencies.push_back(ModuleDependency{.module = ModuleId{"horo.foundation"}});
            const auto result = ValidateModuleGraph(std::array{module});
            REQUIRE(result.HasError());
            REQUIRE(result.ErrorValue().code.Value() == "foundation.module.missing_dependency");
        }

        SECTION("outdated required or optional dependency contract") {
            ModuleDescriptor req = MakeModule("horo.runtime");
            req.dependencies.push_back(ModuleDependency{.module = ModuleId{"horo.foundation"}, .minimumVersion = {2, 0, 0}});
            REQUIRE(ValidateModuleGraph(std::array{MakeModule("horo.foundation", {1, 9, 9}), req}).HasError());

            ModuleDescriptor opt = MakeModule("horo.runtime");
            opt.dependencies.push_back(ModuleDependency{.module = ModuleId{"horo.render.opengl"},
                                                        .minimumVersion = {2, 0, 0},
                                                        .kind = ModuleDependencyKind::Optional});
            REQUIRE(ValidateModuleGraph(std::array{MakeModule("horo.render.opengl", {1, 9, 9}), opt}).HasError());
        }
    }

    TEST_CASE("Module descriptor graph validates required capabilities", "[unit][foundation][modules]") {
        SECTION("missing required capability") {
            ModuleDescriptor module = MakeModule("horo.editor");
            module.requiredCapabilities.push_back(ModuleCapabilityId{"horo.project.read"});
            const auto result = ValidateModuleGraph(std::array{module});
            REQUIRE(result.HasError());
            REQUIRE(result.ErrorValue().code.Value() == "foundation.module.missing_capability");
        }

        SECTION("multiple providers for same capability order before consumer") {
            ModuleDescriptor providerA = MakeModule("horo.provider.a");
            providerA.providedCapabilities.push_back(ModuleCapabilityId{"horo.shared_cap"});
            ModuleDescriptor providerB = MakeModule("horo.provider.b");
            providerB.providedCapabilities.push_back(ModuleCapabilityId{"horo.shared_cap"});
            ModuleDescriptor consumer = MakeModule("horo.consumer");
            consumer.requiredCapabilities.push_back(ModuleCapabilityId{"horo.shared_cap"});

            RequireOrder(std::array{consumer, providerB, providerA}, {"horo.provider.a", "horo.provider.b", "horo.consumer"});
        }
    }

    TEST_CASE("Module descriptor graph resolves optional and redundant edges", "[unit][foundation][modules]") {
        SECTION("present optional dependency participates in ordering") {
            ModuleDescriptor dependant = MakeModule("horo.dependant");
            dependant.dependencies.push_back(ModuleDependency{.module = ModuleId{"horo.optional_provider"},
                                                              .minimumVersion = {1, 0, 0},
                                                              .kind = ModuleDependencyKind::Optional});
            ModuleDescriptor provider = MakeModule("horo.optional_provider");
            RequireOrder(std::array{dependant, provider}, {"horo.optional_provider", "horo.dependant"});
        }

        SECTION("absent optional dependency does not fail validation") {
            ModuleDescriptor runtime = MakeModule("horo.runtime");
            runtime.dependencies.push_back(ModuleDependency{.module = ModuleId{"horo.render.opengl"},
                                                            .minimumVersion = {1, 0, 0},
                                                            .kind = ModuleDependencyKind::Optional});
            RequireOrder(std::array{runtime}, {"horo.runtime"});
        }

        SECTION("redundant dependency and capability edge does not duplicate indegree") {
            ModuleDescriptor provider = MakeModule("horo.provider");
            provider.providedCapabilities.push_back(ModuleCapabilityId{"horo.service"});
            ModuleDescriptor dependant = MakeModule("horo.dependant");
            dependant.dependencies.push_back(ModuleDependency{.module = ModuleId{"horo.provider"}});
            dependant.requiredCapabilities.push_back(ModuleCapabilityId{"horo.service"});

            RequireOrder(std::array{dependant, provider}, {"horo.provider", "horo.dependant"});
        }
    }

    TEST_CASE("Module descriptor graph detects cycles and orders DAGs", "[unit][foundation][modules]") {
        SECTION("two and three module dependency cycles") {
            ModuleDescriptor left = MakeModule("horo.left");
            left.dependencies.push_back(ModuleDependency{.module = ModuleId{"horo.right"}});
            ModuleDescriptor right = MakeModule("horo.right");
            right.dependencies.push_back(ModuleDependency{.module = ModuleId{"horo.left"}});
            REQUIRE(ValidateModuleGraph(std::array{left, right}).HasError());

            ModuleDescriptor a = MakeModule("horo.a");
            a.dependencies.push_back(ModuleDependency{.module = ModuleId{"horo.b"}});
            ModuleDescriptor b = MakeModule("horo.b");
            b.dependencies.push_back(ModuleDependency{.module = ModuleId{"horo.c"}});
            ModuleDescriptor c = MakeModule("horo.c");
            c.dependencies.push_back(ModuleDependency{.module = ModuleId{"horo.a"}});
            REQUIRE(ValidateModuleGraph(std::array{a, b, c}).HasError());
        }

        SECTION("diamond dependency DAG and tie-breaking") {
            ModuleDescriptor root = MakeModule("horo.root");
            ModuleDescriptor left = MakeModule("horo.left");
            left.dependencies.push_back(ModuleDependency{.module = ModuleId{"horo.root"}});
            ModuleDescriptor right = MakeModule("horo.right");
            right.dependencies.push_back(ModuleDependency{.module = ModuleId{"horo.root"}});
            ModuleDescriptor leaf = MakeModule("horo.leaf");
            leaf.dependencies.push_back(ModuleDependency{.module = ModuleId{"horo.left"}});
            leaf.dependencies.push_back(ModuleDependency{.module = ModuleId{"horo.right"}});

            RequireOrder(std::array{leaf, right, left, root}, {"horo.root", "horo.left", "horo.right", "horo.leaf"});

            const std::array independent{MakeModule("horo.gamma"), MakeModule("horo.alpha"), MakeModule("horo.beta")};
            RequireOrder(independent, {"horo.alpha", "horo.beta", "horo.gamma"});
        }
    }
}  // namespace
