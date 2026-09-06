#include "Horo/Runtime/Render/RenderGraph.h"

#include <utility>

namespace Horo::Render {
    /** @copydoc RenderGraph::RenderGraph */
    RenderGraph::RenderGraph(const RenderGraphOwnerId owner, const RenderGraphLimits &limits, std::vector<RenderGraphPass> passes,
                             std::vector<RenderGraphResource> resources, std::vector<RenderGraphResourceUsage> usages,
                             std::vector<RenderGraphDependency> dependencies) noexcept
        : owner_(owner), limits_(limits), passes_(std::move(passes)), resources_(std::move(resources)), usages_(std::move(usages)),
          dependencies_(std::move(dependencies)) {}

    /** @copydoc RenderGraph::RenderGraph(RenderGraph &&) */
    RenderGraph::RenderGraph(RenderGraph &&other) noexcept
        : owner_(std::exchange(other.owner_, {})), limits_(std::exchange(other.limits_, {})), passes_(std::exchange(other.passes_, {})),
          resources_(std::exchange(other.resources_, {})), usages_(std::exchange(other.usages_, {})),
          dependencies_(std::exchange(other.dependencies_, {})) {}

    /** @copydoc RenderGraph::operator= */
    RenderGraph &RenderGraph::operator=(RenderGraph &&other) noexcept {
        if (this != &other) {
            owner_ = std::exchange(other.owner_, {});
            limits_ = std::exchange(other.limits_, {});
            passes_ = std::exchange(other.passes_, {});
            resources_ = std::exchange(other.resources_, {});
            usages_ = std::exchange(other.usages_, {});
            dependencies_ = std::exchange(other.dependencies_, {});
        }
        return *this;
    }

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
        return passes_;
    }

    /** @copydoc RenderGraph::Resources */
    std::span<const RenderGraphResource> RenderGraph::Resources() const noexcept {
        return resources_;
    }

    /** @copydoc RenderGraph::Usages */
    std::span<const RenderGraphResourceUsage> RenderGraph::Usages() const noexcept {
        return usages_;
    }

    /** @copydoc RenderGraph::Dependencies */
    std::span<const RenderGraphDependency> RenderGraph::Dependencies() const noexcept {
        return dependencies_;
    }
}  // namespace Horo::Render
