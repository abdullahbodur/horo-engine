#pragma once

/**
 * @file RenderGraph.h
 * @brief Backend-neutral bounded render-graph authoring contracts.
 */

#include "Horo/Foundation/Result.h"
#include "Horo/Runtime/Render/RenderBackend.h"

#include <compare>
#include <cstddef>
#include <cstdint>
#include <span>
#include <thread>
#include <vector>

namespace Horo::Render {
    /** @brief Process-local owner identity assigned to one render-graph builder. */
    struct RenderGraphOwnerId {
        std::uint64_t value{0};

        /**
         * @brief Reports whether the identity was issued to a builder.
         * @return True for a non-zero issued identity.
         */
        [[nodiscard]] constexpr bool IsValid() const noexcept {
            return value != 0;
        }

        [[nodiscard]] constexpr auto operator<=>(const RenderGraphOwnerId &) const noexcept = default;
    };

    /** @brief Builder-scoped reference to the canonical render-pass identity. */
    struct RenderGraphPassRef {
        RenderGraphOwnerId owner;
        RenderPassId id;

        /**
         * @brief Reports whether both owner and canonical pass identities are valid.
         * @return True when both identity components are non-zero.
         */
        [[nodiscard]] constexpr bool IsValid() const noexcept {
            return owner.IsValid() && id.IsValid();
        }

        [[nodiscard]] constexpr auto operator<=>(const RenderGraphPassRef &) const noexcept = default;
    };

    /** @brief Builder-scoped identity of one logical graph resource. */
    struct RenderGraphResourceId {
        RenderGraphOwnerId owner;
        std::uint32_t value{0};

        /**
         * @brief Reports whether the owner and local identity are valid.
         * @return True when both identity components are non-zero.
         */
        [[nodiscard]] constexpr bool IsValid() const noexcept {
            return owner.IsValid() && value != 0;
        }

        [[nodiscard]] constexpr auto operator<=>(const RenderGraphResourceId &) const noexcept = default;
    };

    /** @brief Logical queue role requested by an authored pass without native queue identity. */
    enum class RenderQueueRole : std::uint8_t {
        Graphics,
        Compute,
        Transfer,
    };

    /** @brief Coarse logical resource category used during initial graph authoring. */
    enum class RenderGraphResourceKind : std::uint8_t {
        Buffer,
        Texture,
    };

    /** @brief Access direction declared by one pass-resource use. */
    enum class RenderGraphAccess : std::uint8_t {
        Read,
        Write,
        ReadWrite,
    };

    /** @brief Backend-neutral semantic role of one pass-resource use. */
    enum class RenderGraphUsageKind : std::uint8_t {
        Sampled,
        Storage,
        ColorAttachment,
        DepthStencilAttachment,
        CopySource,
        CopyDestination,
    };

    /** @brief Explicit reason for an authored pass dependency. */
    enum class RenderGraphDependencyKind : std::uint8_t {
        ExecutionOrder,
        ResourceHazard,
        ExternalSynchronization,
    };

    /** @brief Finite builder capacities admitted before any graph records are authored. */
    struct RenderGraphLimits {
        static constexpr std::size_t HardMaxPasses = 4'096;
        static constexpr std::size_t HardMaxResources = 8'192;
        static constexpr std::size_t HardMaxUsages = 32'768;
        static constexpr std::size_t HardMaxDependencies = 32'768;

        std::size_t maxPasses{256};
        std::size_t maxResources{512};
        std::size_t maxUsages{2'048};
        std::size_t maxDependencies{2'048};

        /**
         * @brief Reports whether every capacity is non-zero and within its hard bound.
         * @return True when every capacity is admitted by the engine hard limits.
         */
        [[nodiscard]] bool IsValid() const noexcept;
    };

    /** @brief One pass authored with the canonical pass identity and an explicit queue role. */
    struct RenderGraphPass {
        RenderGraphPassRef reference;
        RenderPassKind kind{RenderPassKind::Graphics};
        RenderQueueRole queue{RenderQueueRole::Graphics};
    };

    /** @brief One graph-local logical resource owned by the finalized graph value. */
    struct RenderGraphResource {
        RenderGraphResourceId id;
        RenderGraphResourceKind kind{RenderGraphResourceKind::Buffer};
    };

    /** @brief One explicit semantic use of a graph resource by a pass. */
    struct RenderGraphResourceUsage {
        RenderGraphPassRef pass;
        RenderGraphResourceId resource;
        RenderGraphAccess access{RenderGraphAccess::Read};
        RenderGraphUsageKind kind{RenderGraphUsageKind::Sampled};
    };

    /** @brief One explicit directed dependency between two authored passes. */
    struct RenderGraphDependency {
        RenderGraphPassRef before;
        RenderGraphPassRef after;
        RenderGraphDependencyKind kind{RenderGraphDependencyKind::ExecutionOrder};
    };

    /** @brief Immutable owning graph value produced by a successful builder finalization. */
    class RenderGraph final {
    public:
        RenderGraph(const RenderGraph &) = delete;
        RenderGraph &operator=(const RenderGraph &) = delete;
        RenderGraph(RenderGraph &&) noexcept = default;
        RenderGraph &operator=(RenderGraph &&) noexcept = default;
        ~RenderGraph() = default;

        /**
         * @brief Returns the identity of the builder that owns all graph-local references.
         * @return Non-zero process-local owner identity.
         */
        [[nodiscard]] RenderGraphOwnerId Owner() const noexcept;

        /**
         * @brief Returns the finite limits admitted for this graph.
         * @return Immutable admitted limit set owned by this graph.
         */
        [[nodiscard]] const RenderGraphLimits &Limits() const noexcept;

        /**
         * @brief Returns the immutable passes in deterministic authoring order.
         * @return View valid for the lifetime of this graph value.
         */
        [[nodiscard]] std::span<const RenderGraphPass> Passes() const noexcept;

        /**
         * @brief Returns the immutable resources in deterministic authoring order.
         * @return View valid for the lifetime of this graph value.
         */
        [[nodiscard]] std::span<const RenderGraphResource> Resources() const noexcept;

        /**
         * @brief Returns the immutable resource uses in deterministic authoring order.
         * @return View valid for the lifetime of this graph value.
         */
        [[nodiscard]] std::span<const RenderGraphResourceUsage> Usages() const noexcept;

        /**
         * @brief Returns the immutable dependencies in deterministic authoring order.
         * @return View valid for the lifetime of this graph value.
         */
        [[nodiscard]] std::span<const RenderGraphDependency> Dependencies() const noexcept;

    private:
        friend class RenderGraphBuilder;

        RenderGraph(RenderGraphOwnerId owner, RenderGraphLimits limits, std::vector<RenderGraphPass> passes,
                    std::vector<RenderGraphResource> resources, std::vector<RenderGraphResourceUsage> usages,
                    std::vector<RenderGraphDependency> dependencies) noexcept;

        RenderGraphOwnerId owner_;
        RenderGraphLimits limits_;
        std::vector<RenderGraphPass> passes_;
        std::vector<RenderGraphResource> resources_;
        std::vector<RenderGraphResourceUsage> usages_;
        std::vector<RenderGraphDependency> dependencies_;
    };

    /** @brief Observable lifecycle state of a bounded render-graph builder. */
    enum class RenderGraphBuilderState : std::uint8_t {
        Open,
        Finalized,
        Cancelled,
        Shutdown,
        MovedFrom,
    };

    /**
     * @brief Owner-thread-only bounded authoring surface for one backend-neutral render graph.
     *
     * The builder owns every admitted record and allocates its declared capacities during
     * creation. Authoring and finalization must occur on the creating thread. Cancellation
     * and shutdown never submit backend work; shutdown is idempotent and may run during
     * partial initialization or destruction.
     */
    class RenderGraphBuilder final {
    public:
        RenderGraphBuilder(const RenderGraphBuilder &) = delete;
        RenderGraphBuilder &operator=(const RenderGraphBuilder &) = delete;
        RenderGraphBuilder(RenderGraphBuilder &&other) noexcept;
        RenderGraphBuilder &operator=(RenderGraphBuilder &&) = delete;
        ~RenderGraphBuilder();

        /**
         * @brief Creates an open builder and reserves every declared finite capacity.
         * @param limits Requested capacities, each constrained by a hard engine bound.
         * @return Open builder or a typed invalid-limit, identity, or allocation failure.
         */
        [[nodiscard]] static Result<RenderGraphBuilder> Create(RenderGraphLimits limits = {});

        /**
         * @brief Returns the process-local identity that scopes all issued references.
         * @return Builder owner identity, or an invalid identity after move.
         */
        [[nodiscard]] RenderGraphOwnerId Owner() const noexcept;

        /**
         * @brief Returns the current lifecycle state without mutating the builder.
         * @return Current explicit builder state.
         */
        [[nodiscard]] RenderGraphBuilderState State() const noexcept;

        /**
         * @brief Adds one pass and assigns the next canonical render-pass identity.
         * @param kind Backend-neutral pass category.
         * @param queue Explicit logical queue role; incompatible or unknown roles fail without fallback.
         * @return Builder-scoped reference or a typed affinity, lifecycle, capacity, or support failure.
         */
        [[nodiscard]] Result<RenderGraphPassRef> AddPass(RenderPassKind kind, RenderQueueRole queue);

        /**
         * @brief Adds one graph-local logical resource.
         * @param kind Backend-neutral resource category.
         * @return Owner-scoped identity or a typed affinity, lifecycle, capacity, or support failure.
         */
        [[nodiscard]] Result<RenderGraphResourceId> AddResource(RenderGraphResourceKind kind);

        /**
         * @brief Records one pass-resource use after validating ownership and semantic compatibility.
         * @param usage Complete use declaration referencing records from this builder.
         * @return Success or a typed affinity, ownership, reference, capacity, or support failure.
         */
        [[nodiscard]] Result<void> AddUsage(const RenderGraphResourceUsage &usage);

        /**
         * @brief Records one directed dependency between two distinct passes from this builder.
         * @param dependency Complete dependency declaration.
         * @return Success or a typed affinity, ownership, reference, capacity, or support failure.
         */
        [[nodiscard]] Result<void> AddDependency(const RenderGraphDependency &dependency);

        /**
         * @brief Finalizes the builder and transfers all owned records into an immutable graph.
         * @return Owning graph or a typed affinity, lifecycle, or empty-graph failure.
         */
        [[nodiscard]] Result<RenderGraph> Finalize();

        /**
         * @brief Cancels open authoring and releases all authored records on the owner thread.
         * @return Success, including repeated cancellation, or a typed affinity/lifecycle failure.
         */
        [[nodiscard]] Result<void> Cancel();

        /**
         * @brief Releases all retained authoring storage; repeated calls are safe.
         *
         * The caller must first quiesce owner-thread authoring. Shutdown may then run
         * on a teardown thread because it performs no backend or GPU operation.
         */
        void Shutdown() noexcept;

    private:
        RenderGraphBuilder(RenderGraphOwnerId owner, RenderGraphLimits limits) noexcept;

        [[nodiscard]] Result<void> Reserve();
        [[nodiscard]] Result<void> ValidateOpenOnOwnerThread() const;
        [[nodiscard]] Result<void> ValidateUsageReferences(const RenderGraphResourceUsage &usage) const;
        [[nodiscard]] Result<void> ValidateUsageSemantics(const RenderGraphResourceUsage &usage) const;
        [[nodiscard]] Result<void> ValidateDependencyReferences(const RenderGraphDependency &dependency) const;
        void ReleaseStorage() noexcept;
        [[nodiscard]] const RenderGraphPass *FindPass(RenderGraphPassRef reference) const noexcept;
        [[nodiscard]] const RenderGraphResource *FindResource(RenderGraphResourceId id) const noexcept;

        RenderGraphOwnerId owner_;
        RenderGraphLimits limits_;
        std::thread::id ownerThread_;
        RenderGraphBuilderState state_{RenderGraphBuilderState::MovedFrom};
        std::vector<RenderGraphPass> passes_;
        std::vector<RenderGraphResource> resources_;
        std::vector<RenderGraphResourceUsage> usages_;
        std::vector<RenderGraphDependency> dependencies_;
    };
}  // namespace Horo::Render
