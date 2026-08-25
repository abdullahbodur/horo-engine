#include "Horo/Foundation/ModuleDescriptor.h"
#include "ModuleDescriptorTestUtils.h"

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <utility>

namespace {
    using namespace Horo;
    using Horo::Test::MakeModule;

    int g_activateCalls = 0;
    int g_deactivateCalls = 0;

    Result<void> Activate(ModuleActivationContext &) noexcept {
        ++g_activateCalls;
        return Result<void>::Success();
    }

    void Deactivate(ModuleActivationContext &) noexcept {
        ++g_deactivateCalls;
    }

    TEST_CASE("Empty module descriptor set validates successfully", "[unit][foundation][modules]") {
        const std::span<const ModuleDescriptor> emptyDescriptors{};
        const auto result = ValidateModuleGraph(emptyDescriptors);
        REQUIRE(result.HasValue());
        REQUIRE(result.Value().initializationOrder.empty());
    }

    TEST_CASE("Module identifier comparison operators", "[unit][foundation][modules]") {
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
    }

    TEST_CASE("ModuleContractVersion comparison operators", "[unit][foundation][modules]") {
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
        REQUIRE(v200 >= v110);
    }

    TEST_CASE("Module lifecycle callbacks require pairing", "[unit][foundation][modules]") {
        SECTION("only activate provided") {
            ModuleDescriptor module = MakeModule("horo.runtime");
            module.lifecycle.activate = &Activate;
            const auto result = ValidateModuleGraph(std::array{module});
            REQUIRE(result.HasError());
            REQUIRE(result.ErrorValue().code.Value() == "foundation.module.invalid_descriptor");
        }

        SECTION("only deactivate provided") {
            ModuleDescriptor module = MakeModule("horo.runtime");
            module.lifecycle.deactivate = &Deactivate;
            const auto result = ValidateModuleGraph(std::array{module});
            REQUIRE(result.HasError());
            REQUIRE(result.ErrorValue().code.Value() == "foundation.module.invalid_descriptor");
        }

        SECTION("both or neither callbacks provided") {
            ModuleDescriptor module = MakeModule("horo.runtime");
            REQUIRE(ValidateModuleGraph(std::array{module}).HasValue());
            module.lifecycle.activate = &Activate;
            module.lifecycle.deactivate = &Deactivate;
            REQUIRE(ValidateModuleGraph(std::array{module}).HasValue());
        }
    }

    TEST_CASE("Module descriptor validates dependency declarations", "[unit][foundation][modules]") {
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
        SECTION("non-canonical or duplicate provided capability") {
            ModuleDescriptor nonCanonical = MakeModule("horo.runtime");
            nonCanonical.providedCapabilities.push_back(ModuleCapabilityId{"Horo.Capability"});
            REQUIRE(ValidateModuleGraph(std::array{nonCanonical}).HasError());

            ModuleDescriptor duplicate = MakeModule("horo.runtime");
            duplicate.providedCapabilities.push_back(ModuleCapabilityId{"horo.capability"});
            duplicate.providedCapabilities.push_back(ModuleCapabilityId{"horo.capability"});
            REQUIRE(ValidateModuleGraph(std::array{duplicate}).HasError());
        }

        SECTION("non-canonical or duplicate required capability") {
            ModuleDescriptor nonCanonical = MakeModule("horo.runtime");
            nonCanonical.requiredCapabilities.push_back(ModuleCapabilityId{"Horo.Capability"});
            REQUIRE(ValidateModuleGraph(std::array{nonCanonical}).HasError());

            ModuleDescriptor duplicate = MakeModule("horo.runtime");
            duplicate.requiredCapabilities.push_back(ModuleCapabilityId{"horo.capability"});
            duplicate.requiredCapabilities.push_back(ModuleCapabilityId{"horo.capability"});
            REQUIRE(ValidateModuleGraph(std::array{duplicate}).HasError());
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

    TEST_CASE("Module descriptor validates resource budgets", "[unit][foundation][modules]") {
        SECTION("zero limit or duplicate resource budget") {
            ModuleDescriptor zeroLimit = MakeModule("horo.runtime");
            zeroLimit.resourceBudgets.push_back(
                ModuleResourceBudget{.id = "horo.runtime.jobs", .kind = ModuleResourceBudgetKind::ConcurrentJobs, .limit = 0});
            REQUIRE(ValidateModuleGraph(std::array{zeroLimit}).HasError());

            ModuleDescriptor duplicate = MakeModule("horo.runtime");
            duplicate.resourceBudgets.push_back(
                ModuleResourceBudget{.id = "horo.runtime.jobs", .kind = ModuleResourceBudgetKind::ConcurrentJobs, .limit = 2});
            duplicate.resourceBudgets.push_back(
                ModuleResourceBudget{.id = "horo.runtime.jobs", .kind = ModuleResourceBudgetKind::ConcurrentJobs, .limit = 4});
            REQUIRE(ValidateModuleGraph(std::array{duplicate}).HasError());
        }

        SECTION("budget ID namespace prefix checks") {
            ModuleDescriptor exactMatch = MakeModule("horo.runtime");
            exactMatch.resourceBudgets.push_back(
                ModuleResourceBudget{.id = "horo.runtime", .kind = ModuleResourceBudgetKind::MemoryBytes, .limit = 1024});
            REQUIRE(ValidateModuleGraph(std::array{exactMatch}).HasError());

            ModuleDescriptor nonDotPrefix = MakeModule("horo.runtime");
            nonDotPrefix.resourceBudgets.push_back(
                ModuleResourceBudget{.id = "horo.runtime_extra", .kind = ModuleResourceBudgetKind::QueueDepth, .limit = 16});
            REQUIRE(ValidateModuleGraph(std::array{nonDotPrefix}).HasError());
        }
    }

    TEST_CASE("Module descriptor validates observability descriptors", "[unit][foundation][modules]") {
        SECTION("duplicate or non-namespaced observability") {
            ModuleDescriptor duplicate = MakeModule("horo.runtime");
            duplicate.observability.push_back(
                ModuleObservabilityDescriptor{.kind = ModuleObservabilityKind::LogCategory, .id = "horo.runtime.log"});
            duplicate.observability.push_back(
                ModuleObservabilityDescriptor{.kind = ModuleObservabilityKind::LogCategory, .id = "horo.runtime.log"});
            REQUIRE(ValidateModuleGraph(std::array{duplicate}).HasError());

            ModuleDescriptor foreign = MakeModule("horo.runtime");
            foreign.observability.push_back(ModuleObservabilityDescriptor{.kind = ModuleObservabilityKind::LogCategory, .id = "other.log"});
            REQUIRE(ValidateModuleGraph(std::array{foreign}).HasError());
        }

        SECTION("different observability kinds with same namespaced ID") {
            ModuleDescriptor module = MakeModule("horo.runtime");
            module.observability.push_back(
                ModuleObservabilityDescriptor{.kind = ModuleObservabilityKind::LogCategory, .id = "horo.runtime.lifecycle"});
            module.observability.push_back(
                ModuleObservabilityDescriptor{.kind = ModuleObservabilityKind::MetricInstrument, .id = "horo.runtime.lifecycle"});
            module.observability.push_back(
                ModuleObservabilityDescriptor{.kind = ModuleObservabilityKind::ProfilerZone, .id = "horo.runtime.lifecycle"});
            module.observability.push_back(
                ModuleObservabilityDescriptor{.kind = ModuleObservabilityKind::DiagnosticBundleHook, .id = "horo.runtime.lifecycle"});
            REQUIRE(ValidateModuleGraph(std::array{module}).HasValue());
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

        REQUIRE(ValidateModuleGraph(std::array{module}).HasValue());
    }

    TEST_CASE("Module descriptor canonical ID grammar validation", "[unit][foundation][modules]") {
        for (const char *invalidId : {"", ".horo", "horo.", "horo..runtime", "horo_-_runtime", "-horo", "horo-", "horo space",
                                      "horo:runtime", "horo@runtime", "horo/runtime"}) {
            const ModuleDescriptor module = MakeModule(std::string(invalidId));
            const auto result = ValidateModuleGraph(std::array{module});
            REQUIRE(result.HasError());
        }
    }
}  // namespace
