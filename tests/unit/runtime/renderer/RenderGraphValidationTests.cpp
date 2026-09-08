#include "Horo/Runtime/Render/RenderGraph.h"
#include "Horo/Runtime/Render/RenderGraphErrors.h"
#include "RenderGraphTestUtils.h"

#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <string>
#include <thread>
#include <utility>

namespace {
    using namespace Horo;
    using namespace Horo::Render;
    using namespace Horo::Render::Test;

    const RenderGraphPassDisposition &RequireDisposition(const RenderGraphSchedule &schedule, const RenderGraphPassRef pass) {
        const auto found = std::find_if(schedule.PassDispositions().begin(), schedule.PassDispositions().end(),
                                        [pass](const RenderGraphPassDisposition &entry) {
            return entry.pass == pass;
        });
        REQUIRE(found != schedule.PassDispositions().end());
        return *found;
    }

    std::size_t CulledPassCount(const RenderGraphSchedule &schedule) {
        return static_cast<std::size_t>(std::count_if(schedule.PassDispositions().begin(), schedule.PassDispositions().end(),
                                                      [](const RenderGraphPassDisposition &entry) {
            return entry.disposition == RenderGraphPassDispositionKind::Culled;
        }));
    }
}  // namespace

TEST_CASE("Render graph compilation produces deterministic dependency order", "[runtime][renderer][render-graph]") {
    RenderGraphLimits limits{.maxPasses = 4, .maxResources = 1, .maxUsages = 1, .maxDependencies = 2};
    RenderGraphBuilder builder = RequireBuilder(limits);
    const auto first = RequirePass(builder, RenderPassKind::Graphics, RenderQueueRole::Graphics);
    const auto second = RequirePass(builder, RenderPassKind::Graphics, RenderQueueRole::Graphics);
    const auto third = RequirePass(builder, RenderPassKind::Graphics, RenderQueueRole::Graphics);
    const auto fourth = RequirePass(builder, RenderPassKind::Graphics, RenderQueueRole::Graphics);
    RequireDependency(builder, {third, first, RenderGraphDependencyKind::ExecutionOrder});
    RequireDependency(builder, {second, first, RenderGraphDependencyKind::ExecutionOrder});

    RenderGraph graph = RequireGraph(builder);
    RenderGraphSchedule schedule = RequireSchedule(graph);

    REQUIRE(schedule.Owner() == graph.Owner());
    REQUIRE(CulledPassCount(schedule) == 0);
    REQUIRE(schedule.OrderedPasses().size() == 4);
    REQUIRE(schedule.OrderedPasses()[0] == second);
    REQUIRE(schedule.OrderedPasses()[1] == third);
    REQUIRE(schedule.OrderedPasses()[2] == first);
    REQUIRE(schedule.OrderedPasses()[3] == fourth);

    RenderGraphSchedule movedSchedule{std::move(schedule)};
    REQUIRE_FALSE(schedule.Owner().IsValid());
    REQUIRE(movedSchedule.Owner() == graph.Owner());
    RenderGraphSchedule assignedSchedule = RequireSchedule(graph);
    assignedSchedule = std::move(movedSchedule);
    REQUIRE_FALSE(movedSchedule.Owner().IsValid());
    REQUIRE(assignedSchedule.Owner() == graph.Owner());
    assignedSchedule = std::move(assignedSchedule);
    REQUIRE(assignedSchedule.Owner() == graph.Owner());
    REQUIRE(assignedSchedule.OrderedPasses().size() == 4);
}

TEST_CASE("Render graph compilation gives unordered resource hazards a deterministic total order", "[runtime][renderer][render-graph]") {
    SECTION("writer and reader") {
        RenderGraphLimits limits{.maxPasses = 2, .maxResources = 2, .maxUsages = 3, .maxDependencies = 1};
        RenderGraphBuilder builder = RequireBuilder(limits);
        const auto writer = RequirePass(builder, RenderPassKind::Compute, RenderQueueRole::Compute);
        const auto reader = RequirePass(builder, RenderPassKind::Compute, RenderQueueRole::Compute);
        const auto intermediate = RequireResource(builder.AddTransientResource(RenderGraphResourceKind::Buffer));
        const auto output = RequireResource(builder.AddTransientResource(RenderGraphResourceKind::Buffer));
        RequireUsage(builder, {writer, intermediate, RenderGraphAccess::Write, RenderGraphUsageKind::Storage});
        RequireUsage(builder, {reader, intermediate, RenderGraphAccess::Read, RenderGraphUsageKind::Storage});
        RequireUsage(builder, {reader, output, RenderGraphAccess::Write, RenderGraphUsageKind::Storage});
        RequireExport(builder, output);

        RenderGraph graph = RequireGraph(builder);
        RenderGraphSchedule schedule = RequireSchedule(graph);
        REQUIRE(schedule.OrderedPasses().size() == 2);
        REQUIRE(schedule.OrderedPasses()[0] == writer);
        REQUIRE(schedule.OrderedPasses()[1] == reader);
    }

    SECTION("multiple writers") {
        RenderGraphLimits limits{.maxPasses = 2, .maxResources = 1, .maxUsages = 2, .maxDependencies = 1};
        RenderGraphBuilder builder = RequireBuilder(limits);
        const auto firstWriter = RequirePass(builder, RenderPassKind::Compute, RenderQueueRole::Compute);
        const auto secondWriter = RequirePass(builder, RenderPassKind::Compute, RenderQueueRole::Compute);
        const auto imported = RequireResource(builder.ImportBuffer(BufferHandle(), RenderGraphResourceClass::Persistent));
        RequireUsage(builder, {firstWriter, imported, RenderGraphAccess::Write, RenderGraphUsageKind::Storage});
        RequireUsage(builder, {secondWriter, imported, RenderGraphAccess::Write, RenderGraphUsageKind::Storage});

        RenderGraph graph = RequireGraph(builder);
        RenderGraphSchedule schedule = RequireSchedule(graph);
        REQUIRE(schedule.OrderedPasses().size() == 2);
        REQUIRE(schedule.OrderedPasses()[0] == firstWriter);
        REQUIRE(schedule.OrderedPasses()[1] == secondWriter);
    }
}

TEST_CASE("Render graph compilation rejects disconnected dependency cycles", "[runtime][renderer][render-graph]") {
    RenderGraphLimits limits{.maxPasses = 4, .maxResources = 1, .maxUsages = 1, .maxDependencies = 3};
    RenderGraphBuilder builder = RequireBuilder(limits);
    const auto first = RequirePass(builder, RenderPassKind::Graphics, RenderQueueRole::Graphics);
    const auto second = RequirePass(builder, RenderPassKind::Graphics, RenderQueueRole::Graphics);
    const auto third = RequirePass(builder, RenderPassKind::Graphics, RenderQueueRole::Graphics);
    RequirePass(builder, RenderPassKind::Graphics, RenderQueueRole::Graphics);
    RequireDependency(builder, {first, second, RenderGraphDependencyKind::ExecutionOrder});
    RequireDependency(builder, {second, third, RenderGraphDependencyKind::ResourceHazard});
    RequireDependency(builder, {third, first, RenderGraphDependencyKind::ExecutionOrder});
    RenderGraph graph = RequireGraph(builder);
    const auto rejected = CompileRenderGraph(graph);
    RequireError(rejected, "render.graph.dependency_cycle");
    REQUIRE(rejected.ErrorValue().message.find("pass IDs 1 2 3") != std::string::npos);
    REQUIRE(rejected.ErrorValue().message.find(" 4") == std::string::npos);
}

TEST_CASE("Render graph compilation rejects exact duplicate dependencies", "[runtime][renderer][render-graph]") {
    RenderGraphLimits limits{.maxPasses = 2, .maxResources = 1, .maxUsages = 1, .maxDependencies = 2};
    RenderGraphBuilder builder = RequireBuilder(limits);
    const auto first = RequirePass(builder, RenderPassKind::Graphics, RenderQueueRole::Graphics);
    const auto second = RequirePass(builder, RenderPassKind::Graphics, RenderQueueRole::Graphics);
    RequireDependency(builder, {first, second, RenderGraphDependencyKind::ExecutionOrder});
    RequireDependency(builder, {first, second, RenderGraphDependencyKind::ExecutionOrder});
    RenderGraph graph = RequireGraph(builder);
    const auto rejected = CompileRenderGraph(graph);
    RequireError(rejected, "render.graph.dependency_invalid");
    REQUIRE(rejected.ErrorValue().message.find("Pass 1 -> pass 2") != std::string::npos);
}

TEST_CASE("Render graph compilation preserves different dependency reasons", "[runtime][renderer][render-graph]") {
    RenderGraphLimits limits{.maxPasses = 2, .maxResources = 1, .maxUsages = 1, .maxDependencies = 2};
    RenderGraphBuilder builder = RequireBuilder(limits);
    const auto first = RequirePass(builder, RenderPassKind::Graphics, RenderQueueRole::Graphics);
    const auto second = RequirePass(builder, RenderPassKind::Graphics, RenderQueueRole::Graphics);
    RequireDependency(builder, {first, second, RenderGraphDependencyKind::ExecutionOrder});
    RequireDependency(builder, {first, second, RenderGraphDependencyKind::ResourceHazard});
    RenderGraph graph = RequireGraph(builder);
    REQUIRE(graph.Dependencies().size() == 2);
    REQUIRE(RequireSchedule(graph).OrderedPasses().size() == 2);
}

TEST_CASE("Render graph compilation rejects transient reads without a writer", "[runtime][renderer][render-graph]") {
    RenderGraphBuilder builder = RequireBuilder();
    const auto reader = RequirePass(builder, RenderPassKind::Compute, RenderQueueRole::Compute);
    const auto transient = RequireResource(builder.AddTransientResource(RenderGraphResourceKind::Buffer));
    RequireUsage(builder, {reader, transient, RenderGraphAccess::Read, RenderGraphUsageKind::Storage});
    RenderGraph graph = RequireGraph(builder);
    RequireError(CompileRenderGraph(graph), "render.graph.read_before_write");
}

TEST_CASE("Render graph compilation defers imported initial-state validation", "[runtime][renderer][render-graph]") {
    RenderGraphBuilder builder = RequireBuilder();
    const auto reader = RequirePass(builder, RenderPassKind::Compute, RenderQueueRole::Compute);
    const auto imported = RequireResource(builder.ImportBuffer(BufferHandle(), RenderGraphResourceClass::Persistent));
    RequireUsage(builder, {reader, imported, RenderGraphAccess::Read, RenderGraphUsageKind::Storage});
    RenderGraph graph = RequireGraph(builder);
    REQUIRE(RequireSchedule(graph).OrderedPasses().size() == 1);
}

TEST_CASE("Render graph compilation rejects producers ordered after readers", "[runtime][renderer][render-graph]") {
    RenderGraphLimits limits{.maxPasses = 2, .maxResources = 1, .maxUsages = 2, .maxDependencies = 1};
    RenderGraphBuilder builder = RequireBuilder(limits);
    const auto reader = RequirePass(builder, RenderPassKind::Compute, RenderQueueRole::Compute);
    const auto producer = RequirePass(builder, RenderPassKind::Compute, RenderQueueRole::Compute);
    const auto transient = RequireResource(builder.AddTransientResource(RenderGraphResourceKind::Buffer));
    RequireUsage(builder, {reader, transient, RenderGraphAccess::Read, RenderGraphUsageKind::Storage});
    RequireUsage(builder, {producer, transient, RenderGraphAccess::Write, RenderGraphUsageKind::Storage});
    RequireDependency(builder, {reader, producer, RenderGraphDependencyKind::ExecutionOrder});
    RenderGraph graph = RequireGraph(builder);
    RequireError(CompileRenderGraph(graph), "render.graph.read_before_write");
}

TEST_CASE("Render graph compilation rejects unwritten transient exports", "[runtime][renderer][render-graph]") {
    RenderGraphBuilder builder = RequireBuilder();
    RequirePass(builder, RenderPassKind::Graphics, RenderQueueRole::Graphics);
    const auto transient = RequireResource(builder.AddTransientResource(RenderGraphResourceKind::Texture));
    RequireExport(builder, transient);
    RenderGraph graph = RequireGraph(builder);
    RequireError(CompileRenderGraph(graph), "render.graph.read_before_write");
}

TEST_CASE("Render graph compilation culls transitive unused transient work", "[runtime][renderer][render-graph]") {
    RenderGraphLimits limits{.maxPasses = 4, .maxResources = 4, .maxUsages = 6, .maxDependencies = 2};
    RenderGraphBuilder builder = RequireBuilder(limits);
    const auto producer =
        RequirePass(builder, RenderPassKind::Compute, RenderQueueRole::Compute, RenderGraphPassCullPolicy::AllowCullIfOutputsUnused);
    const auto output = RequirePass(builder, RenderPassKind::Compute, RenderQueueRole::Compute);
    const auto dead =
        RequirePass(builder, RenderPassKind::Compute, RenderQueueRole::Compute, RenderGraphPassCullPolicy::AllowCullIfOutputsUnused);
    const auto deadTail =
        RequirePass(builder, RenderPassKind::Compute, RenderQueueRole::Compute, RenderGraphPassCullPolicy::AllowCullIfOutputsUnused);
    const auto intermediate = RequireResource(builder.AddTransientResource(RenderGraphResourceKind::Buffer));
    const auto exported = RequireResource(builder.AddTransientResource(RenderGraphResourceKind::Buffer));
    const auto unused = RequireResource(builder.AddTransientResource(RenderGraphResourceKind::Buffer));
    const auto deadTailOutput = RequireResource(builder.AddTransientResource(RenderGraphResourceKind::Buffer));
    RequireUsage(builder, {producer, intermediate, RenderGraphAccess::Write, RenderGraphUsageKind::Storage});
    RequireUsage(builder, {output, intermediate, RenderGraphAccess::Read, RenderGraphUsageKind::Storage});
    RequireUsage(builder, {output, exported, RenderGraphAccess::Write, RenderGraphUsageKind::Storage});
    RequireUsage(builder, {dead, unused, RenderGraphAccess::Write, RenderGraphUsageKind::Storage});
    RequireUsage(builder, {deadTail, unused, RenderGraphAccess::Read, RenderGraphUsageKind::Storage});
    RequireUsage(builder, {deadTail, deadTailOutput, RenderGraphAccess::Write, RenderGraphUsageKind::Storage});
    RequireDependency(builder, {producer, output, RenderGraphDependencyKind::ResourceHazard});
    RequireDependency(builder, {dead, deadTail, RenderGraphDependencyKind::ResourceHazard});
    RequireExport(builder, exported);

    RenderGraph graph = RequireGraph(builder);
    RenderGraphSchedule schedule = RequireSchedule(graph);
    REQUIRE(schedule.OrderedPasses().size() == 2);
    REQUIRE(schedule.OrderedPasses()[0] == producer);
    REQUIRE(schedule.OrderedPasses()[1] == output);
    REQUIRE(CulledPassCount(schedule) == 2);
    REQUIRE(RequireDisposition(schedule, dead).reason == RenderGraphPassDispositionReason::OnlyRequiredByCulledPasses);
    REQUIRE(RequireDisposition(schedule, deadTail).reason == RenderGraphPassDispositionReason::UnusedTransientOutputs);
    REQUIRE(RequireDisposition(schedule, producer).reason == RenderGraphPassDispositionReason::RequiredResourceProducer);
}

TEST_CASE("Render graph compilation conservatively retains non-transient and unknown effects", "[runtime][renderer][render-graph]") {
    RenderGraphLimits limits{.maxPasses = 3, .maxResources = 2, .maxUsages = 2, .maxDependencies = 1};
    RenderGraphBuilder builder = RequireBuilder(limits);
    const auto importedMutation = RequirePass(builder, RenderPassKind::Compute, RenderQueueRole::Compute);
    const auto unknownEffect = RequirePass(builder, RenderPassKind::Graphics, RenderQueueRole::Graphics);
    const auto defaultTransientWriter = RequirePass(builder, RenderPassKind::Compute, RenderQueueRole::Compute);
    const auto imported = RequireResource(builder.ImportBuffer(BufferHandle(), RenderGraphResourceClass::Persistent));
    const auto transient = RequireResource(builder.AddTransientResource(RenderGraphResourceKind::Buffer));
    RequireUsage(builder, {importedMutation, imported, RenderGraphAccess::Write, RenderGraphUsageKind::Storage});
    RequireUsage(builder, {defaultTransientWriter, transient, RenderGraphAccess::Write, RenderGraphUsageKind::Storage});

    RenderGraph graph = RequireGraph(builder);
    RenderGraphSchedule schedule = RequireSchedule(graph);
    REQUIRE(schedule.OrderedPasses().size() == 3);
    REQUIRE(schedule.OrderedPasses()[0] == importedMutation);
    REQUIRE(schedule.OrderedPasses()[1] == unknownEffect);
    REQUIRE(schedule.OrderedPasses()[2] == defaultTransientWriter);
    REQUIRE(CulledPassCount(schedule) == 0);
    REQUIRE(RequireDisposition(schedule, importedMutation).reason == RenderGraphPassDispositionReason::ImportedResourceMutation);
    REQUIRE(RequireDisposition(schedule, unknownEffect).reason == RenderGraphPassDispositionReason::ConservativePolicy);
    REQUIRE(RequireDisposition(schedule, defaultTransientWriter).reason == RenderGraphPassDispositionReason::ConservativePolicy);
}

TEST_CASE("Render graph compilation preserves explicit and external synchronization predecessors", "[runtime][renderer][render-graph]") {
    RenderGraphLimits limits{.maxPasses = 3, .maxResources = 2, .maxUsages = 2, .maxDependencies = 2};
    RenderGraphBuilder builder = RequireBuilder(limits);
    const auto preparation =
        RequirePass(builder, RenderPassKind::Compute, RenderQueueRole::Compute, RenderGraphPassCullPolicy::AllowCullIfOutputsUnused);
    const auto deadWriter = RequirePass(builder, RenderPassKind::Compute, RenderQueueRole::Compute);
    const auto synchronized = RequirePass(builder, RenderPassKind::Compute, RenderQueueRole::Compute);
    const auto firstResource = RequireResource(builder.AddTransientResource(RenderGraphResourceKind::Buffer));
    const auto secondResource = RequireResource(builder.AddTransientResource(RenderGraphResourceKind::Buffer));
    RequireUsage(builder, {preparation, firstResource, RenderGraphAccess::Write, RenderGraphUsageKind::Storage});
    RequireUsage(builder, {deadWriter, secondResource, RenderGraphAccess::Write, RenderGraphUsageKind::Storage});
    RequireDependency(builder, {preparation, synchronized, RenderGraphDependencyKind::ExecutionOrder});
    RequireDependency(builder, {synchronized, deadWriter, RenderGraphDependencyKind::ExternalSynchronization});

    RenderGraph graph = RequireGraph(builder);
    RenderGraphSchedule schedule = RequireSchedule(graph);
    REQUIRE(schedule.OrderedPasses().size() == 3);
    REQUIRE(CulledPassCount(schedule) == 0);
    REQUIRE(RequireDisposition(schedule, preparation).reason == RenderGraphPassDispositionReason::RequiredDependency);
    REQUIRE(RequireDisposition(schedule, deadWriter).reason == RenderGraphPassDispositionReason::ExternalSynchronization);
}

TEST_CASE("Render graph compilation is immutable and thread-neutral", "[runtime][renderer][render-graph]") {
    RenderGraphBuilder builder = RequireBuilder();
    RequirePass(builder, RenderPassKind::Graphics, RenderQueueRole::Graphics);
    RenderGraph graph = RequireGraph(builder);
    std::string error;
    std::size_t passCount = 0;
    std::thread worker{[&] {
        auto compiled = CompileRenderGraph(graph);
        if (compiled.HasError()) {
            error = compiled.ErrorValue().code.Value();
            return;
        }
        passCount = compiled.Value().OrderedPasses().size();
    }};
    worker.join();
    REQUIRE(error.empty());
    REQUIRE(passCount == 1);
    REQUIRE(graph.Passes().size() == 1);

    RenderGraph moved = std::move(graph);
    RequireError(CompileRenderGraph(graph), "render.graph.invalid");
    REQUIRE(CompileRenderGraph(moved).HasValue());
}

TEST_CASE("Render graph compilation is repeatable under concurrent readers", "[runtime][renderer][render-graph]") {
    RenderGraphBuilder builder = RequireBuilder();
    const auto pass = RequirePass(builder, RenderPassKind::Graphics, RenderQueueRole::Graphics);
    RenderGraph graph = RequireGraph(builder);
    REQUIRE(RequireSchedule(graph).OrderedPasses()[0] == pass);
    REQUIRE(RequireSchedule(graph).OrderedPasses()[0] == pass);

    constexpr std::size_t ReaderCount = 4;
    std::array<std::string, ReaderCount> errors;
    std::array<RenderGraphOwnerId, ReaderCount> owners;
    std::array<std::thread, ReaderCount> readers;
    for (std::size_t index = 0; index < ReaderCount; ++index) {
        readers[index] = std::thread{[&, index] {
            auto compiled = CompileRenderGraph(graph);
            if (compiled.HasError()) {
                errors[index] = compiled.ErrorValue().code.Value();
                return;
            }
            owners[index] = compiled.Value().Owner();
        }};
    }
    for (std::thread &reader : readers) {
        reader.join();
    }
    for (std::size_t index = 0; index < ReaderCount; ++index) {
        REQUIRE(errors[index].empty());
        REQUIRE(owners[index] == graph.Owner());
    }
}
