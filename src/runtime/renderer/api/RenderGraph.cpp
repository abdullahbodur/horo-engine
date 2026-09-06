#include "Horo/Runtime/Render/RenderGraph.h"

#include <utility>

namespace Horo::Render {
    /** @copydoc RenderGraph::RenderGraph */
    RenderGraph::RenderGraph(const RenderGraphOwnerId owner, const RenderGraphLimits limits, std::vector<RenderGraphPass> passes,
                             std::vector<RenderGraphResource> resources, std::vector<RenderGraphResourceUsage> usages,
                             std::vector<RenderGraphDependency> dependencies) noexcept
        : owner_(owner), limits_(limits), passes_(std::move(passes)), resources_(std::move(resources)), usages_(std::move(usages)),
          dependencies_(std::move(dependencies)) {}

    /** @copydoc RenderGraph::Owner */
    RenderGraphOwnerId RenderGraph::Owner() const noexcept {
        return owner_;
    }

    /** @copydoc RenderGraph::Limits */
    const RenderGraphLimits &RenderGraph::Limits() const noexcept {
        return limits_;
    }

    /** @copydoc RenderGraph::Passes */
    std::span<const RenderGraphPass> RenderGraph::Passes() const noexcept {
        return {passes_.data(), passes_.size()};
    }

    /** @copydoc RenderGraph::Resources */
    std::span<const RenderGraphResource> RenderGraph::Resources() const noexcept {
        return {resources_.data(), resources_.size()};
    }

    /** @copydoc RenderGraph::Usages */
    std::span<const RenderGraphResourceUsage> RenderGraph::Usages() const noexcept {
        return {usages_.data(), usages_.size()};
    }

    /** @copydoc RenderGraph::Dependencies */
    std::span<const RenderGraphDependency> RenderGraph::Dependencies() const noexcept {
        return {dependencies_.data(), dependencies_.size()};
    }
}  // namespace Horo::Render
