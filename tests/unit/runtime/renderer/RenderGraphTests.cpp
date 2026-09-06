#include "Horo/Runtime/Render/RenderGraph.h"

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

    RenderGraphLimits SmallLimits() {
        return {.maxPasses = 3, .maxResources = 2, .maxUsages = 3, .maxDependencies = 2};
    }
}  // namespace

TEST_CASE("Render graph builder owns typed records in authoring order", "[runtime][renderer][render-graph]") {
    auto created = RenderGraphBuilder::Create(SmallLimits());
    REQUIRE(created.HasValue());
    RenderGraphBuilder builder = std::move(created).Value();

    auto graphics = builder.AddPass(RenderPassKind::Graphics, RenderQueueRole::Graphics);
    auto compute = builder.AddPass(RenderPassKind::Compute, RenderQueueRole::Compute);
    auto texture = builder.AddResource(RenderGraphResourceKind::Texture);
    auto buffer = builder.AddResource(RenderGraphResourceKind::Buffer);
    REQUIRE(graphics.HasValue());
    REQUIRE(compute.HasValue());
    REQUIRE(texture.HasValue());
    REQUIRE(buffer.HasValue());

    REQUIRE(
        builder.AddUsage({graphics.Value(), texture.Value(), RenderGraphAccess::Write, RenderGraphUsageKind::ColorAttachment}).HasValue());
    REQUIRE(builder.AddUsage({compute.Value(), buffer.Value(), RenderGraphAccess::ReadWrite, RenderGraphUsageKind::Storage}).HasValue());
    REQUIRE(builder.AddDependency({graphics.Value(), compute.Value(), RenderGraphDependencyKind::ExecutionOrder}).HasValue());

    auto finalized = builder.Finalize();
    REQUIRE(finalized.HasValue());
    RenderGraph graph = std::move(finalized).Value();

    REQUIRE(builder.State() == RenderGraphBuilderState::Finalized);
    REQUIRE(graph.Owner() == graphics.Value().owner);
    REQUIRE(graph.Limits().maxPasses == SmallLimits().maxPasses);
    REQUIRE(graph.Passes().size() == 2);
    REQUIRE(graph.Passes()[0].reference.id == RenderPassId{1});
    REQUIRE(graph.Passes()[1].queue == RenderQueueRole::Compute);
    REQUIRE(graph.Resources().size() == 2);
    REQUIRE(graph.Usages().size() == 2);
    REQUIRE(graph.Dependencies().size() == 1);
    REQUIRE(graph.Dependencies()[0].before == graphics.Value());
}

TEST_CASE("Render graph capacities are exact hard admission boundaries", "[runtime][renderer][render-graph]") {
    REQUIRE(RenderGraphLimits{.maxPasses = RenderGraphLimits::HardMaxPasses,
                              .maxResources = RenderGraphLimits::HardMaxResources,
                              .maxUsages = RenderGraphLimits::HardMaxUsages,
                              .maxDependencies = RenderGraphLimits::HardMaxDependencies}
                .IsValid());

    SECTION("pass resource and usage limits") {
        const RenderGraphLimits limits{.maxPasses = 1, .maxResources = 1, .maxUsages = 1, .maxDependencies = 1};
        auto created = RenderGraphBuilder::Create(limits);
        REQUIRE(created.HasValue());
        RenderGraphBuilder builder = std::move(created).Value();

        auto pass = builder.AddPass(RenderPassKind::Graphics, RenderQueueRole::Graphics);
        auto resource = builder.AddResource(RenderGraphResourceKind::Texture);
        REQUIRE(pass.HasValue());
        REQUIRE(resource.HasValue());
        REQUIRE(
            builder.AddUsage({pass.Value(), resource.Value(), RenderGraphAccess::Write, RenderGraphUsageKind::ColorAttachment}).HasValue());

        RequireError(builder.AddPass(RenderPassKind::Graphics, RenderQueueRole::Graphics), "render.graph.capacity_exceeded");
        RequireError(builder.AddResource(RenderGraphResourceKind::Buffer), "render.graph.capacity_exceeded");
        RequireError(builder.AddUsage(
                         {pass.Value(), resource.Value(), RenderGraphAccess::Write, RenderGraphUsageKind::DepthStencilAttachment}),
                     "render.graph.capacity_exceeded");
    }

    SECTION("dependency limit") {
        const RenderGraphLimits limits{.maxPasses = 2, .maxResources = 1, .maxUsages = 1, .maxDependencies = 1};
        auto created = RenderGraphBuilder::Create(limits);
        REQUIRE(created.HasValue());
        RenderGraphBuilder builder = std::move(created).Value();
        auto first = builder.AddPass(RenderPassKind::Graphics, RenderQueueRole::Graphics);
        auto second = builder.AddPass(RenderPassKind::Compute, RenderQueueRole::Compute);
        REQUIRE(first.HasValue());
        REQUIRE(second.HasValue());
        REQUIRE(builder.AddDependency({first.Value(), second.Value(), RenderGraphDependencyKind::ExecutionOrder}).HasValue());
        RequireError(builder.AddDependency({second.Value(), first.Value(), RenderGraphDependencyKind::ExecutionOrder}),
                     "render.graph.capacity_exceeded");
    }
}

TEST_CASE("Render graph supports read-only depth-stencil attachment use", "[runtime][renderer][render-graph]") {
    auto created = RenderGraphBuilder::Create(SmallLimits());
    REQUIRE(created.HasValue());
    RenderGraphBuilder builder = std::move(created).Value();

    auto pass = builder.AddPass(RenderPassKind::Graphics, RenderQueueRole::Graphics);
    auto texture = builder.AddResource(RenderGraphResourceKind::Texture);
    REQUIRE(pass.HasValue());
    REQUIRE(texture.HasValue());
    REQUIRE(builder.AddUsage({pass.Value(), texture.Value(), RenderGraphAccess::Read, RenderGraphUsageKind::DepthStencilAttachment})
                .HasValue());

    auto finalized = builder.Finalize();
    REQUIRE(finalized.HasValue());
    REQUIRE(finalized.Value().Usages().front().access == RenderGraphAccess::Read);
}

TEST_CASE("Render graph failures reject unsupported and foreign records without fallback", "[runtime][renderer][render-graph]") {
    RenderGraphLimits invalid = SmallLimits();
    invalid.maxPasses = RenderGraphLimits::HardMaxPasses + 1;
    RequireError(RenderGraphBuilder::Create(invalid), "render.graph.limits_invalid");

    auto firstCreated = RenderGraphBuilder::Create(SmallLimits());
    auto secondCreated = RenderGraphBuilder::Create(SmallLimits());
    REQUIRE(firstCreated.HasValue());
    REQUIRE(secondCreated.HasValue());
    RenderGraphBuilder first = std::move(firstCreated).Value();
    RenderGraphBuilder second = std::move(secondCreated).Value();

    RequireError(first.AddPass(static_cast<RenderPassKind>(255), RenderQueueRole::Graphics), "render.graph.pass_kind_unsupported");
    RequireError(first.AddPass(RenderPassKind::Graphics, static_cast<RenderQueueRole>(255)), "render.graph.queue_role_unsupported");
    RequireError(first.AddPass(RenderPassKind::Graphics, RenderQueueRole::Compute), "render.graph.queue_incompatible");
    RequireError(first.AddResource(static_cast<RenderGraphResourceKind>(255)), "render.graph.resource_kind_unsupported");

    auto firstPass = first.AddPass(RenderPassKind::Graphics, RenderQueueRole::Graphics);
    auto firstCompute = first.AddPass(RenderPassKind::Compute, RenderQueueRole::Graphics);
    auto firstTexture = first.AddResource(RenderGraphResourceKind::Texture);
    auto foreignPass = second.AddPass(RenderPassKind::Graphics, RenderQueueRole::Graphics);
    auto foreignTexture = second.AddResource(RenderGraphResourceKind::Texture);
    REQUIRE(firstPass.HasValue());
    REQUIRE(firstCompute.HasValue());
    REQUIRE(firstTexture.HasValue());
    REQUIRE(foreignPass.HasValue());
    REQUIRE(foreignTexture.HasValue());

    RequireError(first.AddUsage(
                     {foreignPass.Value(), firstTexture.Value(), RenderGraphAccess::Write, RenderGraphUsageKind::ColorAttachment}),
                 "render.graph.wrong_owner");
    RequireError(first.AddUsage(
                     {firstPass.Value(), foreignTexture.Value(), RenderGraphAccess::Write, RenderGraphUsageKind::ColorAttachment}),
                 "render.graph.wrong_owner");
    RequireError(first.AddUsage({firstPass.Value(), firstTexture.Value(), RenderGraphAccess::Read, RenderGraphUsageKind::ColorAttachment}),
                 "render.graph.usage_invalid");
    RequireError(first.AddUsage({firstPass.Value(), firstTexture.Value(), RenderGraphAccess::Read, static_cast<RenderGraphUsageKind>(255)}),
                 "render.graph.usage_unsupported");
    RequireError(first.AddUsage({RenderGraphPassRef{first.Owner(), RenderPassId{99}}, firstTexture.Value(), RenderGraphAccess::Write,
                                 RenderGraphUsageKind::ColorAttachment}),
                 "render.graph.pass_invalid");
    RequireError(first.AddUsage({firstPass.Value(), RenderGraphResourceId{first.Owner(), 99}, RenderGraphAccess::Write,
                                 RenderGraphUsageKind::ColorAttachment}),
                 "render.graph.resource_invalid");
    RequireError(first.AddDependency({firstPass.Value(), firstPass.Value(), RenderGraphDependencyKind::ExecutionOrder}),
                 "render.graph.dependency_invalid");
    RequireError(first.AddDependency({firstPass.Value(), foreignPass.Value(), RenderGraphDependencyKind::ExecutionOrder}),
                 "render.graph.wrong_owner");
    RequireError(first.AddDependency({firstPass.Value(), firstCompute.Value(), static_cast<RenderGraphDependencyKind>(255)}),
                 "render.graph.dependency_kind_unsupported");
}

TEST_CASE("Render graph builder enforces affinity cancellation and shutdown lifecycle", "[runtime][renderer][render-graph]") {
    SECTION("owner thread affinity") {
        auto created = RenderGraphBuilder::Create(SmallLimits());
        REQUIRE(created.HasValue());
        RenderGraphBuilder builder = std::move(created).Value();
        std::string errorCode;
        std::thread worker{[&builder, &errorCode] {
            auto result = builder.AddPass(RenderPassKind::Graphics, RenderQueueRole::Graphics);
            errorCode = result.HasError() ? result.ErrorValue().code.Value() : "unexpected-success";
        }};
        worker.join();
        REQUIRE(errorCode == "render.graph.wrong_thread");
        REQUIRE(builder.AddPass(RenderPassKind::Graphics, RenderQueueRole::Graphics).HasValue());
    }

    SECTION("cancellation and shutdown are explicit and idempotent") {
        auto created = RenderGraphBuilder::Create(SmallLimits());
        REQUIRE(created.HasValue());
        RenderGraphBuilder builder = std::move(created).Value();
        REQUIRE(builder.AddPass(RenderPassKind::Graphics, RenderQueueRole::Graphics).HasValue());
        REQUIRE(builder.Cancel().HasValue());
        REQUIRE(builder.Cancel().HasValue());
        REQUIRE(builder.State() == RenderGraphBuilderState::Cancelled);
        RequireError(builder.AddResource(RenderGraphResourceKind::Buffer), "render.graph.builder_closed");
        builder.Shutdown();
        builder.Shutdown();
        REQUIRE(builder.State() == RenderGraphBuilderState::Shutdown);
    }

    SECTION("finalization rejects empty and closes mutation") {
        auto created = RenderGraphBuilder::Create(SmallLimits());
        REQUIRE(created.HasValue());
        RenderGraphBuilder builder = std::move(created).Value();
        RequireError(builder.Finalize(), "render.graph.empty");
        REQUIRE(builder.AddPass(RenderPassKind::Copy, RenderQueueRole::Transfer).HasValue());
        REQUIRE(builder.Finalize().HasValue());
        RequireError(builder.AddPass(RenderPassKind::Copy, RenderQueueRole::Transfer), "render.graph.builder_closed");
    }

    SECTION("move transfers ownership and closes the source") {
        auto created = RenderGraphBuilder::Create(SmallLimits());
        REQUIRE(created.HasValue());
        RenderGraphBuilder source = std::move(created).Value();
        const RenderGraphOwnerId owner = source.Owner();
        RenderGraphBuilder destination{std::move(source)};
        REQUIRE(source.State() == RenderGraphBuilderState::MovedFrom);
        REQUIRE_FALSE(source.Owner().IsValid());
        REQUIRE(destination.Owner() == owner);
        REQUIRE(destination.State() == RenderGraphBuilderState::Open);
        REQUIRE(destination.AddPass(RenderPassKind::Copy, RenderQueueRole::Graphics).HasValue());
    }
}
