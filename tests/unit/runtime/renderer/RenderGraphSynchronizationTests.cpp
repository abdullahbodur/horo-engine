#include "Horo/Runtime/Render/RenderGraphSynchronization.h"
#include "Horo/Runtime/Render/RenderGraphSynchronizationErrors.h"
#include "RenderGraphTestUtils.h"

#include <array>

namespace Horo::Render {
    namespace {
        using namespace Test;

        constexpr std::array SharedQueue{
            RenderQueueAssignment{RenderQueueRole::Graphics, RenderQueueId{1}},
            RenderQueueAssignment{RenderQueueRole::Compute, RenderQueueId{1}},
            RenderQueueAssignment{RenderQueueRole::Transfer, RenderQueueId{1}},
        };

        RenderGraphLogicalState BufferState(const RenderGraphSynchronizationAccess access,
                                            const RenderGraphSynchronizationOperation operation, const RenderQueueId queue = {1},
                                            const RenderGraphPipelineScope scope = RenderGraphPipelineScope::External) {
            return {access, operation, scope, RenderGraphTextureLayout::NotApplicable, queue};
        }

        Result<RenderGraphSynchronizationPlan> Synthesize(const RenderGraph &graph, const RenderGraphSchedule &schedule,
                                                          const std::span<const RenderGraphImportedState> states = {}) {
            return SynthesizeRenderGraphSynchronization(graph, schedule, SharedQueue, states);
        }
    }  // namespace

    TEST_CASE("Synchronization synthesis derives RAW and WAW transitions deterministically", "[renderer][render-graph][sync]") {
        RenderGraphBuilder builder = RequireBuilder();
        const RenderGraphPassRef write = RequirePass(builder, RenderPassKind::Compute, RenderQueueRole::Compute);
        const RenderGraphPassRef overwrite = RequirePass(builder, RenderPassKind::Compute, RenderQueueRole::Compute);
        const RenderGraphPassRef read = RequirePass(builder, RenderPassKind::Compute, RenderQueueRole::Compute);
        const RenderGraphResourceId resource = RequireResource(builder.AddTransientResource(RenderGraphResourceKind::Buffer));
        RequireUsage(builder, {write, resource, RenderGraphAccess::Write, RenderGraphUsageKind::Storage});
        RequireUsage(builder, {overwrite, resource, RenderGraphAccess::Write, RenderGraphUsageKind::Storage});
        RequireUsage(builder, {read, resource, RenderGraphAccess::Read, RenderGraphUsageKind::Sampled});
        RenderGraph graph = RequireGraph(builder);
        RenderGraphSchedule schedule = RequireSchedule(graph);

        auto synthesized = Synthesize(graph, schedule);
        REQUIRE(synthesized.HasValue());
        const auto transitions = synthesized.Value().Transitions();
        REQUIRE(transitions.size() == 3);
        CHECK(transitions[0].hazards == RenderGraphHazard::Initial);
        CHECK(transitions[0].oldState.access == RenderGraphSynchronizationAccess::None);
        CHECK(transitions[0].after == write);
        CHECK(HasRenderGraphHazard(transitions[1].hazards, RenderGraphHazard::WriteAfterWrite));
        CHECK(transitions[1].before == write);
        CHECK(transitions[1].after == overwrite);
        CHECK(HasRenderGraphHazard(transitions[2].hazards, RenderGraphHazard::ReadAfterWrite));
        CHECK(transitions[2].before == overwrite);
        CHECK(transitions[2].after == read);
    }

    TEST_CASE("Compatible imported reads share state without a barrier", "[renderer][render-graph][sync]") {
        RenderGraphBuilder builder = RequireBuilder();
        const RenderGraphPassRef first = RequirePass(builder, RenderPassKind::Compute, RenderQueueRole::Compute);
        const RenderGraphPassRef second = RequirePass(builder, RenderPassKind::Compute, RenderQueueRole::Compute);
        const RenderGraphResourceId resource = RequireResource(builder.ImportBuffer(BufferHandle(), RenderGraphResourceClass::Persistent));
        RequireUsage(builder, {first, resource, RenderGraphAccess::Read, RenderGraphUsageKind::Sampled});
        RequireUsage(builder, {second, resource, RenderGraphAccess::Read, RenderGraphUsageKind::Sampled});
        RenderGraph graph = RequireGraph(builder);
        RenderGraphSchedule schedule = RequireSchedule(graph);
        const std::array initial{RenderGraphImportedState{resource, BufferState(RenderGraphSynchronizationAccess::Read,
                                                                                RenderGraphSynchronizationOperation::Sampled, {1},
                                                                                RenderGraphPipelineScope::Compute)}};

        auto synthesized = Synthesize(graph, schedule, initial);
        REQUIRE(synthesized.HasValue());
        CHECK(synthesized.Value().Transitions().empty());
        CHECK(synthesized.Value().OwnershipTransfers().empty());
    }

    TEST_CASE("Distinct effective queues emit one matched ownership transfer", "[renderer][render-graph][sync]") {
        RenderGraphBuilder builder = RequireBuilder();
        const RenderGraphPassRef write = RequirePass(builder, RenderPassKind::Graphics, RenderQueueRole::Graphics);
        const RenderGraphPassRef read = RequirePass(builder, RenderPassKind::Compute, RenderQueueRole::Compute);
        const RenderGraphResourceId resource = RequireResource(builder.AddTransientResource(RenderGraphResourceKind::Buffer));
        RequireUsage(builder, {write, resource, RenderGraphAccess::Write, RenderGraphUsageKind::Storage});
        RequireUsage(builder, {read, resource, RenderGraphAccess::Read, RenderGraphUsageKind::Sampled});
        RenderGraph graph = RequireGraph(builder);
        RenderGraphSchedule schedule = RequireSchedule(graph);
        constexpr std::array queues{
            RenderQueueAssignment{RenderQueueRole::Graphics, RenderQueueId{5}},
            RenderQueueAssignment{RenderQueueRole::Compute, RenderQueueId{8}},
        };

        auto synthesized = SynthesizeRenderGraphSynchronization(graph, schedule, queues, {});
        REQUIRE(synthesized.HasValue());
        REQUIRE(synthesized.Value().OwnershipTransfers().size() == 1);
        const RenderGraphOwnershipTransfer &transfer = synthesized.Value().OwnershipTransfers().front();
        CHECK(transfer.releaseAfter == write);
        CHECK(transfer.acquireBefore == read);
        CHECK(transfer.sourceQueue == RenderQueueId{5});
        CHECK(transfer.destinationQueue == RenderQueueId{8});
    }

    TEST_CASE("Aliased queue roles do not invent ownership transfers", "[renderer][render-graph][sync]") {
        RenderGraphBuilder builder = RequireBuilder();
        const RenderGraphPassRef write = RequirePass(builder, RenderPassKind::Graphics, RenderQueueRole::Graphics);
        const RenderGraphPassRef read = RequirePass(builder, RenderPassKind::Compute, RenderQueueRole::Compute);
        const RenderGraphResourceId resource = RequireResource(builder.AddTransientResource(RenderGraphResourceKind::Buffer));
        RequireUsage(builder, {write, resource, RenderGraphAccess::Write, RenderGraphUsageKind::Storage});
        RequireUsage(builder, {read, resource, RenderGraphAccess::Read, RenderGraphUsageKind::Sampled});
        RenderGraph graph = RequireGraph(builder);
        RenderGraphSchedule schedule = RequireSchedule(graph);

        auto synthesized = Synthesize(graph, schedule);
        REQUIRE(synthesized.HasValue());
        CHECK(synthesized.Value().Transitions().size() == 2);
        CHECK(synthesized.Value().OwnershipTransfers().empty());
    }

    TEST_CASE("Read-write uses retain every applicable hazard reason", "[renderer][render-graph][sync]") {
        RenderGraphBuilder builder = RequireBuilder();
        const RenderGraphPassRef write = RequirePass(builder, RenderPassKind::Compute, RenderQueueRole::Compute);
        const RenderGraphPassRef firstReadWrite = RequirePass(builder, RenderPassKind::Compute, RenderQueueRole::Compute);
        const RenderGraphPassRef secondReadWrite = RequirePass(builder, RenderPassKind::Compute, RenderQueueRole::Compute);
        const RenderGraphResourceId resource = RequireResource(builder.AddTransientResource(RenderGraphResourceKind::Buffer));
        RequireUsage(builder, {write, resource, RenderGraphAccess::Write, RenderGraphUsageKind::Storage});
        RequireUsage(builder, {firstReadWrite, resource, RenderGraphAccess::ReadWrite, RenderGraphUsageKind::Storage});
        RequireUsage(builder, {secondReadWrite, resource, RenderGraphAccess::ReadWrite, RenderGraphUsageKind::Storage});
        RenderGraph graph = RequireGraph(builder);
        RenderGraphSchedule schedule = RequireSchedule(graph);

        auto synthesized = Synthesize(graph, schedule);
        REQUIRE(synthesized.HasValue());
        REQUIRE(synthesized.Value().Transitions().size() == 3);
        const RenderGraphHazard hazards = synthesized.Value().Transitions()[2].hazards;
        CHECK(HasRenderGraphHazard(hazards, RenderGraphHazard::ReadAfterWrite));
        CHECK(HasRenderGraphHazard(hazards, RenderGraphHazard::WriteAfterRead));
        CHECK(HasRenderGraphHazard(hazards, RenderGraphHazard::WriteAfterWrite));
        CHECK_FALSE(HasRenderGraphHazard(hazards, RenderGraphHazard::StateChange));
    }

    TEST_CASE("Undefined transient reads fail before synchronization publication", "[renderer][render-graph][sync]") {
        RenderGraphBuilder builder = RequireBuilder();
        const RenderGraphPassRef read = RequirePass(builder, RenderPassKind::Compute, RenderQueueRole::Compute);
        const RenderGraphResourceId resource = RequireResource(builder.AddTransientResource(RenderGraphResourceKind::Buffer));
        RequireUsage(builder, {read, resource, RenderGraphAccess::Read, RenderGraphUsageKind::Sampled});
        RenderGraph graph = RequireGraph(builder);

        RequireError(CompileRenderGraph(graph), "render.graph.read_before_write");
    }

    TEST_CASE("Imported resources require one exact compatible initial state", "[renderer][render-graph][sync]") {
        RenderGraphBuilder builder = RequireBuilder();
        const RenderGraphPassRef pass = RequirePass(builder, RenderPassKind::Graphics, RenderQueueRole::Graphics);
        const RenderGraphResourceId resource = RequireResource(builder.ImportTexture(TextureHandle(), RenderGraphResourceClass::External));
        RequireUsage(builder, {pass, resource, RenderGraphAccess::Read, RenderGraphUsageKind::Sampled});
        RenderGraph graph = RequireGraph(builder);
        RenderGraphSchedule schedule = RequireSchedule(graph);

        RequireError(Synthesize(graph, schedule), "render.graph.synchronization.initial_state_missing");

        const std::array invalid{RenderGraphImportedState{resource, BufferState(RenderGraphSynchronizationAccess::Read,
                                                                                RenderGraphSynchronizationOperation::Sampled)}};
        RequireError(Synthesize(graph, schedule, invalid), "render.graph.synchronization.initial_state_invalid");

        const RenderGraphLogicalState valid{RenderGraphSynchronizationAccess::Read,
                                            RenderGraphSynchronizationOperation::Sampled,
                                            RenderGraphPipelineScope::External,
                                            RenderGraphTextureLayout::ShaderReadOnly,
                                            {1}};
        const std::array duplicate{RenderGraphImportedState{resource, valid}, RenderGraphImportedState{resource, valid}};
        RequireError(Synthesize(graph, schedule, duplicate), "render.graph.synchronization.initial_state_duplicate");
    }

    TEST_CASE("Synthesis rejects incomplete or ambiguous queue topology", "[renderer][render-graph][sync]") {
        RenderGraphBuilder builder = RequireBuilder();
        const RenderGraphPassRef pass = RequirePass(builder, RenderPassKind::Compute, RenderQueueRole::Compute);
        const RenderGraphResourceId resource = RequireResource(builder.AddTransientResource(RenderGraphResourceKind::Buffer));
        RequireUsage(builder, {pass, resource, RenderGraphAccess::Write, RenderGraphUsageKind::Storage});
        RenderGraph graph = RequireGraph(builder);
        RenderGraphSchedule schedule = RequireSchedule(graph);

        constexpr std::array missing{RenderQueueAssignment{RenderQueueRole::Graphics, RenderQueueId{1}}};
        RequireError(SynthesizeRenderGraphSynchronization(graph, schedule, missing, {}),
                     "render.graph.synchronization.queue_topology_invalid");

        constexpr std::array duplicate{
            RenderQueueAssignment{RenderQueueRole::Compute, RenderQueueId{1}},
            RenderQueueAssignment{RenderQueueRole::Compute, RenderQueueId{2}},
        };
        RequireError(SynthesizeRenderGraphSynchronization(graph, schedule, duplicate, {}),
                     "render.graph.synchronization.queue_topology_invalid");
    }

    TEST_CASE("Synthesis rejects a pass-internal state conflict", "[renderer][render-graph][sync]") {
        RenderGraphBuilder builder = RequireBuilder();
        const RenderGraphPassRef pass = RequirePass(builder, RenderPassKind::Compute, RenderQueueRole::Compute);
        const RenderGraphResourceId resource = RequireResource(builder.ImportBuffer(BufferHandle(), RenderGraphResourceClass::Persistent));
        RequireUsage(builder, {pass, resource, RenderGraphAccess::Read, RenderGraphUsageKind::Sampled});
        RequireUsage(builder, {pass, resource, RenderGraphAccess::Write, RenderGraphUsageKind::Storage});
        RenderGraph graph = RequireGraph(builder);
        RenderGraphSchedule schedule = RequireSchedule(graph);
        const std::array initial{RenderGraphImportedState{resource, BufferState(RenderGraphSynchronizationAccess::Read,
                                                                                RenderGraphSynchronizationOperation::Sampled, {1},
                                                                                RenderGraphPipelineScope::Compute)}};

        RequireError(Synthesize(graph, schedule, initial), "render.graph.synchronization.state_unsupported");
    }
}  // namespace Horo::Render
