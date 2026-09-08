#pragma once

#include "Horo/Runtime/Render/RenderGraph.h"

#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <string_view>
#include <utility>

namespace Horo::Render::Test {
    template <typename T> void RequireError(const Result<T> &result, const std::string_view code) {
        REQUIRE(result.HasError());
        REQUIRE(result.ErrorValue().code.Value() == code);
        REQUIRE_FALSE(result.ErrorValue().message.empty());
    }

    inline RenderGraphLimits SmallLimits() {
        return {.maxPasses = 3, .maxResources = 2, .maxUsages = 3, .maxDependencies = 2};
    }

    inline RenderBufferHandle BufferHandle(const std::uint32_t slot = 1) {
        return {{41}, slot, 1};
    }

    inline RenderTextureHandle TextureHandle(const std::uint32_t slot = 1) {
        return {{42}, slot, 1};
    }

    inline RenderGraphBuilder RequireBuilder(const RenderGraphLimits &limits = SmallLimits()) {
        auto created = RenderGraphBuilder::Create(limits);
        REQUIRE(created.HasValue());
        return std::move(created).Value();
    }

    inline RenderGraphPassRef RequirePass(RenderGraphBuilder &builder, const RenderPassKind kind, const RenderQueueRole queue,
                                          const RenderGraphPassCullPolicy cullPolicy = RenderGraphPassCullPolicy::ConservativeKeep) {
        auto added = builder.AddPass(kind, queue, cullPolicy);
        REQUIRE(added.HasValue());
        return added.Value();
    }

    inline RenderGraphResourceId RequireResource(Result<RenderGraphResourceId> added) {
        REQUIRE(added.HasValue());
        return added.Value();
    }

    inline void RequireUsage(RenderGraphBuilder &builder, const RenderGraphResourceUsage &usage) {
        REQUIRE(builder.AddUsage(usage).HasValue());
    }

    inline void RequireDependency(RenderGraphBuilder &builder, const RenderGraphDependency &dependency) {
        REQUIRE(builder.AddDependency(dependency).HasValue());
    }

    inline void RequireExport(RenderGraphBuilder &builder, const RenderGraphResourceId resource) {
        REQUIRE(builder.ExportResource(resource).HasValue());
    }

    inline RenderGraph RequireGraph(RenderGraphBuilder &builder) {
        auto finalized = builder.Finalize();
        REQUIRE(finalized.HasValue());
        return std::move(finalized).Value();
    }

    inline RenderGraphSchedule RequireSchedule(const RenderGraph &graph) {
        auto compiled = CompileRenderGraph(graph);
        REQUIRE(compiled.HasValue());
        return std::move(compiled).Value();
    }
}  // namespace Horo::Render::Test
