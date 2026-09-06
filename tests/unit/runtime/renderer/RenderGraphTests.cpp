#include "Horo/Runtime/Render/RenderGraph.h"
#include "Horo/Runtime/Render/RenderGraphErrors.h"

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>

namespace {
    using namespace Horo::Render;
}

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
    auto created = RenderGraphBuilder::Create();
    REQUIRE(created.HasValue());
    RenderGraphBuilder builder = std::move(created).Value();
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
    auto created = RenderGraphBuilder::Create(limits);
    REQUIRE(created.HasValue());
    RenderGraphBuilder builder = std::move(created).Value();

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
