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

        /** @brief Reports whether the pass kind belongs to the public contract. */
        [[nodiscard]] bool IsKnown(const RenderPassKind kind) noexcept {
            return static_cast<std::uint8_t>(kind) <= static_cast<std::uint8_t>(RenderPassKind::Copy);
        }

        /** @brief Reports whether the queue role belongs to the public contract. */
        [[nodiscard]] bool IsKnown(const RenderQueueRole queue) noexcept {
            return static_cast<std::uint8_t>(queue) <= static_cast<std::uint8_t>(RenderQueueRole::Transfer);
        }

        /** @brief Reports whether a declared queue can carry the pass kind. */
        [[nodiscard]] bool IsQueueCompatible(const RenderPassKind kind, const RenderQueueRole queue) noexcept {
            constexpr bool compatible[3][3] = {
                {true, false, false},
                {true, true, false},
                {true, true, true},
            };
            return compatible[static_cast<std::uint8_t>(kind)][static_cast<std::uint8_t>(queue)];
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

    /** @copydoc RenderGraphBuilder::AddPass */
    Result<RenderGraphPassRef> RenderGraphBuilder::AddPass(const RenderPassKind kind, const RenderQueueRole queue) {
        const Result<void> open = ValidateOpenOnOwnerThread();
        if (open.HasError()) {
            return Result<RenderGraphPassRef>::Failure(open.ErrorValue());
        }
        if (!IsKnown(kind)) {
            return Result<RenderGraphPassRef>::Failure(MakeError(RenderGraphErrors::UnsupportedPassKind));
        }
        if (!IsKnown(queue)) {
            return Result<RenderGraphPassRef>::Failure(MakeError(RenderGraphErrors::UnsupportedQueueRole));
        }
        if (!IsQueueCompatible(kind, queue)) {
            return Result<RenderGraphPassRef>::Failure(MakeError(RenderGraphErrors::IncompatibleQueue));
        }
        if (passes_.size() == limits_.maxPasses) {
            return Result<RenderGraphPassRef>::Failure(MakeError(RenderGraphErrors::CapacityExceeded, "Render-pass capacity is full."));
        }

        const RenderGraphPassRef reference{owner_, RenderPassId{static_cast<std::uint32_t>(passes_.size() + 1)}};
        passes_.push_back(RenderGraphPass{reference, kind, queue});
        return Result<RenderGraphPassRef>::Success(reference);
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

    /** @copydoc RenderGraphBuilder::ValidateOpenOnOwnerThread */
    Result<void> RenderGraphBuilder::ValidateOpenOnOwnerThread() const {
        if (std::this_thread::get_id() != ownerThread_) {
            return Result<void>::Failure(MakeError(RenderGraphErrors::WrongThread));
        }
        if (state_ != RenderGraphBuilderState::Open) {
            return Result<void>::Failure(MakeError(RenderGraphErrors::BuilderClosed));
        }
        return Result<void>::Success();
    }

    /** @copydoc RenderGraphBuilder::ReleaseStorage */
    void RenderGraphBuilder::ReleaseStorage() noexcept {
        std::vector<RenderGraphPass>{}.swap(passes_);
        std::vector<RenderGraphResource>{}.swap(resources_);
        std::vector<RenderGraphResourceUsage>{}.swap(usages_);
        std::vector<RenderGraphDependency>{}.swap(dependencies_);
    }
}  // namespace Horo::Render
