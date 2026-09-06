#include "Horo/Runtime/Render/RenderGraph.h"
#include "Horo/Runtime/Render/RenderGraphErrors.h"

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
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

    RenderGraphBuilder RequireBuilder(const RenderGraphLimits &limits = SmallLimits()) {
        auto created = RenderGraphBuilder::Create(limits);
        REQUIRE(created.HasValue());
        return std::move(created).Value();
    }
}  // namespace

TEST_CASE("Render graph records retain backend-neutral typed identities", "[runtime][renderer][render-graph]") {
    const RenderGraphOwnerId owner{7};
    const RenderGraphPassRef pass{owner, RenderPassId{3}};
    const RenderGraphResourceId resource{owner, 5};
    const RenderGraphResourceUsage usage{pass, resource, RenderGraphAccess::Read, RenderGraphUsageKind::Sampled};
    const RenderGraphDependency dependency{pass, {owner, RenderPassId{4}}, RenderGraphDependencyKind::ExecutionOrder};

    REQUIRE(owner.IsValid());
    REQUIRE(pass.IsValid());
    REQUIRE(resource.IsValid());
    REQUIRE(usage.pass == pass);
    REQUIRE(usage.resource == resource);
    REQUIRE(dependency.before == pass);
    REQUIRE(dependency.after.id == RenderPassId{4});
}

TEST_CASE("Render graph finite limits enforce exact hard boundaries", "[runtime][renderer][render-graph]") {
    REQUIRE(RenderGraphLimits{}.IsValid());
    REQUIRE(RenderGraphLimits{.maxPasses = RenderGraphLimits::HardMaxPasses,
                              .maxResources = RenderGraphLimits::HardMaxResources,
                              .maxUsages = RenderGraphLimits::HardMaxUsages,
                              .maxDependencies = RenderGraphLimits::HardMaxDependencies}
                .IsValid());

    RenderGraphLimits zero = {};
    zero.maxPasses = 0;
    REQUIRE_FALSE(zero.IsValid());

    RenderGraphLimits oversized = {};
    oversized.maxDependencies = RenderGraphLimits::HardMaxDependencies + 1;
    REQUIRE_FALSE(oversized.IsValid());
}

TEST_CASE("Render graph errors expose stable actionable identities", "[runtime][renderer][render-graph]") {
    const std::array descriptors{
        &RenderGraphErrors::AllocationFailed,    &RenderGraphErrors::BuilderClosed,        &RenderGraphErrors::CapacityExceeded,
        &RenderGraphErrors::EmptyGraph,          &RenderGraphErrors::IncompatibleQueue,    &RenderGraphErrors::InvalidDependency,
        &RenderGraphErrors::InvalidLimits,       &RenderGraphErrors::InvalidPass,          &RenderGraphErrors::InvalidResource,
        &RenderGraphErrors::InvalidUsage,        &RenderGraphErrors::OwnerExhausted,       &RenderGraphErrors::UnsupportedDependencyKind,
        &RenderGraphErrors::UnsupportedPassKind, &RenderGraphErrors::UnsupportedQueueRole, &RenderGraphErrors::UnsupportedResourceKind,
        &RenderGraphErrors::UnsupportedUsage,    &RenderGraphErrors::WrongOwner,           &RenderGraphErrors::WrongThread,
    };

    for (const Horo::ErrorCodeDescriptor *descriptor : descriptors) {
        REQUIRE(descriptor->domain.Value() == "render.graph");
        REQUIRE(descriptor->code.Value().starts_with("render.graph."));
        REQUIRE_FALSE(descriptor->summary.empty());
        REQUIRE_FALSE(descriptor->remediationHint.empty());
    }

    REQUIRE(RenderGraphErrors::AllocationFailed.retryable);
    REQUIRE(RenderGraphErrors::OwnerExhausted.defaultSeverity == Horo::ErrorSeverity::Critical);
}

TEST_CASE("Render graph storage is move-only and immutable through its views", "[runtime][renderer][render-graph]") {
    STATIC_REQUIRE_FALSE(std::is_copy_constructible_v<RenderGraph>);
    STATIC_REQUIRE_FALSE(std::is_copy_assignable_v<RenderGraph>);
    STATIC_REQUIRE(std::is_move_constructible_v<RenderGraph>);
    STATIC_REQUIRE(std::is_same_v<decltype(std::declval<const RenderGraph &>().Passes()), std::span<const RenderGraphPass>>);
}

TEST_CASE("Render graph builder owns finite storage and explicit lifecycle", "[runtime][renderer][render-graph]") {
    RenderGraphBuilder builder = RequireBuilder();
    const RenderGraphOwnerId owner = builder.Owner();
    REQUIRE(owner.IsValid());
    REQUIRE(builder.State() == RenderGraphBuilderState::Open);

    RenderGraphBuilder moved{std::move(builder)};
    REQUIRE(builder.State() == RenderGraphBuilderState::MovedFrom);
    REQUIRE_FALSE(builder.Owner().IsValid());
    REQUIRE(moved.Owner() == owner);

    moved.Shutdown();
    moved.Shutdown();
    REQUIRE(moved.State() == RenderGraphBuilderState::Shutdown);

    RenderGraphLimits invalid = {};
    invalid.maxPasses = 0;
    const auto rejected = RenderGraphBuilder::Create(invalid);
    REQUIRE(rejected.HasError());
    REQUIRE(rejected.ErrorValue().code.Value() == "render.graph.limits_invalid");
}

TEST_CASE("Render graph pass authoring rejects unsupported and incompatible queues", "[runtime][renderer][render-graph]") {
    RenderGraphLimits limits = {};
    limits.maxPasses = 1;
    RenderGraphBuilder builder = RequireBuilder(limits);

    REQUIRE(builder.AddPass(RenderPassKind::Graphics, RenderQueueRole::Compute).ErrorValue().code.Value() ==
            "render.graph.queue_incompatible");
    REQUIRE(builder.AddPass(static_cast<RenderPassKind>(255), RenderQueueRole::Graphics).ErrorValue().code.Value() ==
            "render.graph.pass_kind_unsupported");
    REQUIRE(builder.AddPass(RenderPassKind::Graphics, static_cast<RenderQueueRole>(255)).ErrorValue().code.Value() ==
            "render.graph.queue_role_unsupported");

    auto pass = builder.AddPass(RenderPassKind::Graphics, RenderQueueRole::Graphics);
    REQUIRE(pass.HasValue());
    REQUIRE(pass.Value().id == RenderPassId{1});
    REQUIRE(builder.AddPass(RenderPassKind::Graphics, RenderQueueRole::Graphics).ErrorValue().code.Value() ==
            "render.graph.capacity_exceeded");

    std::string threadError;
    std::thread worker{[&builder, &threadError] {
        threadError = builder.AddPass(RenderPassKind::Copy, RenderQueueRole::Transfer).ErrorValue().code.Value();
    }};
    worker.join();
    REQUIRE(threadError == "render.graph.wrong_thread");
}

TEST_CASE("Render graph builder finalizes typed records in authoring order", "[runtime][renderer][render-graph]") {
    RenderGraphBuilder builder = RequireBuilder();

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
    RequireError(builder.AddResource(RenderGraphResourceKind::Buffer), "render.graph.builder_closed");
}

TEST_CASE("Render graph capacities bound every authored record", "[runtime][renderer][render-graph]") {
    SECTION("pass resource and usage capacities") {
        const RenderGraphLimits limits{.maxPasses = 1, .maxResources = 1, .maxUsages = 1, .maxDependencies = 1};
        RenderGraphBuilder builder = RequireBuilder(limits);
        auto pass = builder.AddPass(RenderPassKind::Graphics, RenderQueueRole::Graphics);
        auto resource = builder.AddResource(RenderGraphResourceKind::Texture);
        REQUIRE(pass.HasValue());
        REQUIRE(resource.HasValue());
        REQUIRE(
            builder.AddUsage({pass.Value(), resource.Value(), RenderGraphAccess::Write, RenderGraphUsageKind::ColorAttachment}).HasValue());
        RequireError(builder.AddResource(RenderGraphResourceKind::Buffer), "render.graph.capacity_exceeded");
        RequireError(builder.AddUsage(
                         {pass.Value(), resource.Value(), RenderGraphAccess::Read, RenderGraphUsageKind::DepthStencilAttachment}),
                     "render.graph.capacity_exceeded");
    }

    SECTION("dependency capacity") {
        const RenderGraphLimits limits{.maxPasses = 2, .maxResources = 1, .maxUsages = 1, .maxDependencies = 1};
        RenderGraphBuilder builder = RequireBuilder(limits);
        auto first = builder.AddPass(RenderPassKind::Graphics, RenderQueueRole::Graphics);
        auto second = builder.AddPass(RenderPassKind::Compute, RenderQueueRole::Compute);
        REQUIRE(first.HasValue());
        REQUIRE(second.HasValue());
        REQUIRE(builder.AddDependency({first.Value(), second.Value(), RenderGraphDependencyKind::ExecutionOrder}).HasValue());
        RequireError(builder.AddDependency({second.Value(), first.Value(), RenderGraphDependencyKind::ExecutionOrder}),
                     "render.graph.capacity_exceeded");
    }
}

TEST_CASE("Render graph rejects invalid and foreign records without fallback", "[runtime][renderer][render-graph]") {
    RenderGraphBuilder first = RequireBuilder();
    RenderGraphBuilder second = RequireBuilder();

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

TEST_CASE("Render graph supports depth read and explicit cancellation", "[runtime][renderer][render-graph]") {
    RenderGraphBuilder builder = RequireBuilder();
    auto pass = builder.AddPass(RenderPassKind::Graphics, RenderQueueRole::Graphics);
    auto texture = builder.AddResource(RenderGraphResourceKind::Texture);
    REQUIRE(pass.HasValue());
    REQUIRE(texture.HasValue());
    REQUIRE(builder.AddUsage({pass.Value(), texture.Value(), RenderGraphAccess::Read, RenderGraphUsageKind::DepthStencilAttachment})
                .HasValue());
    REQUIRE(builder.Cancel().HasValue());
    REQUIRE(builder.Cancel().HasValue());
    REQUIRE(builder.State() == RenderGraphBuilderState::Cancelled);
    RequireError(builder.Finalize(), "render.graph.builder_closed");

    RenderGraphBuilder empty = RequireBuilder();
    RequireError(empty.Finalize(), "render.graph.empty");
}

TEST_CASE("Render graph mutation and cancellation enforce owner-thread affinity", "[runtime][renderer][render-graph]") {
    RenderGraphBuilder builder = RequireBuilder();
    auto pass = builder.AddPass(RenderPassKind::Graphics, RenderQueueRole::Graphics);
    auto resource = builder.AddResource(RenderGraphResourceKind::Texture);
    REQUIRE(pass.HasValue());
    REQUIRE(resource.HasValue());

    std::array<std::string, 5> errors;
    std::thread worker{[&] {
        errors[0] = builder.AddResource(RenderGraphResourceKind::Buffer).ErrorValue().code.Value();
        errors[1] = builder.AddUsage({pass.Value(), resource.Value(), RenderGraphAccess::Read, RenderGraphUsageKind::Sampled})
                        .ErrorValue()
                        .code.Value();
        errors[2] = builder.Finalize().ErrorValue().code.Value();
        errors[3] = builder.Cancel().ErrorValue().code.Value();
        errors[4] = builder.AddDependency({pass.Value(), {builder.Owner(), RenderPassId{2}}, RenderGraphDependencyKind::ExecutionOrder})
                        .ErrorValue()
                        .code.Value();
    }};
    worker.join();
    for (const std::string &error : errors) {
        REQUIRE(error == "render.graph.wrong_thread");
    }
}

TEST_CASE("Render graph owner identities remain unique under contention", "[runtime][renderer][render-graph]") {
    constexpr std::size_t BuilderCount = 16;
    std::array<RenderGraphOwnerId, BuilderCount> owners{};
    std::array<bool, BuilderCount> createdSuccessfully{};
    std::array<std::thread, BuilderCount> workers;

    for (std::size_t index = 0; index < BuilderCount; ++index) {
        workers[index] = std::thread{[index, &owners, &createdSuccessfully] {
            auto created = RenderGraphBuilder::Create(SmallLimits());
            createdSuccessfully[index] = created.HasValue();
            if (created.HasValue()) {
                owners[index] = created.Value().Owner();
            }
        }};
    }
    for (std::thread &worker : workers) {
        worker.join();
    }
    for (std::size_t left = 0; left < owners.size(); ++left) {
        REQUIRE(createdSuccessfully[left]);
        REQUIRE(owners[left].IsValid());
        for (std::size_t right = left + 1; right < owners.size(); ++right) {
            REQUIRE(owners[left] != owners[right]);
        }
    }
}
