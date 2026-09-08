#include "Horo/Runtime/Render/RenderGraph.h"
#include "Horo/Runtime/Render/RenderGraphErrors.h"

#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

namespace {
    using namespace Horo;
    using namespace Horo::Render;

    template <typename T> void RequireError(const Result<T> &result, const std::string_view code) {
        REQUIRE(result.HasError());
        REQUIRE(result.ErrorValue().code.Value() == code);
        REQUIRE_FALSE(result.ErrorValue().message.empty());
    }

    RenderGraphLimits ValidationLimits() {
        return {.maxPasses = 3, .maxResources = 2, .maxUsages = 3, .maxDependencies = 2};
    }

    RenderBufferHandle BufferHandle(const std::uint32_t slot = 1) {
        return {{41}, slot, 1};
    }

    RenderGraphBuilder RequireBuilder(const RenderGraphLimits &limits = ValidationLimits()) {
        auto created = RenderGraphBuilder::Create(limits);
        REQUIRE(created.HasValue());
        return std::move(created).Value();
    }

    RenderGraph RequireGraph(RenderGraphBuilder &builder) {
        auto finalized = builder.Finalize();
        REQUIRE(finalized.HasValue());
        return std::move(finalized).Value();
    }

    RenderGraphSchedule RequireSchedule(const RenderGraph &graph) {
        auto compiled = CompileRenderGraph(graph);
        REQUIRE(compiled.HasValue());
        return std::move(compiled).Value();
    }

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
    const auto first = builder.AddPass(RenderPassKind::Graphics, RenderQueueRole::Graphics);
    const auto second = builder.AddPass(RenderPassKind::Graphics, RenderQueueRole::Graphics);
    const auto third = builder.AddPass(RenderPassKind::Graphics, RenderQueueRole::Graphics);
    const auto fourth = builder.AddPass(RenderPassKind::Graphics, RenderQueueRole::Graphics);
    REQUIRE(first.HasValue());
    REQUIRE(second.HasValue());
    REQUIRE(third.HasValue());
    REQUIRE(fourth.HasValue());
    REQUIRE(builder.AddDependency({third.Value(), first.Value(), RenderGraphDependencyKind::ExecutionOrder}).HasValue());
    REQUIRE(builder.AddDependency({second.Value(), first.Value(), RenderGraphDependencyKind::ExecutionOrder}).HasValue());

    auto finalized = builder.Finalize();
    REQUIRE(finalized.HasValue());
    RenderGraph graph = std::move(finalized).Value();
    auto compiled = CompileRenderGraph(graph);
    REQUIRE(compiled.HasValue());
    RenderGraphSchedule schedule = std::move(compiled).Value();

    REQUIRE(schedule.Owner() == graph.Owner());
    REQUIRE(CulledPassCount(schedule) == 0);
    REQUIRE(schedule.OrderedPasses().size() == 4);
    REQUIRE(schedule.OrderedPasses()[0] == second.Value());
    REQUIRE(schedule.OrderedPasses()[1] == third.Value());
    REQUIRE(schedule.OrderedPasses()[2] == first.Value());
    REQUIRE(schedule.OrderedPasses()[3] == fourth.Value());

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
        const auto writer = builder.AddPass(RenderPassKind::Compute, RenderQueueRole::Compute);
        const auto reader = builder.AddPass(RenderPassKind::Compute, RenderQueueRole::Compute);
        const auto intermediate = builder.AddTransientResource(RenderGraphResourceKind::Buffer);
        const auto output = builder.AddTransientResource(RenderGraphResourceKind::Buffer);
        REQUIRE(writer.HasValue());
        REQUIRE(reader.HasValue());
        REQUIRE(intermediate.HasValue());
        REQUIRE(output.HasValue());
        REQUIRE(
            builder.AddUsage({writer.Value(), intermediate.Value(), RenderGraphAccess::Write, RenderGraphUsageKind::Storage}).HasValue());
        REQUIRE(
            builder.AddUsage({reader.Value(), intermediate.Value(), RenderGraphAccess::Read, RenderGraphUsageKind::Storage}).HasValue());
        REQUIRE(builder.AddUsage({reader.Value(), output.Value(), RenderGraphAccess::Write, RenderGraphUsageKind::Storage}).HasValue());
        REQUIRE(builder.ExportResource(output.Value()).HasValue());

        RenderGraph graph = RequireGraph(builder);
        RenderGraphSchedule schedule = RequireSchedule(graph);
        REQUIRE(schedule.OrderedPasses().size() == 2);
        REQUIRE(schedule.OrderedPasses()[0] == writer.Value());
        REQUIRE(schedule.OrderedPasses()[1] == reader.Value());
    }

    SECTION("multiple writers") {
        RenderGraphLimits limits{.maxPasses = 2, .maxResources = 1, .maxUsages = 2, .maxDependencies = 1};
        RenderGraphBuilder builder = RequireBuilder(limits);
        const auto firstWriter = builder.AddPass(RenderPassKind::Compute, RenderQueueRole::Compute);
        const auto secondWriter = builder.AddPass(RenderPassKind::Compute, RenderQueueRole::Compute);
        const auto imported = builder.ImportBuffer(BufferHandle(), RenderGraphResourceClass::Persistent);
        REQUIRE(firstWriter.HasValue());
        REQUIRE(secondWriter.HasValue());
        REQUIRE(imported.HasValue());
        REQUIRE(
            builder.AddUsage({firstWriter.Value(), imported.Value(), RenderGraphAccess::Write, RenderGraphUsageKind::Storage}).HasValue());
        REQUIRE(
            builder.AddUsage({secondWriter.Value(), imported.Value(), RenderGraphAccess::Write, RenderGraphUsageKind::Storage}).HasValue());

        RenderGraph graph = RequireGraph(builder);
        RenderGraphSchedule schedule = RequireSchedule(graph);
        REQUIRE(schedule.OrderedPasses().size() == 2);
        REQUIRE(schedule.OrderedPasses()[0] == firstWriter.Value());
        REQUIRE(schedule.OrderedPasses()[1] == secondWriter.Value());
    }
}

TEST_CASE("Render graph compilation rejects cycles and duplicate dependencies", "[runtime][renderer][render-graph]") {
    SECTION("cycle") {
        RenderGraphLimits limits{.maxPasses = 4, .maxResources = 1, .maxUsages = 1, .maxDependencies = 3};
        RenderGraphBuilder builder = RequireBuilder(limits);
        const auto first = builder.AddPass(RenderPassKind::Graphics, RenderQueueRole::Graphics);
        const auto second = builder.AddPass(RenderPassKind::Graphics, RenderQueueRole::Graphics);
        const auto third = builder.AddPass(RenderPassKind::Graphics, RenderQueueRole::Graphics);
        const auto disconnected = builder.AddPass(RenderPassKind::Graphics, RenderQueueRole::Graphics);
        REQUIRE(first.HasValue());
        REQUIRE(second.HasValue());
        REQUIRE(third.HasValue());
        REQUIRE(disconnected.HasValue());
        REQUIRE(builder.AddDependency({first.Value(), second.Value(), RenderGraphDependencyKind::ExecutionOrder}).HasValue());
        REQUIRE(builder.AddDependency({second.Value(), third.Value(), RenderGraphDependencyKind::ResourceHazard}).HasValue());
        REQUIRE(builder.AddDependency({third.Value(), first.Value(), RenderGraphDependencyKind::ExecutionOrder}).HasValue());
        auto finalized = builder.Finalize();
        REQUIRE(finalized.HasValue());
        RenderGraph graph = std::move(finalized).Value();
        const auto rejected = CompileRenderGraph(graph);
        RequireError(rejected, "render.graph.dependency_cycle");
        REQUIRE(rejected.ErrorValue().message.find("pass IDs 1 2 3") != std::string::npos);
        REQUIRE(rejected.ErrorValue().message.find(" 4") == std::string::npos);
    }

    SECTION("duplicate edge") {
        RenderGraphLimits limits{.maxPasses = 2, .maxResources = 1, .maxUsages = 1, .maxDependencies = 2};
        RenderGraphBuilder builder = RequireBuilder(limits);
        const auto first = builder.AddPass(RenderPassKind::Graphics, RenderQueueRole::Graphics);
        const auto second = builder.AddPass(RenderPassKind::Graphics, RenderQueueRole::Graphics);
        REQUIRE(first.HasValue());
        REQUIRE(second.HasValue());
        REQUIRE(builder.AddDependency({first.Value(), second.Value(), RenderGraphDependencyKind::ExecutionOrder}).HasValue());
        REQUIRE(builder.AddDependency({first.Value(), second.Value(), RenderGraphDependencyKind::ExecutionOrder}).HasValue());
        auto finalized = builder.Finalize();
        REQUIRE(finalized.HasValue());
        RenderGraph graph = std::move(finalized).Value();
        const auto rejected = CompileRenderGraph(graph);
        RequireError(rejected, "render.graph.dependency_invalid");
        REQUIRE(rejected.ErrorValue().message.find("Pass 1 -> pass 2") != std::string::npos);
    }

    SECTION("different dependency reasons are preserved") {
        RenderGraphLimits limits{.maxPasses = 2, .maxResources = 1, .maxUsages = 1, .maxDependencies = 2};
        RenderGraphBuilder builder = RequireBuilder(limits);
        const auto first = builder.AddPass(RenderPassKind::Graphics, RenderQueueRole::Graphics);
        const auto second = builder.AddPass(RenderPassKind::Graphics, RenderQueueRole::Graphics);
        REQUIRE(first.HasValue());
        REQUIRE(second.HasValue());
        REQUIRE(builder.AddDependency({first.Value(), second.Value(), RenderGraphDependencyKind::ExecutionOrder}).HasValue());
        REQUIRE(builder.AddDependency({first.Value(), second.Value(), RenderGraphDependencyKind::ResourceHazard}).HasValue());
        RenderGraph graph = RequireGraph(builder);
        REQUIRE(graph.Dependencies().size() == 2);
        REQUIRE(RequireSchedule(graph).OrderedPasses().size() == 2);
    }
}

TEST_CASE("Render graph compilation validates transient initialization", "[runtime][renderer][render-graph]") {
    SECTION("read before write") {
        RenderGraphBuilder builder = RequireBuilder();
        const auto reader = builder.AddPass(RenderPassKind::Compute, RenderQueueRole::Compute);
        const auto transient = builder.AddTransientResource(RenderGraphResourceKind::Buffer);
        REQUIRE(reader.HasValue());
        REQUIRE(transient.HasValue());
        REQUIRE(builder.AddUsage({reader.Value(), transient.Value(), RenderGraphAccess::Read, RenderGraphUsageKind::Storage}).HasValue());
        auto finalized = builder.Finalize();
        REQUIRE(finalized.HasValue());
        RenderGraph graph = std::move(finalized).Value();
        RequireError(CompileRenderGraph(graph), "render.graph.read_before_write");
    }

    SECTION("imported content defers exact initial-state validation") {
        RenderGraphBuilder builder = RequireBuilder();
        const auto reader = builder.AddPass(RenderPassKind::Compute, RenderQueueRole::Compute);
        const auto imported = builder.ImportBuffer(BufferHandle(), RenderGraphResourceClass::Persistent);
        REQUIRE(reader.HasValue());
        REQUIRE(imported.HasValue());
        REQUIRE(builder.AddUsage({reader.Value(), imported.Value(), RenderGraphAccess::Read, RenderGraphUsageKind::Storage}).HasValue());
        auto finalized = builder.Finalize();
        REQUIRE(finalized.HasValue());
        RenderGraph graph = std::move(finalized).Value();
        auto compiled = CompileRenderGraph(graph);
        REQUIRE(compiled.HasValue());
        REQUIRE(compiled.Value().OrderedPasses().size() == 1);
    }

    SECTION("dependency order cannot move a producer after its reader") {
        RenderGraphLimits limits{.maxPasses = 2, .maxResources = 1, .maxUsages = 2, .maxDependencies = 1};
        RenderGraphBuilder builder = RequireBuilder(limits);
        const auto reader = builder.AddPass(RenderPassKind::Compute, RenderQueueRole::Compute);
        const auto producer = builder.AddPass(RenderPassKind::Compute, RenderQueueRole::Compute);
        const auto transient = builder.AddTransientResource(RenderGraphResourceKind::Buffer);
        REQUIRE(reader.HasValue());
        REQUIRE(producer.HasValue());
        REQUIRE(transient.HasValue());
        REQUIRE(builder.AddUsage({reader.Value(), transient.Value(), RenderGraphAccess::Read, RenderGraphUsageKind::Storage}).HasValue());
        REQUIRE(
            builder.AddUsage({producer.Value(), transient.Value(), RenderGraphAccess::Write, RenderGraphUsageKind::Storage}).HasValue());
        REQUIRE(builder.AddDependency({reader.Value(), producer.Value(), RenderGraphDependencyKind::ExecutionOrder}).HasValue());
        auto finalized = builder.Finalize();
        REQUIRE(finalized.HasValue());
        RenderGraph graph = std::move(finalized).Value();
        RequireError(CompileRenderGraph(graph), "render.graph.read_before_write");
    }

    SECTION("exported transient data requires a writer") {
        RenderGraphBuilder builder = RequireBuilder();
        REQUIRE(builder.AddPass(RenderPassKind::Graphics, RenderQueueRole::Graphics).HasValue());
        const auto transient = builder.AddTransientResource(RenderGraphResourceKind::Texture);
        REQUIRE(transient.HasValue());
        REQUIRE(builder.ExportResource(transient.Value()).HasValue());
        auto finalized = builder.Finalize();
        REQUIRE(finalized.HasValue());
        RenderGraph graph = std::move(finalized).Value();
        RequireError(CompileRenderGraph(graph), "render.graph.read_before_write");
    }
}

TEST_CASE("Render graph compilation culls transitive unused transient work", "[runtime][renderer][render-graph]") {
    RenderGraphLimits limits{.maxPasses = 4, .maxResources = 4, .maxUsages = 6, .maxDependencies = 2};
    RenderGraphBuilder builder = RequireBuilder(limits);
    const auto producer =
        builder.AddPass(RenderPassKind::Compute, RenderQueueRole::Compute, RenderGraphPassCullPolicy::AllowCullIfOutputsUnused);
    const auto output = builder.AddPass(RenderPassKind::Compute, RenderQueueRole::Compute);
    const auto dead =
        builder.AddPass(RenderPassKind::Compute, RenderQueueRole::Compute, RenderGraphPassCullPolicy::AllowCullIfOutputsUnused);
    const auto deadTail =
        builder.AddPass(RenderPassKind::Compute, RenderQueueRole::Compute, RenderGraphPassCullPolicy::AllowCullIfOutputsUnused);
    const auto intermediate = builder.AddTransientResource(RenderGraphResourceKind::Buffer);
    const auto exported = builder.AddTransientResource(RenderGraphResourceKind::Buffer);
    const auto unused = builder.AddTransientResource(RenderGraphResourceKind::Buffer);
    const auto deadTailOutput = builder.AddTransientResource(RenderGraphResourceKind::Buffer);
    REQUIRE(producer.HasValue());
    REQUIRE(output.HasValue());
    REQUIRE(dead.HasValue());
    REQUIRE(deadTail.HasValue());
    REQUIRE(intermediate.HasValue());
    REQUIRE(exported.HasValue());
    REQUIRE(unused.HasValue());
    REQUIRE(deadTailOutput.HasValue());
    REQUIRE(builder.AddUsage({producer.Value(), intermediate.Value(), RenderGraphAccess::Write, RenderGraphUsageKind::Storage}).HasValue());
    REQUIRE(builder.AddUsage({output.Value(), intermediate.Value(), RenderGraphAccess::Read, RenderGraphUsageKind::Storage}).HasValue());
    REQUIRE(builder.AddUsage({output.Value(), exported.Value(), RenderGraphAccess::Write, RenderGraphUsageKind::Storage}).HasValue());
    REQUIRE(builder.AddUsage({dead.Value(), unused.Value(), RenderGraphAccess::Write, RenderGraphUsageKind::Storage}).HasValue());
    REQUIRE(builder.AddUsage({deadTail.Value(), unused.Value(), RenderGraphAccess::Read, RenderGraphUsageKind::Storage}).HasValue());
    REQUIRE(
        builder.AddUsage({deadTail.Value(), deadTailOutput.Value(), RenderGraphAccess::Write, RenderGraphUsageKind::Storage}).HasValue());
    REQUIRE(builder.AddDependency({producer.Value(), output.Value(), RenderGraphDependencyKind::ResourceHazard}).HasValue());
    REQUIRE(builder.AddDependency({dead.Value(), deadTail.Value(), RenderGraphDependencyKind::ResourceHazard}).HasValue());
    REQUIRE(builder.ExportResource(exported.Value()).HasValue());

    RenderGraph graph = RequireGraph(builder);
    RenderGraphSchedule schedule = RequireSchedule(graph);
    REQUIRE(schedule.OrderedPasses().size() == 2);
    REQUIRE(schedule.OrderedPasses()[0] == producer.Value());
    REQUIRE(schedule.OrderedPasses()[1] == output.Value());
    REQUIRE(CulledPassCount(schedule) == 2);
    REQUIRE(RequireDisposition(schedule, dead.Value()).reason == RenderGraphPassDispositionReason::OnlyRequiredByCulledPasses);
    REQUIRE(RequireDisposition(schedule, deadTail.Value()).reason == RenderGraphPassDispositionReason::UnusedTransientOutputs);
    REQUIRE(RequireDisposition(schedule, producer.Value()).reason == RenderGraphPassDispositionReason::RequiredResourceProducer);
}

TEST_CASE("Render graph compilation conservatively retains non-transient and unknown effects", "[runtime][renderer][render-graph]") {
    RenderGraphLimits limits{.maxPasses = 3, .maxResources = 2, .maxUsages = 2, .maxDependencies = 1};
    RenderGraphBuilder builder = RequireBuilder(limits);
    const auto importedMutation = builder.AddPass(RenderPassKind::Compute, RenderQueueRole::Compute);
    const auto unknownEffect = builder.AddPass(RenderPassKind::Graphics, RenderQueueRole::Graphics);
    const auto defaultTransientWriter = builder.AddPass(RenderPassKind::Compute, RenderQueueRole::Compute);
    const auto imported = builder.ImportBuffer(BufferHandle(), RenderGraphResourceClass::Persistent);
    const auto transient = builder.AddTransientResource(RenderGraphResourceKind::Buffer);
    REQUIRE(importedMutation.HasValue());
    REQUIRE(unknownEffect.HasValue());
    REQUIRE(defaultTransientWriter.HasValue());
    REQUIRE(imported.HasValue());
    REQUIRE(transient.HasValue());
    REQUIRE(
        builder.AddUsage({importedMutation.Value(), imported.Value(), RenderGraphAccess::Write, RenderGraphUsageKind::Storage}).HasValue());
    REQUIRE(builder.AddUsage({defaultTransientWriter.Value(), transient.Value(), RenderGraphAccess::Write, RenderGraphUsageKind::Storage})
                .HasValue());

    RenderGraph graph = RequireGraph(builder);
    RenderGraphSchedule schedule = RequireSchedule(graph);
    REQUIRE(schedule.OrderedPasses().size() == 3);
    REQUIRE(schedule.OrderedPasses()[0] == importedMutation.Value());
    REQUIRE(schedule.OrderedPasses()[1] == unknownEffect.Value());
    REQUIRE(schedule.OrderedPasses()[2] == defaultTransientWriter.Value());
    REQUIRE(CulledPassCount(schedule) == 0);
    REQUIRE(RequireDisposition(schedule, importedMutation.Value()).reason == RenderGraphPassDispositionReason::ImportedResourceMutation);
    REQUIRE(RequireDisposition(schedule, unknownEffect.Value()).reason == RenderGraphPassDispositionReason::ConservativePolicy);
    REQUIRE(RequireDisposition(schedule, defaultTransientWriter.Value()).reason == RenderGraphPassDispositionReason::ConservativePolicy);
}

TEST_CASE("Render graph compilation preserves explicit and external synchronization predecessors", "[runtime][renderer][render-graph]") {
    RenderGraphLimits limits{.maxPasses = 3, .maxResources = 2, .maxUsages = 2, .maxDependencies = 2};
    RenderGraphBuilder builder = RequireBuilder(limits);
    const auto preparation =
        builder.AddPass(RenderPassKind::Compute, RenderQueueRole::Compute, RenderGraphPassCullPolicy::AllowCullIfOutputsUnused);
    const auto deadWriter = builder.AddPass(RenderPassKind::Compute, RenderQueueRole::Compute);
    const auto synchronized = builder.AddPass(RenderPassKind::Compute, RenderQueueRole::Compute);
    const auto firstResource = builder.AddTransientResource(RenderGraphResourceKind::Buffer);
    const auto secondResource = builder.AddTransientResource(RenderGraphResourceKind::Buffer);
    REQUIRE(preparation.HasValue());
    REQUIRE(deadWriter.HasValue());
    REQUIRE(synchronized.HasValue());
    REQUIRE(firstResource.HasValue());
    REQUIRE(secondResource.HasValue());
    REQUIRE(
        builder.AddUsage({preparation.Value(), firstResource.Value(), RenderGraphAccess::Write, RenderGraphUsageKind::Storage}).HasValue());
    REQUIRE(
        builder.AddUsage({deadWriter.Value(), secondResource.Value(), RenderGraphAccess::Write, RenderGraphUsageKind::Storage}).HasValue());
    REQUIRE(builder.AddDependency({preparation.Value(), synchronized.Value(), RenderGraphDependencyKind::ExecutionOrder}).HasValue());
    REQUIRE(
        builder.AddDependency({synchronized.Value(), deadWriter.Value(), RenderGraphDependencyKind::ExternalSynchronization}).HasValue());

    auto finalized = builder.Finalize();
    REQUIRE(finalized.HasValue());
    RenderGraph graph = std::move(finalized).Value();
    auto compiled = CompileRenderGraph(graph);
    REQUIRE(compiled.HasValue());
    REQUIRE(compiled.Value().OrderedPasses().size() == 3);
    REQUIRE(CulledPassCount(compiled.Value()) == 0);
    REQUIRE(RequireDisposition(compiled.Value(), preparation.Value()).reason == RenderGraphPassDispositionReason::RequiredDependency);
    REQUIRE(RequireDisposition(compiled.Value(), deadWriter.Value()).reason == RenderGraphPassDispositionReason::ExternalSynchronization);
}

TEST_CASE("Render graph compilation is immutable and thread-neutral", "[runtime][renderer][render-graph]") {
    RenderGraphBuilder builder = RequireBuilder();
    REQUIRE(builder.AddPass(RenderPassKind::Graphics, RenderQueueRole::Graphics).HasValue());
    auto finalized = builder.Finalize();
    REQUIRE(finalized.HasValue());
    RenderGraph graph = std::move(finalized).Value();
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
    const auto pass = builder.AddPass(RenderPassKind::Graphics, RenderQueueRole::Graphics);
    REQUIRE(pass.HasValue());
    RenderGraph graph = RequireGraph(builder);
    REQUIRE(RequireSchedule(graph).OrderedPasses()[0] == pass.Value());
    REQUIRE(RequireSchedule(graph).OrderedPasses()[0] == pass.Value());

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
