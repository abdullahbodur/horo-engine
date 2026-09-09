#include "Horo/Runtime/Render/RenderGraphExecution.h"
#include "RenderGraphTestUtils.h"

#include <array>
#include <utility>

namespace Horo::Render {
    namespace {
        using namespace Test;

        constexpr std::array SharedQueues{
            RenderQueueAssignment{RenderQueueRole::Graphics, RenderQueueId{7}},
            RenderQueueAssignment{RenderQueueRole::Compute, RenderQueueId{7}},
            RenderQueueAssignment{RenderQueueRole::Transfer, RenderQueueId{7}},
        };

        constexpr std::array SplitQueues{
            RenderQueueAssignment{RenderQueueRole::Graphics, RenderQueueId{3}},
            RenderQueueAssignment{RenderQueueRole::Compute, RenderQueueId{5}},
            RenderQueueAssignment{RenderQueueRole::Transfer, RenderQueueId{8}},
        };

        template <typename T> std::span<const T> Slice(const std::span<const T> records, const RenderGraphExecutionRange range) {
            REQUIRE(range.IsValidFor(records.size()));
            return records.subspan(range.offset, range.count);
        }

        struct GraphPipeline {
            RenderGraph graph;
            RenderGraphSchedule schedule;
            RenderGraphSynchronizationPlan synchronization;
        };

        GraphPipeline RequirePipeline(RenderGraphBuilder &builder, const std::span<const RenderQueueAssignment> queues = SharedQueues) {
            RenderGraph graph = RequireGraph(builder);
            RenderGraphSchedule schedule = RequireSchedule(graph);
            auto synchronization = SynthesizeRenderGraphSynchronization(graph, schedule, queues, {});
            REQUIRE(synchronization.HasValue());
            return {std::move(graph), std::move(schedule), std::move(synchronization).Value()};
        }

        struct WriteReadRefs {
            RenderGraphPassRef write;
            RenderGraphPassRef read;
            RenderGraphResourceId resource;
        };

        WriteReadRefs AuthorWriteRead(RenderGraphBuilder &builder) {
            const RenderGraphPassRef write = RequirePass(builder, RenderPassKind::Graphics, RenderQueueRole::Graphics);
            const RenderGraphPassRef read = RequirePass(builder, RenderPassKind::Compute, RenderQueueRole::Compute);
            const RenderGraphResourceId resource = RequireResource(builder.AddTransientResource(RenderGraphResourceKind::Buffer));
            const std::array usages{
                RenderGraphResourceUsage{write, resource, RenderGraphAccess::Write, RenderGraphUsageKind::Storage},
                RenderGraphResourceUsage{read, resource, RenderGraphAccess::Read, RenderGraphUsageKind::Sampled},
            };
            for (const RenderGraphResourceUsage &usage : usages) {
                RequireUsage(builder, usage);
            }
            return {write, read, resource};
        }

        CompiledRenderGraphExecution RequireDetachedExecution() {
            RenderGraphBuilder builder = RequireBuilder();
            AuthorWriteRead(builder);
            GraphPipeline pipeline = RequirePipeline(builder, SplitQueues);
            auto execution = CompileRenderGraphExecution(pipeline.graph, pipeline.schedule, pipeline.synchronization, SplitQueues);
            REQUIRE(execution.HasValue());
            return std::move(execution).Value();
        }
    }  // namespace

    TEST_CASE("Execution compilation owns ordered backend-neutral pass packets", "[renderer][render-graph][execution]") {
        RenderGraphBuilder builder = RequireBuilder();
        const WriteReadRefs refs = AuthorWriteRead(builder);
        GraphPipeline pipeline = RequirePipeline(builder, SplitQueues);

        auto compiled = CompileRenderGraphExecution(pipeline.graph, pipeline.schedule, pipeline.synchronization, SplitQueues);
        REQUIRE(compiled.HasValue());
        const CompiledRenderGraphExecution &execution = compiled.Value();
        REQUIRE(execution.Passes().size() == 2);
        REQUIRE(execution.Resources().size() == 1);

        const RenderGraphExecutionPass &write = execution.Passes()[0];
        CHECK(write.pass == refs.write);
        CHECK(write.kind == RenderPassKind::Graphics);
        CHECK(write.queue == RenderQueueId{3});
        CHECK(Slice(execution.Usages(), write.usages).front().resource == refs.resource);
        CHECK(Slice(execution.Transitions(), write.transitions).front().hazards == RenderGraphHazard::Initial);
        REQUIRE(Slice(execution.ReleaseTransfers(), write.releaseTransfers).size() == 1);
        CHECK(Slice(execution.ReleaseTransfers(), write.releaseTransfers).front().acquireBefore == refs.read);
        CHECK(Slice(execution.AcquireTransfers(), write.acquireTransfers).empty());

        const RenderGraphExecutionPass &read = execution.Passes()[1];
        CHECK(read.pass == refs.read);
        CHECK(read.queue == RenderQueueId{5});
        CHECK(Slice(execution.Transitions(), read.transitions).front().after == refs.read);
        CHECK(Slice(execution.ReleaseTransfers(), read.releaseTransfers).empty());
        REQUIRE(Slice(execution.AcquireTransfers(), read.acquireTransfers).size() == 1);
        CHECK(Slice(execution.AcquireTransfers(), read.acquireTransfers).front().releaseAfter == refs.write);
    }

    TEST_CASE("Execution plan remains valid after compilation sources are released", "[renderer][render-graph][execution]") {
        CompiledRenderGraphExecution execution = RequireDetachedExecution();
        const RenderGraphOwnerId owner = execution.Owner();
        REQUIRE(owner.IsValid());
        REQUIRE(execution.Passes().size() == 2);
        CHECK(execution.Resources().front().id.owner == owner);

        CompiledRenderGraphExecution moved = std::move(execution);
        CHECK_FALSE(execution.Owner().IsValid());
        CHECK(moved.Owner() == owner);
        CHECK(moved.Passes().size() == 2);
    }

    TEST_CASE("Execution compilation omits culled pass work", "[renderer][render-graph][execution]") {
        RenderGraphBuilder builder = RequireBuilder();
        const RenderGraphPassRef retained = RequirePass(builder, RenderPassKind::Compute, RenderQueueRole::Compute);
        const RenderGraphPassRef culled =
            RequirePass(builder, RenderPassKind::Compute, RenderQueueRole::Compute, RenderGraphPassCullPolicy::AllowCullIfOutputsUnused);
        const RenderGraphResourceId retainedResource = RequireResource(builder.AddTransientResource(RenderGraphResourceKind::Buffer));
        const RenderGraphResourceId culledResource = RequireResource(builder.AddTransientResource(RenderGraphResourceKind::Buffer));
        RequireUsage(builder, {retained, retainedResource, RenderGraphAccess::Write, RenderGraphUsageKind::Storage});
        RequireUsage(builder, {culled, culledResource, RenderGraphAccess::Write, RenderGraphUsageKind::Storage});
        GraphPipeline pipeline = RequirePipeline(builder);

        auto compiled = CompileRenderGraphExecution(pipeline.graph, pipeline.schedule, pipeline.synchronization, SharedQueues);
        REQUIRE(compiled.HasValue());
        REQUIRE(compiled.Value().Passes().size() == 1);
        CHECK(compiled.Value().Passes().front().pass == retained);
        REQUIRE(compiled.Value().Usages().size() == 1);
        CHECK(compiled.Value().Usages().front().pass != culled);
    }

    TEST_CASE("Execution compilation rejects missing and duplicate queue assignments", "[renderer][render-graph][execution]") {
        RenderGraphBuilder builder = RequireBuilder();
        AuthorWriteRead(builder);
        GraphPipeline pipeline = RequirePipeline(builder, SplitQueues);
        constexpr std::array missing{RenderQueueAssignment{RenderQueueRole::Graphics, RenderQueueId{1}}};
        RequireError(CompileRenderGraphExecution(pipeline.graph, pipeline.schedule, pipeline.synchronization, missing),
                     "render.graph.execution.queue_topology_invalid");

        constexpr std::array duplicate{
            RenderQueueAssignment{RenderQueueRole::Graphics, RenderQueueId{1}},
            RenderQueueAssignment{RenderQueueRole::Graphics, RenderQueueId{2}},
            RenderQueueAssignment{RenderQueueRole::Compute, RenderQueueId{3}},
        };
        RequireError(CompileRenderGraphExecution(pipeline.graph, pipeline.schedule, pipeline.synchronization, duplicate),
                     "render.graph.execution.queue_topology_invalid");

        constexpr std::array invalid{
            RenderQueueAssignment{RenderQueueRole::Graphics, RenderQueueId{1}},
            RenderQueueAssignment{RenderQueueRole::Compute, RenderQueueId{}},
        };
        RequireError(CompileRenderGraphExecution(pipeline.graph, pipeline.schedule, pipeline.synchronization, invalid),
                     "render.graph.execution.queue_topology_invalid");
    }

    TEST_CASE("Execution compilation rejects synchronization synthesized for another effective topology",
              "[renderer][render-graph][execution]") {
        RenderGraphBuilder builder = RequireBuilder();
        AuthorWriteRead(builder);
        GraphPipeline pipeline = RequirePipeline(builder, SharedQueues);

        RequireError(CompileRenderGraphExecution(pipeline.graph, pipeline.schedule, pipeline.synchronization, SplitQueues),
                     "render.graph.execution.synchronization_invalid");
    }

    TEST_CASE("Execution compilation preserves cross-queue dependency provenance without resource hazards",
              "[renderer][render-graph][execution]") {
        RenderGraphBuilder builder = RequireBuilder();
        const RenderGraphPassRef graphics = RequirePass(builder, RenderPassKind::Graphics, RenderQueueRole::Graphics);
        const RenderGraphPassRef compute = RequirePass(builder, RenderPassKind::Compute, RenderQueueRole::Compute);
        RequireDependency(builder, {graphics, compute, RenderGraphDependencyKind::ExternalSynchronization});
        GraphPipeline pipeline = RequirePipeline(builder, SplitQueues);
        REQUIRE(pipeline.synchronization.Transitions().empty());
        REQUIRE(pipeline.synchronization.OwnershipTransfers().empty());

        auto compiled = CompileRenderGraphExecution(pipeline.graph, pipeline.schedule, pipeline.synchronization, SplitQueues);
        REQUIRE(compiled.HasValue());
        const RenderGraphExecutionPass &dependent = compiled.Value().Passes()[1];
        const auto dependencies = Slice(compiled.Value().Dependencies(), dependent.dependencies);
        REQUIRE(dependencies.size() == 1);
        CHECK(dependencies.front().before == graphics);
        CHECK(dependencies.front().after == compute);
        CHECK(dependencies.front().kind == RenderGraphDependencyKind::ExternalSynchronization);
    }

    TEST_CASE("Execution compilation verifies topology when imported state needs no transition", "[renderer][render-graph][execution]") {
        RenderGraphBuilder builder = RequireBuilder();
        const auto pass = RequirePass(builder, RenderPassKind::Compute, RenderQueueRole::Compute);
        const auto resource = RequireResource(builder.ImportBuffer(BufferHandle(), RenderGraphResourceClass::Persistent));
        const RenderGraphResourceUsage read{pass, resource, RenderGraphAccess::Read, RenderGraphUsageKind::Sampled};
        RequireUsage(builder, read);
        RenderGraph graph = RequireGraph(builder);
        RenderGraphSchedule schedule = RequireSchedule(graph);
        constexpr std::array topologyA{RenderQueueAssignment{RenderQueueRole::Compute, RenderQueueId{11}}};
        constexpr std::array topologyB{RenderQueueAssignment{RenderQueueRole::Compute, RenderQueueId{12}}};
        const std::array initial{
            RenderGraphImportedState{resource,
                                     {RenderGraphSynchronizationAccess::Read, RenderGraphSynchronizationOperation::Sampled,
                                      RenderGraphPipelineScope::Compute, RenderGraphTextureLayout::NotApplicable, RenderQueueId{11}}}};
        auto synchronization = SynthesizeRenderGraphSynchronization(graph, schedule, topologyA, initial);
        REQUIRE(synchronization.HasValue());
        REQUIRE(synchronization.Value().Transitions().empty());

        RequireError(CompileRenderGraphExecution(graph, schedule, synchronization.Value(), topologyB),
                     "render.graph.execution.synchronization_invalid");
    }

    TEST_CASE("Execution compilation rejects schedule and synchronization provenance mismatch", "[renderer][render-graph][execution]") {
        RenderGraphBuilder firstBuilder = RequireBuilder();
        AuthorWriteRead(firstBuilder);
        GraphPipeline first = RequirePipeline(firstBuilder);
        RenderGraphBuilder secondBuilder = RequireBuilder();
        AuthorWriteRead(secondBuilder);
        GraphPipeline second = RequirePipeline(secondBuilder);

        RequireError(CompileRenderGraphExecution(first.graph, second.schedule, first.synchronization, SharedQueues),
                     "render.graph.execution.schedule_invalid");
        RequireError(CompileRenderGraphExecution(first.graph, first.schedule, second.synchronization, SharedQueues),
                     "render.graph.execution.synchronization_invalid");

        RenderGraphSynchronizationPlan intact = std::move(first.synchronization);
        REQUIRE(intact.Owner().IsValid());
        REQUIRE_FALSE(first.synchronization.Owner().IsValid());
        RequireError(CompileRenderGraphExecution(first.graph, first.schedule, first.synchronization, SharedQueues),
                     "render.graph.execution.synchronization_invalid");
    }
}  // namespace Horo::Render
