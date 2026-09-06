#pragma once

/**
 * @file RenderGraph.h
 * @brief Backend-neutral bounded render-graph authoring contracts.
 */

#include "Horo/Runtime/Render/RenderBackend.h"

#include <compare>
#include <cstddef>
#include <cstdint>
#include <span>
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
        [[nodiscard]] constexpr bool IsValid() const noexcept {
            return maxPasses > 0 && maxPasses <= HardMaxPasses && maxResources > 0 && maxResources <= HardMaxResources && maxUsages > 0 &&
                   maxUsages <= HardMaxUsages && maxDependencies > 0 && maxDependencies <= HardMaxDependencies;
        }
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

    /** @brief Immutable owning graph value produced by successful authoring finalization. */
    class RenderGraph final {
    public:
        RenderGraph(const RenderGraph &) = delete;
        RenderGraph &operator=(const RenderGraph &) = delete;
        RenderGraph(RenderGraph &&other) noexcept;
        RenderGraph &operator=(RenderGraph &&other) noexcept;
        ~RenderGraph() = default;

        /**
         * @brief Returns the identity that owns every graph-local reference.
         * @return Non-zero process-local owner identity.
         */
        [[nodiscard]] RenderGraphOwnerId Owner() const noexcept;

        /**
         * @brief Returns the finite limits admitted for this graph.
         * @return Immutable admitted limit set owned by this graph.
         */
        [[nodiscard]] const RenderGraphLimits &Limits() const noexcept;

        /**
         * @brief Returns immutable passes in deterministic authoring order.
         * @return View valid for the lifetime of this graph value.
         */
        [[nodiscard]] std::span<const RenderGraphPass> Passes() const noexcept;

        /**
         * @brief Returns immutable resources in deterministic authoring order.
         * @return View valid for the lifetime of this graph value.
         */
        [[nodiscard]] std::span<const RenderGraphResource> Resources() const noexcept;

        /**
         * @brief Returns immutable resource uses in deterministic authoring order.
         * @return View valid for the lifetime of this graph value.
         */
        [[nodiscard]] std::span<const RenderGraphResourceUsage> Usages() const noexcept;

        /**
         * @brief Returns immutable dependencies in deterministic authoring order.
         * @return View valid for the lifetime of this graph value.
         */
        [[nodiscard]] std::span<const RenderGraphDependency> Dependencies() const noexcept;

    private:
        friend class RenderGraphBuilder;

        RenderGraph(RenderGraphOwnerId owner, const RenderGraphLimits &limits, std::vector<RenderGraphPass> passes,
                    std::vector<RenderGraphResource> resources, std::vector<RenderGraphResourceUsage> usages,
                    std::vector<RenderGraphDependency> dependencies) noexcept;

        RenderGraphOwnerId owner_;
        RenderGraphLimits limits_;
        std::vector<RenderGraphPass> passes_;
        std::vector<RenderGraphResource> resources_;
        std::vector<RenderGraphResourceUsage> usages_;
        std::vector<RenderGraphDependency> dependencies_;
    };

}  // namespace Horo::Render
