#include "Horo/Runtime/Render/RenderGraph.h"
#include "Horo/Runtime/Render/RenderGraphErrors.h"

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <type_traits>
#include <utility>
#include <vector>

namespace Horo::Render {
    /** @brief Test-only construction boundary for the storage layer before builder delivery. */
    class RenderGraphBuilder final {
    public:
        static RenderGraph BuildForStorageTest(const RenderGraphOwnerId owner, const RenderGraphLimits &limits,
                                               std::vector<RenderGraphPass> passes, std::vector<RenderGraphResource> resources,
                                               std::vector<RenderGraphResourceUsage> usages,
                                               std::vector<RenderGraphDependency> dependencies) {
            return RenderGraph{owner, limits, std::move(passes), std::move(resources), std::move(usages), std::move(dependencies)};
        }
    };
}  // namespace Horo::Render

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

    const RenderGraphOwnerId owner{9};
    const RenderGraphLimits limits{.maxPasses = 1, .maxResources = 1, .maxUsages = 1, .maxDependencies = 1};
    const RenderGraphPassRef pass{owner, RenderPassId{1}};
    const RenderGraphResourceId resource{owner, 1};
    RenderGraph graph =
        RenderGraphBuilder::BuildForStorageTest(owner, limits, {{pass, RenderPassKind::Graphics, RenderQueueRole::Graphics}},
                                                {{resource, RenderGraphResourceKind::Texture}},
                                                {{pass, resource, RenderGraphAccess::Read, RenderGraphUsageKind::Sampled}},
                                                {{pass, pass, RenderGraphDependencyKind::ExecutionOrder}});

    REQUIRE(graph.Owner() == owner);
    REQUIRE(graph.Limits().maxPasses == 1);
    REQUIRE(graph.Passes().front().reference == pass);
    REQUIRE(graph.Resources().front().id == resource);
    REQUIRE(graph.Usages().front().resource == resource);
    REQUIRE(graph.Dependencies().front().kind == RenderGraphDependencyKind::ExecutionOrder);

    RenderGraph moved{std::move(graph)};
    REQUIRE_FALSE(graph.Owner().IsValid());
    REQUIRE(graph.Passes().empty());
    REQUIRE(moved.Owner() == owner);

    RenderGraph assigned = RenderGraphBuilder::BuildForStorageTest({10}, limits, {}, {}, {}, {});
    assigned = std::move(moved);
    REQUIRE_FALSE(moved.Owner().IsValid());
    REQUIRE(moved.Passes().empty());
    REQUIRE(assigned.Owner() == owner);
}
