#include "Horo/Runtime/Render/RenderGraph.h"
#include "Horo/Runtime/Render/RenderGraphErrors.h"

#include <catch2/catch_test_macros.hpp>
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
    REQUIRE(RenderGraphErrors::WrongOwner.domain.Value() == "render.graph");
    REQUIRE(RenderGraphErrors::WrongOwner.code.Value() == "render.graph.wrong_owner");
    REQUIRE_FALSE(RenderGraphErrors::WrongOwner.summary.empty());
    REQUIRE_FALSE(RenderGraphErrors::WrongOwner.remediationHint.empty());
    REQUIRE(RenderGraphErrors::AllocationFailed.retryable);
}

TEST_CASE("Render graph storage is move-only and immutable through its views", "[runtime][renderer][render-graph]") {
    STATIC_REQUIRE_FALSE(std::is_copy_constructible_v<RenderGraph>);
    STATIC_REQUIRE_FALSE(std::is_copy_assignable_v<RenderGraph>);
    STATIC_REQUIRE(std::is_move_constructible_v<RenderGraph>);
    STATIC_REQUIRE(std::is_same_v<decltype(std::declval<const RenderGraph &>().Passes()), std::span<const RenderGraphPass>>);
}
