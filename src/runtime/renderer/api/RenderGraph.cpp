#include "Horo/Runtime/Render/RenderGraph.h"

#include "Horo/Runtime/Render/RenderGraphErrors.h"

#include <array>
#include <atomic>
#include <limits>
#include <new>
#include <utility>

namespace Horo::Render {
    namespace {
        /** @brief Atomically acquires a non-zero owner identity without wrapping. */
        [[nodiscard]] Result<RenderGraphOwnerId> AcquireOwner() {
            static std::atomic<std::uint64_t> nextGraphOwner{1};
            std::uint64_t candidate = nextGraphOwner.load();
            while (candidate != std::numeric_limits<std::uint64_t>::max()) {
                if (nextGraphOwner.compare_exchange_weak(candidate, candidate + 1)) {
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
            return IsRenderQueueRoleValid(queue);
        }

        /** @brief Reports whether the resource kind belongs to the public contract. */
        [[nodiscard]] bool IsKnown(const RenderGraphResourceKind kind) noexcept {
            return static_cast<std::uint8_t>(kind) <= static_cast<std::uint8_t>(RenderGraphResourceKind::Texture);
        }

        /** @brief Reports whether the access value belongs to the public contract. */
        [[nodiscard]] bool IsKnown(const RenderGraphAccess access) noexcept {
            return static_cast<std::uint8_t>(access) <= static_cast<std::uint8_t>(RenderGraphAccess::ReadWrite);
        }

        /** @brief Reports whether the usage kind belongs to the public contract. */
        [[nodiscard]] bool IsKnown(const RenderGraphUsageKind kind) noexcept {
            return static_cast<std::uint8_t>(kind) <= static_cast<std::uint8_t>(RenderGraphUsageKind::CopyDestination);
        }

        /** @brief Reports whether the dependency kind belongs to the public contract. */
        [[nodiscard]] bool IsKnown(const RenderGraphDependencyKind kind) noexcept {
            return static_cast<std::uint8_t>(kind) <= static_cast<std::uint8_t>(RenderGraphDependencyKind::ExternalSynchronization);
        }

        /** @brief Reports whether a declared queue can carry the pass kind. */
        [[nodiscard]] bool IsQueueCompatible(const RenderPassKind kind, const RenderQueueRole queue) noexcept {
            constexpr std::array compatible{
                std::array{true, false, false},
                std::array{true, true, false},
                std::array{true, true, true},
            };
            return compatible[static_cast<std::uint8_t>(kind)][static_cast<std::uint8_t>(queue)];
        }

        /** @brief Reports whether access direction matches the semantic use. */
        [[nodiscard]] bool IsAccessCompatible(const RenderGraphAccess access, const RenderGraphUsageKind kind) noexcept {
            using enum RenderGraphAccess;
            using enum RenderGraphUsageKind;
            switch (kind) {
                case Sampled:
                case CopySource:
                    return access == Read;
                case ColorAttachment:
                    return access == Write || access == ReadWrite;
                case DepthStencilAttachment:
                case Storage:
                    return true;
                case CopyDestination:
                    return access == Write;
                default:
                    return false;
            }
        }

        /** @brief Reports whether a pass category can declare the semantic use. */
        [[nodiscard]] bool IsPassCompatible(const RenderPassKind pass, const RenderGraphUsageKind usage) noexcept {
            using enum RenderGraphUsageKind;
            using enum RenderPassKind;
            switch (pass) {
                case Graphics:
                    return usage != CopySource && usage != CopyDestination;
                case Compute:
                    return usage == Sampled || usage == Storage;
                case Copy:
                    return usage == CopySource || usage == CopyDestination;
                default:
                    return false;
            }
        }

        /** @brief Reports whether a resource category can satisfy the semantic use. */
        [[nodiscard]] bool IsResourceCompatible(const RenderGraphResourceKind resource, const RenderGraphUsageKind usage) noexcept {
            if (usage == RenderGraphUsageKind::ColorAttachment || usage == RenderGraphUsageKind::DepthStencilAttachment) {
                return resource == RenderGraphResourceKind::Texture;
            }
            return true;
        }
    }  // namespace

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

    /** @copydoc RenderGraphBuilder::RenderGraphBuilder */
    RenderGraphBuilder::RenderGraphBuilder(const RenderGraphOwnerId owner, const RenderGraphLimits &limits) noexcept
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
    Result<RenderGraphBuilder> RenderGraphBuilder::Create(const RenderGraphLimits &limits) {
        if (!limits.IsValid()) {
            return Result<RenderGraphBuilder>::Failure(MakeError(RenderGraphErrors::InvalidLimits));
        }
        auto owner = AcquireOwner();
        if (owner.HasError()) {
            return Result<RenderGraphBuilder>::Failure(owner.ErrorValue());
        }

        RenderGraphBuilder builder{owner.Value(), limits};
        if (const Result<void> reserved = builder.Reserve(); reserved.HasError()) {
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
        if (const Result<void> open = ValidateOpenOnOwnerThread(); open.HasError()) {
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
        passes_.emplace_back(reference, kind, queue);
        return Result<RenderGraphPassRef>::Success(reference);
    }

    /** @copydoc RenderGraphBuilder::AddResource */
    Result<RenderGraphResourceId> RenderGraphBuilder::AddResource(const RenderGraphResourceKind kind) {
        if (const Result<void> open = ValidateOpenOnOwnerThread(); open.HasError()) {
            return Result<RenderGraphResourceId>::Failure(open.ErrorValue());
        }
        if (!IsKnown(kind)) {
            return Result<RenderGraphResourceId>::Failure(MakeError(RenderGraphErrors::UnsupportedResourceKind));
        }
        if (resources_.size() == limits_.maxResources) {
            return Result<RenderGraphResourceId>::Failure(
                MakeError(RenderGraphErrors::CapacityExceeded, "Graph-resource capacity is full."));
        }

        const RenderGraphResourceId id{owner_, static_cast<std::uint32_t>(resources_.size() + 1)};
        resources_.emplace_back(id, kind);
        return Result<RenderGraphResourceId>::Success(id);
    }

    /** @copydoc RenderGraphBuilder::AddUsage */
    Result<void> RenderGraphBuilder::AddUsage(const RenderGraphResourceUsage &usage) {
        if (const Result<void> open = ValidateOpenOnOwnerThread(); open.HasError()) {
            return open;
        }
        if (usages_.size() == limits_.maxUsages) {
            return Result<void>::Failure(MakeError(RenderGraphErrors::CapacityExceeded, "Graph-resource usage capacity is full."));
        }
        if (const Result<void> references = ValidateUsageReferences(usage); references.HasError()) {
            return references;
        }
        if (const Result<void> semantics = ValidateUsageSemantics(usage); semantics.HasError()) {
            return semantics;
        }

        usages_.emplace_back(usage);
        return Result<void>::Success();
    }

    /** @copydoc RenderGraphBuilder::AddDependency */
    Result<void> RenderGraphBuilder::AddDependency(const RenderGraphDependency &dependency) {
        if (const Result<void> open = ValidateOpenOnOwnerThread(); open.HasError()) {
            return open;
        }
        if (dependencies_.size() == limits_.maxDependencies) {
            return Result<void>::Failure(MakeError(RenderGraphErrors::CapacityExceeded, "Render dependency capacity is full."));
        }
        if (const Result<void> references = ValidateDependencyReferences(dependency); references.HasError()) {
            return references;
        }
        if (!IsKnown(dependency.kind)) {
            return Result<void>::Failure(MakeError(RenderGraphErrors::UnsupportedDependencyKind));
        }

        dependencies_.emplace_back(dependency);
        return Result<void>::Success();
    }

    /** @copydoc RenderGraphBuilder::Finalize */
    Result<RenderGraph> RenderGraphBuilder::Finalize() {
        if (const Result<void> open = ValidateOpenOnOwnerThread(); open.HasError()) {
            return Result<RenderGraph>::Failure(open.ErrorValue());
        }
        if (passes_.empty()) {
            return Result<RenderGraph>::Failure(MakeError(RenderGraphErrors::EmptyGraph));
        }

        state_ = RenderGraphBuilderState::Finalized;
        return Result<RenderGraph>::Success(
            RenderGraph{owner_, limits_, std::move(passes_), std::move(resources_), std::move(usages_), std::move(dependencies_)});
    }

    /** @copydoc RenderGraphBuilder::Cancel */
    Result<void> RenderGraphBuilder::Cancel() {
        if (std::this_thread::get_id() != ownerThread_) {
            return Result<void>::Failure(MakeError(RenderGraphErrors::WrongThread));
        }
        if (state_ == RenderGraphBuilderState::Cancelled) {
            return Result<void>::Success();
        }
        if (state_ != RenderGraphBuilderState::Open) {
            return Result<void>::Failure(MakeError(RenderGraphErrors::BuilderClosed));
        }

        ReleaseStorage();
        state_ = RenderGraphBuilderState::Cancelled;
        return Result<void>::Success();
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

    /** @copydoc RenderGraphBuilder::ValidateUsageReferences */
    Result<void> RenderGraphBuilder::ValidateUsageReferences(const RenderGraphResourceUsage &usage) const {
        if (!usage.pass.IsValid()) {
            return Result<void>::Failure(MakeError(RenderGraphErrors::InvalidPass));
        }
        if (!usage.resource.IsValid()) {
            return Result<void>::Failure(MakeError(RenderGraphErrors::InvalidResource));
        }
        if (usage.pass.owner != owner_ || usage.resource.owner != owner_) {
            return Result<void>::Failure(MakeError(RenderGraphErrors::WrongOwner));
        }
        if (FindPass(usage.pass) == nullptr) {
            return Result<void>::Failure(MakeError(RenderGraphErrors::InvalidPass));
        }
        if (FindResource(usage.resource) == nullptr) {
            return Result<void>::Failure(MakeError(RenderGraphErrors::InvalidResource));
        }
        return Result<void>::Success();
    }

    /** @copydoc RenderGraphBuilder::ValidateUsageSemantics */
    Result<void> RenderGraphBuilder::ValidateUsageSemantics(const RenderGraphResourceUsage &usage) const {
        if (!IsKnown(usage.access) || !IsKnown(usage.kind)) {
            return Result<void>::Failure(MakeError(RenderGraphErrors::UnsupportedUsage));
        }

        const RenderGraphPass &pass = *FindPass(usage.pass);
        if (const RenderGraphResource &resource = *FindResource(usage.resource); !IsAccessCompatible(usage.access, usage.kind) ||
                                                                                 !IsPassCompatible(pass.kind, usage.kind) ||
                                                                                 !IsResourceCompatible(resource.kind, usage.kind)) {
            return Result<void>::Failure(MakeError(RenderGraphErrors::InvalidUsage));
        }
        return Result<void>::Success();
    }

    /** @copydoc RenderGraphBuilder::ValidateDependencyReferences */
    Result<void> RenderGraphBuilder::ValidateDependencyReferences(const RenderGraphDependency &dependency) const {
        if (!dependency.before.IsValid() || !dependency.after.IsValid() || dependency.before == dependency.after) {
            return Result<void>::Failure(MakeError(RenderGraphErrors::InvalidDependency));
        }
        if (dependency.before.owner != owner_ || dependency.after.owner != owner_) {
            return Result<void>::Failure(MakeError(RenderGraphErrors::WrongOwner));
        }
        if (FindPass(dependency.before) == nullptr || FindPass(dependency.after) == nullptr) {
            return Result<void>::Failure(MakeError(RenderGraphErrors::InvalidDependency));
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

    /** @copydoc RenderGraphBuilder::FindPass */
    const RenderGraphPass *RenderGraphBuilder::FindPass(const RenderGraphPassRef reference) const noexcept {
        if (reference.owner != owner_ || reference.id.value == 0 || reference.id.value > passes_.size()) {
            return nullptr;
        }
        return &passes_[reference.id.value - 1];
    }

    /** @copydoc RenderGraphBuilder::FindResource */
    const RenderGraphResource *RenderGraphBuilder::FindResource(const RenderGraphResourceId id) const noexcept {
        if (id.owner != owner_ || id.value == 0 || id.value > resources_.size()) {
            return nullptr;
        }
        return &resources_[id.value - 1];
    }
}  // namespace Horo::Render
