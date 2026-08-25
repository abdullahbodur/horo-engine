#include "Horo/Foundation/ModuleDescriptor.h"

#include <array>
#include <catch2/catch_test_macros.hpp>
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

    [[nodiscard]] std::vector<std::string> OrderOf(const ValidatedModuleGraph &graph) {
        std::vector<std::string> order;
        order.reserve(graph.initializationOrder.size());
        for (const ModuleId &id : graph.initializationOrder)
            order.push_back(id.value);
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
                std::vector<std::string>{"horo.foundation", "horo.application", "horo.render.null", "horo.runtime", "horo.editor"});
        REQUIRE(g_activateCalls == 0);
        REQUIRE(g_deactivateCalls == 0);
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
    }

    TEST_CASE("Module descriptor graph rejects missing capabilities and cycles", "[unit][foundation][modules]") {
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
    }

    TEST_CASE("Module descriptor graph rejects invalid local metadata", "[unit][foundation][modules]") {
        SECTION("unpaired lifecycle callback") {
            ModuleDescriptor module = MakeModule("horo.runtime");
            module.lifecycle.activate = &Activate;
            const std::array descriptors{module};
            const auto result = ValidateModuleGraph(descriptors);
            REQUIRE(result.HasError());
            REQUIRE(result.ErrorValue().code.Value() == "foundation.module.invalid_descriptor");
        }

        SECTION("resource budget outside module namespace") {
            ModuleDescriptor module = MakeModule("horo.runtime");
            module.resourceBudgets.push_back(
                ModuleResourceBudget{.id = "other.jobs", .kind = ModuleResourceBudgetKind::ConcurrentJobs, .limit = 1});
            const std::array descriptors{module};
            const auto result = ValidateModuleGraph(descriptors);
            REQUIRE(result.HasError());
            REQUIRE(result.ErrorValue().message == "Resource budget 'other.jobs' is not namespaced by module 'horo.runtime'.");
        }

        SECTION("non-canonical observability identity") {
            ModuleDescriptor module = MakeModule("horo.runtime");
            module.observability.push_back(
                ModuleObservabilityDescriptor{.kind = ModuleObservabilityKind::LogCategory, .id = "Horo.Runtime"});
            const std::array descriptors{module};
            const auto result = ValidateModuleGraph(descriptors);
            REQUIRE(result.HasError());
            REQUIRE(result.ErrorValue().message == "Module 'horo.runtime' has a non-canonical observability identity.");
        }
    }

    TEST_CASE("Module descriptor identities enforce canonical namespacing", "[unit][foundation][modules]") {
        SECTION("non-canonical module identity") {
            const ModuleDescriptor module = MakeModule("Horo.Runtime");
            const std::array descriptors{module};
            const auto result = ValidateModuleGraph(descriptors);
            REQUIRE(result.HasError());
            REQUIRE(result.ErrorValue().message == "Module identity is not canonical: 'Horo.Runtime'.");
        }

        SECTION("non-canonical resource budget identity") {
            ModuleDescriptor module = MakeModule("horo.runtime");
            module.resourceBudgets.push_back(
                ModuleResourceBudget{.id = "Horo.Runtime.Jobs", .kind = ModuleResourceBudgetKind::ConcurrentJobs, .limit = 1});
            const std::array descriptors{module};
            const auto result = ValidateModuleGraph(descriptors);
            REQUIRE(result.HasError());
            REQUIRE(result.ErrorValue().message == "Module 'horo.runtime' has a non-canonical resource budget identity.");
        }

        SECTION("observability identity outside module namespace") {
            ModuleDescriptor module = MakeModule("horo.runtime");
            module.observability.push_back(
                ModuleObservabilityDescriptor{.kind = ModuleObservabilityKind::LogCategory, .id = "other.lifecycle"});
            const std::array descriptors{module};
            const auto result = ValidateModuleGraph(descriptors);
            REQUIRE(result.HasError());
            REQUIRE(result.ErrorValue().message ==
                    "Observability descriptor 'other.lifecycle' is not namespaced by module 'horo.runtime'.");
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
        REQUIRE(OrderOf(result.Value()) == std::vector<std::string>{"horo.runtime"});
    }

    TEST_CASE("Module descriptor rejects self and duplicate dependencies", "[unit][foundation][modules]") {
        SECTION("self dependency rejection") {
            ModuleDescriptor module = MakeModule("horo.runtime");
            module.dependencies.push_back(ModuleDependency{.module = ModuleId{"horo.runtime"}});
            const auto result = ValidateModuleGraph(std::array{module});
            REQUIRE(result.HasError());
            REQUIRE(result.ErrorValue().message == "Module 'horo.runtime' depends on itself.");
        }

        SECTION("duplicate dependency rejection") {
            ModuleDescriptor module = MakeModule("horo.runtime");
            module.dependencies.push_back(ModuleDependency{.module = ModuleId{"horo.foundation"}});
            module.dependencies.push_back(ModuleDependency{.module = ModuleId{"horo.foundation"}});
            const auto result = ValidateModuleGraph(std::array{module});
            REQUIRE(result.HasError());
            REQUIRE(result.ErrorValue().message == "Module 'horo.runtime' repeats dependency 'horo.foundation'.");
        }

        SECTION("non-canonical dependency identity rejection") {
            ModuleDescriptor module = MakeModule("horo.runtime");
            module.dependencies.push_back(ModuleDependency{.module = ModuleId{"Horo.Foundation"}});
            const auto result = ValidateModuleGraph(std::array{module});
            REQUIRE(result.HasError());
            REQUIRE(result.ErrorValue().message == "Module 'horo.runtime' has a non-canonical dependency identity.");
        }
    }

    TEST_CASE("Module descriptor validates capability declarations", "[unit][foundation][modules]") {
        SECTION("non-canonical provided capability rejection") {
            ModuleDescriptor module = MakeModule("horo.runtime");
            module.providedCapabilities.push_back(ModuleCapabilityId{"Horo.Capability"});
            const auto result = ValidateModuleGraph(std::array{module});
            REQUIRE(result.HasError());
            REQUIRE(result.ErrorValue().message == "Module 'horo.runtime' has a non-canonical provided capability.");
        }

        SECTION("duplicate provided capability rejection") {
            ModuleDescriptor module = MakeModule("horo.runtime");
            module.providedCapabilities.push_back(ModuleCapabilityId{"horo.capability"});
            module.providedCapabilities.push_back(ModuleCapabilityId{"horo.capability"});
            const auto result = ValidateModuleGraph(std::array{module});
            REQUIRE(result.HasError());
            REQUIRE(result.ErrorValue().message == "Module 'horo.runtime' repeats provided capability 'horo.capability'.");
        }

        SECTION("non-canonical required capability rejection") {
            ModuleDescriptor module = MakeModule("horo.runtime");
            module.requiredCapabilities.push_back(ModuleCapabilityId{"Horo.Capability"});
            const auto result = ValidateModuleGraph(std::array{module});
            REQUIRE(result.HasError());
            REQUIRE(result.ErrorValue().message == "Module 'horo.runtime' has a non-canonical required capability.");
        }

        SECTION("duplicate required capability rejection") {
            ModuleDescriptor module = MakeModule("horo.runtime");
            module.requiredCapabilities.push_back(ModuleCapabilityId{"horo.capability"});
            module.requiredCapabilities.push_back(ModuleCapabilityId{"horo.capability"});
            const auto result = ValidateModuleGraph(std::array{module});
            REQUIRE(result.HasError());
            REQUIRE(result.ErrorValue().message == "Module 'horo.runtime' repeats required capability 'horo.capability'.");
        }

        SECTION("requiring a provided capability rejection") {
            ModuleDescriptor module = MakeModule("horo.runtime");
            module.providedCapabilities.push_back(ModuleCapabilityId{"horo.shared.service"});
            module.requiredCapabilities.push_back(ModuleCapabilityId{"horo.shared.service"});
            const auto result = ValidateModuleGraph(std::array{module});
            REQUIRE(result.HasError());
            REQUIRE(result.ErrorValue().message == "Module 'horo.runtime' cannot require a capability it provides.");
        }
    }

    TEST_CASE("Module descriptor validates resource budgets and observability", "[unit][foundation][modules]") {
        SECTION("zero limit resource budget rejection") {
            ModuleDescriptor module = MakeModule("horo.runtime");
            module.resourceBudgets.push_back(
                ModuleResourceBudget{.id = "horo.runtime.jobs", .kind = ModuleResourceBudgetKind::ConcurrentJobs, .limit = 0});
            const auto result = ValidateModuleGraph(std::array{module});
            REQUIRE(result.HasError());
            REQUIRE(result.ErrorValue().message == "Resource budget 'horo.runtime.jobs' has a zero limit.");
        }

        SECTION("duplicate resource budget rejection") {
            ModuleDescriptor module = MakeModule("horo.runtime");
            module.resourceBudgets.push_back(
                ModuleResourceBudget{.id = "horo.runtime.jobs", .kind = ModuleResourceBudgetKind::ConcurrentJobs, .limit = 2});
            module.resourceBudgets.push_back(
                ModuleResourceBudget{.id = "horo.runtime.jobs", .kind = ModuleResourceBudgetKind::ConcurrentJobs, .limit = 4});
            const auto result = ValidateModuleGraph(std::array{module});
            REQUIRE(result.HasError());
            REQUIRE(result.ErrorValue().message == "Module 'horo.runtime' repeats resource budget 'horo.runtime.jobs'.");
        }

        SECTION("duplicate observability descriptor rejection") {
            ModuleDescriptor module = MakeModule("horo.runtime");
            module.observability.push_back(
                ModuleObservabilityDescriptor{.kind = ModuleObservabilityKind::LogCategory, .id = "horo.runtime.log"});
            module.observability.push_back(
                ModuleObservabilityDescriptor{.kind = ModuleObservabilityKind::LogCategory, .id = "horo.runtime.log"});
            const auto result = ValidateModuleGraph(std::array{module});
            REQUIRE(result.HasError());
            REQUIRE(result.ErrorValue().message == "Module 'horo.runtime' repeats observability descriptor 'horo.runtime.log'.");
        }
    }

    TEST_CASE("Module descriptor canonical ID grammar and tie-breaking", "[unit][foundation][modules]") {
        SECTION("invalid canonical ID grammar variations") {
            for (const char *invalidId : {"", ".horo", "horo.", "horo..runtime", "horo_-_runtime", "-horo", "horo-", "horo space",
                                          "horo:runtime", "horo@runtime", "horo/runtime"}) {
                const ModuleDescriptor module = MakeModule(std::string(invalidId));
                const auto result = ValidateModuleGraph(std::array{module});
                REQUIRE(result.HasError());
            }
        }

        SECTION("deterministic alphabetical tie-breaking on independent modules") {
            const std::array descriptors{MakeModule("horo.gamma"), MakeModule("horo.alpha"), MakeModule("horo.beta")};
            const auto result = ValidateModuleGraph(descriptors);
            REQUIRE(result.HasValue());
            REQUIRE(OrderOf(result.Value()) == std::vector<std::string>{"horo.alpha", "horo.beta", "horo.gamma"});
        }
    }

    TEST_CASE("Empty module descriptor set validates successfully", "[unit][foundation][modules]") {
        const std::span<const ModuleDescriptor> emptyDescriptors{};
        const auto result = ValidateModuleGraph(emptyDescriptors);
        REQUIRE(result.HasValue());
        REQUIRE(result.Value().initializationOrder.empty());
    }

    TEST_CASE("Module contract and identifier comparison operators", "[unit][foundation][modules]") {
        SECTION("ModuleId comparisons") {
            const ModuleId a{"horo.a"};
            const ModuleId aCopy{"horo.a"};
            const ModuleId b{"horo.b"};
            REQUIRE(a == aCopy);
            REQUIRE(a != b);
            REQUIRE(a < b);
            REQUIRE(b > a);
            REQUIRE(a <= aCopy);
            REQUIRE(a <= b);
            REQUIRE(b >= a);
        }

        SECTION("ModuleCapabilityId comparisons") {
            const ModuleCapabilityId a{"horo.cap.a"};
            const ModuleCapabilityId aCopy{"horo.cap.a"};
            const ModuleCapabilityId b{"horo.cap.b"};
            REQUIRE(a == aCopy);
            REQUIRE(a != b);
            REQUIRE(a < b);
            REQUIRE(b > a);
            REQUIRE(a <= aCopy);
            REQUIRE(a <= b);
            REQUIRE(b >= a);
        }

        SECTION("ModuleContractVersion comparisons") {
            const ModuleContractVersion v100{1, 0, 0};
            const ModuleContractVersion v100Copy{1, 0, 0};
            const ModuleContractVersion v101{1, 0, 1};
            const ModuleContractVersion v110{1, 1, 0};
            const ModuleContractVersion v200{2, 0, 0};

            REQUIRE(v100 == v100Copy);
            REQUIRE(v100 != v101);
            REQUIRE(v100 < v101);
            REQUIRE(v101 < v110);
            REQUIRE(v110 < v200);
            REQUIRE(v200 > v110);
            REQUIRE(v100 <= v100Copy);
            REQUIRE(v100 <= v101);
            REQUIRE(v200 >= v110);
        }
    }

    TEST_CASE("Module lifecycle callbacks require both activate and deactivate", "[unit][foundation][modules]") {
        SECTION("only activate provided") {
            ModuleDescriptor module = MakeModule("horo.runtime");
            module.lifecycle.activate = &Activate;
            module.lifecycle.deactivate = nullptr;
            const auto result = ValidateModuleGraph(std::array{module});
            REQUIRE(result.HasError());
            REQUIRE(result.ErrorValue().code.Value() == "foundation.module.invalid_descriptor");
            REQUIRE(result.ErrorValue().message == "Module 'horo.runtime' must declare both lifecycle callbacks or neither.");
        }

        SECTION("only deactivate provided") {
            ModuleDescriptor module = MakeModule("horo.runtime");
            module.lifecycle.activate = nullptr;
            module.lifecycle.deactivate = &Deactivate;
            const auto result = ValidateModuleGraph(std::array{module});
            REQUIRE(result.HasError());
            REQUIRE(result.ErrorValue().code.Value() == "foundation.module.invalid_descriptor");
            REQUIRE(result.ErrorValue().message == "Module 'horo.runtime' must declare both lifecycle callbacks or neither.");
        }

        SECTION("neither callback provided") {
            ModuleDescriptor module = MakeModule("horo.runtime");
            module.lifecycle.activate = nullptr;
            module.lifecycle.deactivate = nullptr;
            const auto result = ValidateModuleGraph(std::array{module});
            REQUIRE(result.HasValue());
        }

        SECTION("both callbacks provided") {
            ModuleDescriptor module = MakeModule("horo.runtime");
            module.lifecycle.activate = &Activate;
            module.lifecycle.deactivate = &Deactivate;
            const auto result = ValidateModuleGraph(std::array{module});
            REQUIRE(result.HasValue());
        }
    }

    TEST_CASE("Module namespacing prefix validation edge cases", "[unit][foundation][modules]") {
        SECTION("budget ID equal to module ID without dot suffix") {
            ModuleDescriptor module = MakeModule("horo.runtime");
            module.resourceBudgets.push_back(
                ModuleResourceBudget{.id = "horo.runtime", .kind = ModuleResourceBudgetKind::MemoryBytes, .limit = 1024});
            const auto result = ValidateModuleGraph(std::array{module});
            REQUIRE(result.HasError());
            REQUIRE(result.ErrorValue().message == "Resource budget 'horo.runtime' is not namespaced by module 'horo.runtime'.");
        }

        SECTION("budget ID starting with module ID but without dot separator") {
            ModuleDescriptor module = MakeModule("horo.runtime");
            module.resourceBudgets.push_back(
                ModuleResourceBudget{.id = "horo.runtime_extra", .kind = ModuleResourceBudgetKind::QueueDepth, .limit = 16});
            const auto result = ValidateModuleGraph(std::array{module});
            REQUIRE(result.HasError());
            REQUIRE(result.ErrorValue().message == "Resource budget 'horo.runtime_extra' is not namespaced by module 'horo.runtime'.");
        }

        SECTION("observability ID equal to module ID without dot suffix") {
            ModuleDescriptor module = MakeModule("horo.runtime");
            module.observability.push_back(
                ModuleObservabilityDescriptor{.kind = ModuleObservabilityKind::MetricInstrument, .id = "horo.runtime"});
            const auto result = ValidateModuleGraph(std::array{module});
            REQUIRE(result.HasError());
            REQUIRE(result.ErrorValue().message == "Observability descriptor 'horo.runtime' is not namespaced by module 'horo.runtime'.");
        }

        SECTION("observability ID starting with module ID but without dot separator") {
            ModuleDescriptor module = MakeModule("horo.runtime");
            module.observability.push_back(
                ModuleObservabilityDescriptor{.kind = ModuleObservabilityKind::ProfilerZone, .id = "horo.runtime_update"});
            const auto result = ValidateModuleGraph(std::array{module});
            REQUIRE(result.HasError());
            REQUIRE(result.ErrorValue().message ==
                    "Observability descriptor 'horo.runtime_update' is not namespaced by module 'horo.runtime'.");
        }

        SECTION("different observability kinds with same namespaced ID are permitted") {
            ModuleDescriptor module = MakeModule("horo.runtime");
            module.observability.push_back(
                ModuleObservabilityDescriptor{.kind = ModuleObservabilityKind::LogCategory, .id = "horo.runtime.lifecycle"});
            module.observability.push_back(
                ModuleObservabilityDescriptor{.kind = ModuleObservabilityKind::MetricInstrument, .id = "horo.runtime.lifecycle"});
            module.observability.push_back(
                ModuleObservabilityDescriptor{.kind = ModuleObservabilityKind::ProfilerZone, .id = "horo.runtime.lifecycle"});
            module.observability.push_back(
                ModuleObservabilityDescriptor{.kind = ModuleObservabilityKind::DiagnosticBundleHook, .id = "horo.runtime.lifecycle"});
            const auto result = ValidateModuleGraph(std::array{module});
            REQUIRE(result.HasValue());
        }
    }

    TEST_CASE("Module descriptor complex graphs: multiple providers, optional dependencies, cycles", "[unit][foundation][modules]") {
        SECTION("multiple providers for same capability order before consumer") {
            ModuleDescriptor providerA = MakeModule("horo.provider.a");
            providerA.providedCapabilities.push_back(ModuleCapabilityId{"horo.shared_cap"});

            ModuleDescriptor providerB = MakeModule("horo.provider.b");
            providerB.providedCapabilities.push_back(ModuleCapabilityId{"horo.shared_cap"});

            ModuleDescriptor consumer = MakeModule("horo.consumer");
            consumer.requiredCapabilities.push_back(ModuleCapabilityId{"horo.shared_cap"});

            const auto result = ValidateModuleGraph(std::array{consumer, providerB, providerA});
            REQUIRE(result.HasValue());
            const auto order = OrderOf(result.Value());
            REQUIRE(order == std::vector<std::string>{"horo.provider.a", "horo.provider.b", "horo.consumer"});
        }

        SECTION("present optional dependency participates in ordering") {
            ModuleDescriptor dependant = MakeModule("horo.dependant");
            dependant.dependencies.push_back(ModuleDependency{.module = ModuleId{"horo.optional_provider"},
                                                              .minimumVersion = {1, 0, 0},
                                                              .kind = ModuleDependencyKind::Optional});
            ModuleDescriptor provider = MakeModule("horo.optional_provider");

            const auto result = ValidateModuleGraph(std::array{dependant, provider});
            REQUIRE(result.HasValue());
            REQUIRE(OrderOf(result.Value()) == std::vector<std::string>{"horo.optional_provider", "horo.dependant"});
        }

        SECTION("redundant dependency and capability edge does not duplicate indegree") {
            ModuleDescriptor provider = MakeModule("horo.provider");
            provider.providedCapabilities.push_back(ModuleCapabilityId{"horo.service"});

            ModuleDescriptor dependant = MakeModule("horo.dependant");
            dependant.dependencies.push_back(ModuleDependency{.module = ModuleId{"horo.provider"}});
            dependant.requiredCapabilities.push_back(ModuleCapabilityId{"horo.service"});

            const auto result = ValidateModuleGraph(std::array{dependant, provider});
            REQUIRE(result.HasValue());
            REQUIRE(OrderOf(result.Value()) == std::vector<std::string>{"horo.provider", "horo.dependant"});
        }

        SECTION("three module dependency cycle") {
            ModuleDescriptor a = MakeModule("horo.a");
            a.dependencies.push_back(ModuleDependency{.module = ModuleId{"horo.b"}});
            ModuleDescriptor b = MakeModule("horo.b");
            b.dependencies.push_back(ModuleDependency{.module = ModuleId{"horo.c"}});
            ModuleDescriptor c = MakeModule("horo.c");
            c.dependencies.push_back(ModuleDependency{.module = ModuleId{"horo.a"}});

            const auto result = ValidateModuleGraph(std::array{a, b, c});
            REQUIRE(result.HasError());
            REQUIRE(result.ErrorValue().code.Value() == "foundation.module.dependency_cycle");
        }

        SECTION("diamond dependency graph") {
            ModuleDescriptor root = MakeModule("horo.root");
            ModuleDescriptor left = MakeModule("horo.left");
            left.dependencies.push_back(ModuleDependency{.module = ModuleId{"horo.root"}});
            ModuleDescriptor right = MakeModule("horo.right");
            right.dependencies.push_back(ModuleDependency{.module = ModuleId{"horo.root"}});
            ModuleDescriptor leaf = MakeModule("horo.leaf");
            leaf.dependencies.push_back(ModuleDependency{.module = ModuleId{"horo.left"}});
            leaf.dependencies.push_back(ModuleDependency{.module = ModuleId{"horo.right"}});

            const auto result = ValidateModuleGraph(std::array{leaf, right, left, root});
            REQUIRE(result.HasValue());
            REQUIRE(OrderOf(result.Value()) == std::vector<std::string>{"horo.root", "horo.left", "horo.right", "horo.leaf"});
        }
    }

    TEST_CASE("Module resource budget kinds and thread affinities", "[unit][foundation][modules]") {
        ModuleDescriptor module = MakeModule("horo.foundation");
        module.resourceBudgets.push_back(ModuleResourceBudget{.id = "horo.foundation.mem",
                                                              .kind = ModuleResourceBudgetKind::MemoryBytes,
                                                              .limit = 1024,
                                                              .affinity = ModuleThreadAffinity::Any});
        module.resourceBudgets.push_back(ModuleResourceBudget{.id = "horo.foundation.queue",
                                                              .kind = ModuleResourceBudgetKind::QueueDepth,
                                                              .limit = 32,
                                                              .affinity = ModuleThreadAffinity::Main});
        module.resourceBudgets.push_back(ModuleResourceBudget{.id = "horo.foundation.jobs",
                                                              .kind = ModuleResourceBudgetKind::ConcurrentJobs,
                                                              .limit = 8,
                                                              .affinity = ModuleThreadAffinity::Worker});
        module.resourceBudgets.push_back(ModuleResourceBudget{.id = "horo.foundation.threads",
                                                              .kind = ModuleResourceBudgetKind::DedicatedThreads,
                                                              .limit = 2,
                                                              .affinity = ModuleThreadAffinity::Render});
        module.resourceBudgets.push_back(ModuleResourceBudget{.id = "horo.foundation.capture",
                                                              .kind = ModuleResourceBudgetKind::CaptureBytesPerSecond,
                                                              .limit = 65536,
                                                              .affinity = ModuleThreadAffinity::Io});

        const auto result = ValidateModuleGraph(std::array{module});
        REQUIRE(result.HasValue());
    }
}  // namespace
