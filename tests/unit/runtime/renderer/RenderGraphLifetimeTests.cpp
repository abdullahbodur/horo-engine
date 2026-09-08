#include "Horo/Runtime/Render/RenderGraphLifetime.h"
#include "Horo/Runtime/Render/RenderGraphLifetimeErrors.h"
#include "RenderGraphTestUtils.h"

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <string>
#include <type_traits>
#include <utility>

namespace {
    using namespace Horo;
    using namespace Horo::Render;
    using namespace Horo::Render::Test;

    RenderBufferDescriptor BufferDescriptor(const std::size_t bytes = 256) {
        return {.byteSize = bytes, .usage = RenderBufferUsage::Storage, .access = RenderBufferAccess::DeviceLocal};
    }

    RenderTextureDescriptor TextureDescriptor(const RenderTextureFormat format = RenderTextureFormat::Rgba8Unorm) {
        return {.dimension = RenderTextureDimension::TwoD,
                .extent = {64, 64},
                .format = format,
                .mipCount = 1,
                .layerCount = 1,
                .sampleCount = 1,
                .usage = RenderTextureUsage::RenderAttachment | RenderTextureUsage::Sampled | RenderTextureUsage::Storage,
                .depth = 1};
    }

    RenderGraphLifetimePlan RequireLifetimePlan(const RenderGraph &graph, const RenderGraphSchedule &schedule,
                                                const std::span<const RenderGraphTransientRequirement> requirements) {
        auto result = CompileRenderGraphLifetimePlan(graph, schedule, requirements);
        REQUIRE(result.HasValue());
        return std::move(result).Value();
    }

    const RenderGraphResourceLifetime &Lifetime(const RenderGraphLifetimePlan &plan, const RenderGraphResourceId resource) {
        REQUIRE(resource.value > 0);
        REQUIRE(resource.value <= plan.Lifetimes().size());
        return plan.Lifetimes()[resource.value - 1];
    }

    void RequireDistinctSlots(const RenderGraph &graph, const RenderGraphSchedule &schedule,
                              const std::span<const RenderGraphTransientRequirement> requirements) {
        RenderGraphLifetimePlan plan = RequireLifetimePlan(graph, schedule, requirements);
        REQUIRE(plan.AliasOpportunities().empty());
        REQUIRE(plan.AllocationRequirements().size() == 2);
        REQUIRE(plan.AllocationRequirements().front().slot != plan.AllocationRequirements().back().slot);
    }

    void RequireAttachmentFormatRejected(const RenderGraphUsageKind usage, const RenderTextureFormat format) {
        RenderGraphBuilder builder = RequireBuilder();
        const auto texture = RequireResource(builder.AddTransientResource(RenderGraphResourceKind::Texture));
        const auto pass = RequirePass(builder, RenderPassKind::Graphics, RenderQueueRole::Graphics);
        RequireUsage(builder, {pass, texture, RenderGraphAccess::Write, usage});
        RenderGraph graph = RequireGraph(builder);
        RenderGraphSchedule schedule = RequireSchedule(graph);
        const std::array requirements{RenderGraphTransientRequirement{texture, TextureDescriptor(format)}};
        RequireError(CompileRenderGraphLifetimePlan(graph, schedule, requirements), "render.graph.lifetime.descriptor_invalid");
    }

    struct SingleBufferPlanFixture {
        RenderGraph graph;
        RenderGraphSchedule schedule;
        RenderGraphLifetimePlan plan;
    };

    SingleBufferPlanFixture CompileSingleBufferPlan(RenderGraphBuilder &builder, const RenderGraphPassRef pass,
                                                    const RenderGraphResourceId resource) {
        RequireUsage(builder, {pass, resource, RenderGraphAccess::Write, RenderGraphUsageKind::Storage});
        RenderGraph graph = RequireGraph(builder);
        RenderGraphSchedule schedule = RequireSchedule(graph);
        const std::array requirements{RenderGraphTransientRequirement{resource, BufferDescriptor()}};
        RenderGraphLifetimePlan plan = RequireLifetimePlan(graph, schedule, requirements);
        return {std::move(graph), std::move(schedule), std::move(plan)};
    }
}  // namespace

TEST_CASE("Render graph lifetime plan records retained first and last uses", "[runtime][renderer][render-graph][lifetime]") {
    RenderGraphLimits limits{.maxPasses = 3, .maxResources = 2, .maxUsages = 4, .maxDependencies = 2};
    RenderGraphBuilder builder = RequireBuilder(limits);
    const auto first = RequirePass(builder, RenderPassKind::Compute, RenderQueueRole::Compute);
    const auto middle = RequirePass(builder, RenderPassKind::Compute, RenderQueueRole::Compute);
    const auto last = RequirePass(builder, RenderPassKind::Compute, RenderQueueRole::Compute);
    const auto transient = RequireResource(builder.AddTransientResource(RenderGraphResourceKind::Buffer));
    const auto imported = RequireResource(builder.ImportBuffer(BufferHandle(), RenderGraphResourceClass::Persistent));
    RequireUsage(builder, {first, transient, RenderGraphAccess::Write, RenderGraphUsageKind::Storage});
    RequireUsage(builder, {middle, imported, RenderGraphAccess::Read, RenderGraphUsageKind::Storage});
    RequireUsage(builder, {last, transient, RenderGraphAccess::Read, RenderGraphUsageKind::Storage});
    RequireUsage(builder, {last, imported, RenderGraphAccess::Read, RenderGraphUsageKind::Storage});
    RequireDependency(builder, {first, middle, RenderGraphDependencyKind::ExecutionOrder});
    RequireDependency(builder, {middle, last, RenderGraphDependencyKind::ExecutionOrder});

    RenderGraph graph = RequireGraph(builder);
    RenderGraphSchedule schedule = RequireSchedule(graph);
    const std::array requirements{RenderGraphTransientRequirement{transient, BufferDescriptor()}};
    RenderGraphLifetimePlan plan = RequireLifetimePlan(graph, schedule, requirements);

    REQUIRE(plan.Owner() == graph.Owner());
    REQUIRE(Lifetime(plan, transient).firstPass == first);
    REQUIRE(Lifetime(plan, transient).lastPass == last);
    REQUIRE(Lifetime(plan, transient).firstUseIndex == 0);
    REQUIRE(Lifetime(plan, transient).lastUseIndex == 2);
    REQUIRE(Lifetime(plan, imported).firstPass == middle);
    REQUIRE(plan.AllocationRequirements().size() == 1);
    REQUIRE(plan.AllocationRequirements()[0].resource == transient);
    REQUIRE(plan.AllocationRequirements()[0].compatibilityClass.IsValid());
    REQUIRE(plan.AllocationRequirements()[0].slot.IsValid());
}

TEST_CASE("Render graph lifetime plan excludes culled-only transient uses", "[runtime][renderer][render-graph][lifetime]") {
    RenderGraphBuilder builder = RequireBuilder();
    const auto culled =
        RequirePass(builder, RenderPassKind::Compute, RenderQueueRole::Compute, RenderGraphPassCullPolicy::AllowCullIfOutputsUnused);
    const auto transient = RequireResource(builder.AddTransientResource(RenderGraphResourceKind::Buffer));
    SingleBufferPlanFixture fixture = CompileSingleBufferPlan(builder, culled, transient);

    REQUIRE(fixture.schedule.OrderedPasses().empty());
    REQUIRE(Lifetime(fixture.plan, transient).disposition == RenderGraphLifetimeDisposition::Unused);
    REQUIRE_FALSE(Lifetime(fixture.plan, transient).firstPass.IsValid());
    REQUIRE(fixture.plan.AllocationRequirements().empty());
    REQUIRE(fixture.plan.AliasOpportunities().empty());
}

TEST_CASE("Render graph lifetime plan assigns deterministic compatible alias slots", "[runtime][renderer][render-graph][lifetime]") {
    RenderGraphLimits limits{.maxPasses = 3, .maxResources = 3, .maxUsages = 3, .maxDependencies = 2};
    RenderGraphBuilder builder = RequireBuilder(limits);
    const auto first = RequirePass(builder, RenderPassKind::Compute, RenderQueueRole::Compute);
    const auto second = RequirePass(builder, RenderPassKind::Compute, RenderQueueRole::Compute);
    const auto third = RequirePass(builder, RenderPassKind::Compute, RenderQueueRole::Compute);
    const auto a = RequireResource(builder.AddTransientResource(RenderGraphResourceKind::Buffer));
    const auto b = RequireResource(builder.AddTransientResource(RenderGraphResourceKind::Buffer));
    const auto incompatible = RequireResource(builder.AddTransientResource(RenderGraphResourceKind::Buffer));
    RequireUsage(builder, {first, a, RenderGraphAccess::Write, RenderGraphUsageKind::Storage});
    RequireUsage(builder, {second, incompatible, RenderGraphAccess::Write, RenderGraphUsageKind::Storage});
    RequireUsage(builder, {third, b, RenderGraphAccess::Write, RenderGraphUsageKind::Storage});
    RequireDependency(builder, {first, second, RenderGraphDependencyKind::ExecutionOrder});
    RequireDependency(builder, {second, third, RenderGraphDependencyKind::ExecutionOrder});

    RenderGraph graph = RequireGraph(builder);
    RenderGraphSchedule schedule = RequireSchedule(graph);
    const std::array requirements{
        RenderGraphTransientRequirement{a, BufferDescriptor()},
        RenderGraphTransientRequirement{b, BufferDescriptor()},
        RenderGraphTransientRequirement{incompatible, BufferDescriptor(512)},
    };
    RenderGraphLifetimePlan plan = RequireLifetimePlan(graph, schedule, requirements);

    REQUIRE(plan.AllocationRequirements().size() == 3);
    const auto &firstAllocation = plan.AllocationRequirements()[0];
    const auto &secondAllocation = plan.AllocationRequirements()[1];
    const auto &thirdAllocation = plan.AllocationRequirements()[2];
    REQUIRE(firstAllocation.resource == a);
    REQUIRE(secondAllocation.resource == b);
    REQUIRE(firstAllocation.compatibilityClass == secondAllocation.compatibilityClass);
    REQUIRE(firstAllocation.slot == secondAllocation.slot);
    REQUIRE(thirdAllocation.resource == incompatible);
    REQUIRE(thirdAllocation.compatibilityClass != firstAllocation.compatibilityClass);
    REQUIRE(thirdAllocation.slot != firstAllocation.slot);
    REQUIRE(plan.AliasOpportunities().size() == 1);
    REQUIRE(plan.AliasOpportunities()[0].first == a);
    REQUIRE(plan.AliasOpportunities()[0].second == b);
}

TEST_CASE("Render graph lifetime plan keeps touching and overlapping lifetimes separate", "[runtime][renderer][render-graph][lifetime]") {
    RenderGraphLimits limits{.maxPasses = 2, .maxResources = 2, .maxUsages = 3, .maxDependencies = 1};
    RenderGraphBuilder builder = RequireBuilder(limits);
    const auto first = RequirePass(builder, RenderPassKind::Compute, RenderQueueRole::Compute);
    const auto second = RequirePass(builder, RenderPassKind::Compute, RenderQueueRole::Compute);
    const auto a = RequireResource(builder.AddTransientResource(RenderGraphResourceKind::Texture));
    const auto b = RequireResource(builder.AddTransientResource(RenderGraphResourceKind::Texture));
    RequireUsage(builder, {first, a, RenderGraphAccess::Write, RenderGraphUsageKind::Storage});
    RequireUsage(builder, {second, a, RenderGraphAccess::Read, RenderGraphUsageKind::Sampled});
    RequireUsage(builder, {second, b, RenderGraphAccess::Write, RenderGraphUsageKind::Storage});
    RequireDependency(builder, {first, second, RenderGraphDependencyKind::ResourceHazard});

    RenderGraph graph = RequireGraph(builder);
    RenderGraphSchedule schedule = RequireSchedule(graph);
    const std::array requirements{
        RenderGraphTransientRequirement{a, TextureDescriptor()},
        RenderGraphTransientRequirement{b, TextureDescriptor()},
    };
    RequireDistinctSlots(graph, schedule, requirements);
}

TEST_CASE("Render graph lifetime plan does not alias across queue roles", "[runtime][renderer][render-graph][lifetime]") {
    for (const bool dependent : {false, true}) {
        RenderGraphLimits limits{.maxPasses = 2, .maxResources = 2, .maxUsages = 2, .maxDependencies = 1};
        RenderGraphBuilder builder = RequireBuilder(limits);
        const auto graphics = RequirePass(builder, RenderPassKind::Graphics, RenderQueueRole::Graphics);
        const auto compute = RequirePass(builder, RenderPassKind::Compute, RenderQueueRole::Compute);
        const auto first = RequireResource(builder.AddTransientResource(RenderGraphResourceKind::Buffer));
        const auto second = RequireResource(builder.AddTransientResource(RenderGraphResourceKind::Buffer));
        RequireUsage(builder, {graphics, first, RenderGraphAccess::Write, RenderGraphUsageKind::Storage});
        RequireUsage(builder, {compute, second, RenderGraphAccess::Write, RenderGraphUsageKind::Storage});
        if (dependent) {
            RequireDependency(builder, {graphics, compute, RenderGraphDependencyKind::ExecutionOrder});
        }

        RenderGraph graph = RequireGraph(builder);
        RenderGraphSchedule schedule = RequireSchedule(graph);
        const std::array requirements{
            RenderGraphTransientRequirement{first, BufferDescriptor()},
            RenderGraphTransientRequirement{second, BufferDescriptor()},
        };
        RequireDistinctSlots(graph, schedule, requirements);
    }
}

TEST_CASE("Render graph lifetime plan validates attachment format classes", "[runtime][renderer][render-graph][lifetime]") {
    SECTION("color attachment rejects depth format") {
        RequireAttachmentFormatRejected(RenderGraphUsageKind::ColorAttachment, RenderTextureFormat::Depth32Float);
    }

    SECTION("depth attachment rejects color format") {
        RequireAttachmentFormatRejected(RenderGraphUsageKind::DepthStencilAttachment, RenderTextureFormat::Rgba8Unorm);
    }
}

TEST_CASE("Render graph alias chains obey their exact finite capacity", "[runtime][renderer][render-graph][lifetime]") {
    RenderGraphLimits graphLimits{.maxPasses = 4, .maxResources = 4, .maxUsages = 4, .maxDependencies = 3};
    RenderGraphBuilder builder = RequireBuilder(graphLimits);
    std::array<RenderGraphPassRef, 4> passes;
    std::array<RenderGraphResourceId, 4> resources;
    for (std::size_t index = 0; index < passes.size(); ++index) {
        passes[index] = RequirePass(builder, RenderPassKind::Compute, RenderQueueRole::Compute);
        resources[index] = RequireResource(builder.AddTransientResource(RenderGraphResourceKind::Buffer));
        RequireUsage(builder, {passes[index], resources[index], RenderGraphAccess::Write, RenderGraphUsageKind::Storage});
        if (index > 0) {
            RequireDependency(builder, {passes[index - 1], passes[index], RenderGraphDependencyKind::ExecutionOrder});
        }
    }
    RenderGraph graph = RequireGraph(builder);
    RenderGraphSchedule schedule = RequireSchedule(graph);
    const std::array requirements{
        RenderGraphTransientRequirement{resources[0], BufferDescriptor()},
        RenderGraphTransientRequirement{resources[1], BufferDescriptor()},
        RenderGraphTransientRequirement{resources[2], BufferDescriptor()},
        RenderGraphTransientRequirement{resources[3], BufferDescriptor()},
    };

    RenderGraphLifetimePlan plan = RequireLifetimePlan(graph, schedule, requirements);
    REQUIRE(plan.AliasOpportunities().size() == resources.size() - 1);
    REQUIRE(plan.AliasOpportunities()[0].first == resources[0]);
    REQUIRE(plan.AliasOpportunities()[0].second == resources[1]);
    REQUIRE(plan.AliasOpportunities()[1].first == resources[1]);
    REQUIRE(plan.AliasOpportunities()[1].second == resources[2]);
    REQUIRE(plan.AliasOpportunities()[2].first == resources[2]);
    REQUIRE(plan.AliasOpportunities()[2].second == resources[3]);
    REQUIRE(plan.AllocationRequirements()[0].slot == plan.AllocationRequirements()[3].slot);

    const RenderGraphLifetimeLimits exactLimit{.maxAliasOpportunities = resources.size() - 1};
    REQUIRE(CompileRenderGraphLifetimePlan(graph, schedule, requirements, exactLimit).HasValue());
    const RenderGraphLifetimeLimits exhausted{.maxAliasOpportunities = resources.size() - 2};
    RequireError(CompileRenderGraphLifetimePlan(graph, schedule, requirements, exhausted), "render.graph.lifetime.capacity_exceeded");
    const RenderGraphLifetimeLimits invalid{
        .maxAliasOpportunities = RenderGraphLifetimeLimits::HardMaxAliasOpportunities + 1,
    };
    RequireError(CompileRenderGraphLifetimePlan(graph, schedule, requirements, invalid), "render.graph.lifetime.limits_invalid");
}

TEST_CASE("Render graph lifetime planning rejects invalid requirement sets", "[runtime][renderer][render-graph][lifetime]") {
    RenderGraphBuilder builder = RequireBuilder();
    const auto transient = RequireResource(builder.AddTransientResource(RenderGraphResourceKind::Buffer));
    const auto pass = RequirePass(builder, RenderPassKind::Compute, RenderQueueRole::Compute);
    const auto imported = RequireResource(builder.ImportBuffer(BufferHandle(), RenderGraphResourceClass::Persistent));
    RequireUsage(builder, {pass, transient, RenderGraphAccess::Write, RenderGraphUsageKind::Storage});
    RenderGraph graph = RequireGraph(builder);
    RenderGraphSchedule schedule = RequireSchedule(graph);

    SECTION("missing") {
        RequireError(CompileRenderGraphLifetimePlan(graph, schedule, {}), "render.graph.lifetime.requirement_missing");
    }
    SECTION("duplicate") {
        const std::array requirements{
            RenderGraphTransientRequirement{transient, BufferDescriptor()},
            RenderGraphTransientRequirement{transient, BufferDescriptor()},
        };
        RequireError(CompileRenderGraphLifetimePlan(graph, schedule, requirements), "render.graph.lifetime.requirement_duplicate");
    }
    SECTION("imported") {
        const std::array requirements{
            RenderGraphTransientRequirement{transient, BufferDescriptor()},
            RenderGraphTransientRequirement{imported, BufferDescriptor()},
        };
        RequireError(CompileRenderGraphLifetimePlan(graph, schedule, requirements), "render.graph.lifetime.requirement_unexpected");
    }
    SECTION("kind mismatch") {
        const std::array requirements{RenderGraphTransientRequirement{transient, TextureDescriptor()}};
        RequireError(CompileRenderGraphLifetimePlan(graph, schedule, requirements), "render.graph.lifetime.descriptor_invalid");
    }
    SECTION("invalid descriptor") {
        const std::array requirements{RenderGraphTransientRequirement{transient, BufferDescriptor(0)}};
        RequireError(CompileRenderGraphLifetimePlan(graph, schedule, requirements), "render.graph.lifetime.descriptor_invalid");
    }
    SECTION("descriptor lacks declared usage") {
        RenderBufferDescriptor descriptor = BufferDescriptor();
        descriptor.usage = RenderBufferUsage::CopyDestination;
        const std::array requirements{RenderGraphTransientRequirement{transient, descriptor}};
        RequireError(CompileRenderGraphLifetimePlan(graph, schedule, requirements), "render.graph.lifetime.descriptor_invalid");
    }
}

TEST_CASE("Render graph lifetime planning rejects inputs from different owners", "[runtime][renderer][render-graph][lifetime]") {
    RenderGraphBuilder firstBuilder = RequireBuilder();
    const auto firstPass = RequirePass(firstBuilder, RenderPassKind::Compute, RenderQueueRole::Compute);
    const auto firstResource = RequireResource(firstBuilder.AddTransientResource(RenderGraphResourceKind::Buffer));
    RequireUsage(firstBuilder, {firstPass, firstResource, RenderGraphAccess::Write, RenderGraphUsageKind::Storage});
    RenderGraph firstGraph = RequireGraph(firstBuilder);

    RenderGraphBuilder secondBuilder = RequireBuilder();
    const auto secondPass = RequirePass(secondBuilder, RenderPassKind::Compute, RenderQueueRole::Compute);
    const auto secondResource = RequireResource(secondBuilder.AddTransientResource(RenderGraphResourceKind::Buffer));
    RequireUsage(secondBuilder, {secondPass, secondResource, RenderGraphAccess::Write, RenderGraphUsageKind::Storage});
    RenderGraph secondGraph = RequireGraph(secondBuilder);
    RenderGraphSchedule secondSchedule = RequireSchedule(secondGraph);

    const std::array requirements{RenderGraphTransientRequirement{firstResource, BufferDescriptor()}};
    RequireError(CompileRenderGraphLifetimePlan(firstGraph, secondSchedule, requirements), "render.graph.lifetime.owner_mismatch");
}

TEST_CASE("Render graph lifetime plan has explicit move-only ownership", "[runtime][renderer][render-graph][lifetime]") {
    STATIC_REQUIRE_FALSE(std::is_copy_constructible_v<RenderGraphLifetimePlan>);
    STATIC_REQUIRE_FALSE(std::is_copy_assignable_v<RenderGraphLifetimePlan>);
    STATIC_REQUIRE(std::is_move_constructible_v<RenderGraphLifetimePlan>);
    STATIC_REQUIRE(std::is_move_assignable_v<RenderGraphLifetimePlan>);

    RenderGraphBuilder builder = RequireBuilder();
    const auto transient = RequireResource(builder.AddTransientResource(RenderGraphResourceKind::Buffer));
    const auto pass = RequirePass(builder, RenderPassKind::Compute, RenderQueueRole::Compute);
    SingleBufferPlanFixture fixture = CompileSingleBufferPlan(builder, pass, transient);
    RenderGraphLifetimePlan plan{std::move(fixture.plan)};
    RenderGraphLifetimePlan moved{std::move(plan)};
    REQUIRE_FALSE(plan.Owner().IsValid());
    REQUIRE(moved.Owner() == fixture.graph.Owner());
    REQUIRE(moved.AllocationRequirements().size() == 1);
}

TEST_CASE("Render graph lifetime errors expose actionable stable identities", "[runtime][renderer][render-graph][lifetime]") {
    const std::array descriptors{
        &RenderGraphLifetimeErrors::AllocationFailed,  &RenderGraphLifetimeErrors::CapacityExceeded,
        &RenderGraphLifetimeErrors::DescriptorInvalid, &RenderGraphLifetimeErrors::DuplicateRequirement,
        &RenderGraphLifetimeErrors::InvalidGraph,      &RenderGraphLifetimeErrors::InvalidLimits,
        &RenderGraphLifetimeErrors::InvalidSchedule,   &RenderGraphLifetimeErrors::MissingRequirement,
        &RenderGraphLifetimeErrors::OwnerMismatch,     &RenderGraphLifetimeErrors::UnexpectedRequirement,
    };
    for (const ErrorCodeDescriptor *descriptor : descriptors) {
        REQUIRE(descriptor->domain.Value() == "render.graph.lifetime");
        REQUIRE(descriptor->code.Value().starts_with("render.graph.lifetime."));
        REQUIRE_FALSE(descriptor->summary.empty());
        REQUIRE_FALSE(descriptor->remediationHint.empty());
    }
    REQUIRE(RenderGraphLifetimeErrors::AllocationFailed.retryable);
}
