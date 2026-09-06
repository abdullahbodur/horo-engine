#include "Horo/Runtime/Render/RenderGraph.h"

#include "Horo/Runtime/Render/RenderGraphErrors.h"

#include <atomic>
#include <limits>
#include <new>
#include <utility>

namespace Horo::Render {
    namespace {
        /** @brief Next non-reusable process-local render-graph owner identity. */
        std::atomic<std::uint64_t> NextGraphOwner{1};

        /** @brief Atomically acquires a non-zero owner identity without wrapping. */
        [[nodiscard]] Result<RenderGraphOwnerId> AcquireOwner() {
            std::uint64_t candidate = NextGraphOwner.load(std::memory_order_relaxed);
            while (candidate != std::numeric_limits<std::uint64_t>::max()) {
                if (NextGraphOwner.compare_exchange_weak(candidate, candidate + 1, std::memory_order_relaxed)) {
                    return Result<RenderGraphOwnerId>::Success(RenderGraphOwnerId{candidate});
                }
            }
            return Result<RenderGraphOwnerId>::Failure(MakeError(RenderGraphErrors::OwnerExhausted));
        }
    }  // namespace

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

    /** @copydoc RenderGraphBuilder::RenderGraphBuilder */
    RenderGraphBuilder::RenderGraphBuilder(const RenderGraphOwnerId owner, const RenderGraphLimits limits) noexcept
        : owner_(owner), limits_(limits), ownerThread_(std::this_thread::get_id()), state_(RenderGraphBuilderState::Open) {}

    /** @copydoc RenderGraphBuilder::RenderGraphBuilder */
    RenderGraphBuilder::RenderGraphBuilder(RenderGraphBuilder &&other) noexcept
        : owner_(other.owner_), limits_(other.limits_), ownerThread_(other.ownerThread_), state_(other.state_),
          passes_(std::move(other.passes_)), resources_(std::move(other.resources_)), usages_(std::move(other.usages_)),
          dependencies_(std::move(other.dependencies_)) {
        other.owner_ = {};
        other.state_ = RenderGraphBuilderState::MovedFrom;
    }

    /** @copydoc RenderGraphBuilder::~RenderGraphBuilder */
    RenderGraphBuilder::~RenderGraphBuilder() {
        Shutdown();
    }

    /** @copydoc RenderGraphBuilder::Create */
    Result<RenderGraphBuilder> RenderGraphBuilder::Create(const RenderGraphLimits limits) {
        if (!limits.IsValid()) {
            return Result<RenderGraphBuilder>::Failure(MakeError(RenderGraphErrors::InvalidLimits));
        }
        auto owner = AcquireOwner();
        if (owner.HasError()) {
            return Result<RenderGraphBuilder>::Failure(owner.ErrorValue());
        }

        RenderGraphBuilder builder{owner.Value(), limits};
        const Result<void> reserved = builder.Reserve();
        if (reserved.HasError()) {
            return Result<RenderGraphBuilder>::Failure(reserved.ErrorValue());
        }
        return Result<RenderGraphBuilder>::Success(std::move(builder));
    }

    /** @copydoc RenderGraphBuilder::Owner */
    RenderGraphOwnerId RenderGraphBuilder::Owner() const noexcept {
        return owner_;
    }

    /** @copydoc RenderGraphBuilder::State */
    RenderGraphBuilderState RenderGraphBuilder::State() const noexcept {
        return state_;
    }

    /** @copydoc RenderGraphBuilder::Shutdown */
    void RenderGraphBuilder::Shutdown() noexcept {
        ReleaseStorage();
        if (state_ != RenderGraphBuilderState::MovedFrom) {
            state_ = RenderGraphBuilderState::Shutdown;
        }
    }

    /** @copydoc RenderGraphBuilder::Reserve */
    Result<void> RenderGraphBuilder::Reserve() {
        try {
            passes_.reserve(limits_.maxPasses);
            resources_.reserve(limits_.maxResources);
            usages_.reserve(limits_.maxUsages);
            dependencies_.reserve(limits_.maxDependencies);
            return Result<void>::Success();
        } catch (const std::bad_alloc &) {
            Shutdown();
            return Result<void>::Failure(MakeError(RenderGraphErrors::AllocationFailed));
        }
    }

    /** @copydoc RenderGraphBuilder::ReleaseStorage */
    void RenderGraphBuilder::ReleaseStorage() noexcept {
        std::vector<RenderGraphPass>{}.swap(passes_);
        std::vector<RenderGraphResource>{}.swap(resources_);
        std::vector<RenderGraphResourceUsage>{}.swap(usages_);
        std::vector<RenderGraphDependency>{}.swap(dependencies_);
    }
}  // namespace Horo::Render
