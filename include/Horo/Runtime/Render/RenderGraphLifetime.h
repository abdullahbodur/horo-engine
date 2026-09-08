#pragma once

/**
 * @file RenderGraphLifetime.h
 * @brief Backend-neutral resource lifetime and transient allocation planning.
 */

#include "Horo/Foundation/Result.h"
#include "Horo/Runtime/Render/RenderGraph.h"
#include "Horo/Runtime/Render/RenderResourceDescriptors.h"

#include <compare>
#include <cstddef>
#include <cstdint>
#include <span>
#include <variant>
#include <vector>

namespace Horo::Render {
    /** @brief Immutable storage requirement declared for one transient graph resource. */
    using RenderGraphTransientDescriptor = std::variant<RenderBufferDescriptor, RenderTextureDescriptor>;

    /** @brief Graph-local transient resource paired with its backend-neutral storage requirement. */
    struct RenderGraphTransientRequirement {
        RenderGraphResourceId resource;
        RenderGraphTransientDescriptor descriptor;
    };

    /** @brief Finite output capacities admitted before lifetime compilation. */
    struct RenderGraphLifetimeLimits {
        static constexpr std::size_t HardMaxAliasOpportunities = RenderGraphLimits::HardMaxResources - 1;

        std::size_t maxAliasOpportunities{HardMaxAliasOpportunities};

        /** @brief Reports whether output capacities are within their hard bounds. @return True for admitted limits. */
        [[nodiscard]] constexpr bool IsValid() const noexcept {
            return maxAliasOpportunities <= HardMaxAliasOpportunities;
        }
    };

    /** @brief Whether a resource participates in at least one retained scheduled pass. */
    enum class RenderGraphLifetimeDisposition : std::uint8_t {
        Used,
        Unused,
    };

    /** @brief First and last retained scheduled use of one graph resource. */
    struct RenderGraphResourceLifetime {
        RenderGraphResourceId resource;
        RenderGraphPassRef firstPass;
        RenderGraphPassRef lastPass;
        std::size_t firstUseIndex{0};
        std::size_t lastUseIndex{0};
        RenderGraphLifetimeDisposition disposition{RenderGraphLifetimeDisposition::Unused};
    };

    /** @brief Deterministic identity shared by transient resources with exactly compatible storage. */
    struct RenderGraphCompatibilityClassId {
        std::uint32_t value{0};

        [[nodiscard]] constexpr bool IsValid() const noexcept {
            return value != 0;
        }

        [[nodiscard]] constexpr auto operator<=>(const RenderGraphCompatibilityClassId &) const noexcept = default;
    };

    /** @brief Deterministic logical allocation slot assigned without creating native memory. */
    struct RenderGraphTransientAllocationSlot {
        std::uint32_t value{0};

        [[nodiscard]] constexpr bool IsValid() const noexcept {
            return value != 0;
        }

        [[nodiscard]] constexpr auto operator<=>(const RenderGraphTransientAllocationSlot &) const noexcept = default;
    };

    /** @brief Allocation requirement for one used transient resource. */
    struct RenderGraphTransientAllocationRequirement {
        RenderGraphResourceId resource;
        RenderGraphCompatibilityClassId compatibilityClass;
        RenderGraphTransientAllocationSlot slot;
        RenderGraphTransientDescriptor descriptor;
    };

    /** @brief One deterministic reuse edge between consecutive occupants of a logical slot. */
    struct RenderGraphAliasOpportunity {
        RenderGraphResourceId first;
        RenderGraphResourceId second;
        RenderGraphCompatibilityClassId compatibilityClass;
    };

    /**
     * @brief Owning immutable result of lifetime and transient allocation planning.
     *
     * The plan records logical allocation slots and alias opportunities only. It owns no native
     * memory and grants no backend permission to alias resources before later admission.
     */
    class RenderGraphLifetimePlan final {
    public:
        RenderGraphLifetimePlan(const RenderGraphLifetimePlan &) = delete;
        RenderGraphLifetimePlan &operator=(const RenderGraphLifetimePlan &) = delete;
        RenderGraphLifetimePlan(RenderGraphLifetimePlan &&other) noexcept;
        RenderGraphLifetimePlan &operator=(RenderGraphLifetimePlan &&other) noexcept;
        ~RenderGraphLifetimePlan() = default;

        /** @brief Returns the source graph owner identity. @return Non-zero owner for an intact plan. */
        [[nodiscard]] RenderGraphOwnerId Owner() const noexcept;

        /** @brief Returns one deterministic record per graph resource. @return Immutable authoring-order view. */
        [[nodiscard]] std::span<const RenderGraphResourceLifetime> Lifetimes() const noexcept;

        /** @brief Returns requirements for used transient resources. @return Immutable resource-order view. */
        [[nodiscard]] std::span<const RenderGraphTransientAllocationRequirement> AllocationRequirements() const noexcept;

        /** @brief Returns slot-reuse edges in deterministic allocation order. @return Immutable bounded view. */
        [[nodiscard]] std::span<const RenderGraphAliasOpportunity> AliasOpportunities() const noexcept;

    private:
        friend Result<RenderGraphLifetimePlan> CompileRenderGraphLifetimePlan(const RenderGraph &, const RenderGraphSchedule &,
                                                                              std::span<const RenderGraphTransientRequirement>,
                                                                              const RenderGraphLifetimeLimits &);

        RenderGraphLifetimePlan(RenderGraphOwnerId owner, std::vector<RenderGraphResourceLifetime> lifetimes,
                                std::vector<RenderGraphTransientAllocationRequirement> allocations,
                                std::vector<RenderGraphAliasOpportunity> aliases) noexcept;

        RenderGraphOwnerId owner_;
        std::vector<RenderGraphResourceLifetime> lifetimes_;
        std::vector<RenderGraphTransientAllocationRequirement> allocations_;
        std::vector<RenderGraphAliasOpportunity> aliases_;
    };

    /**
     * @brief Compiles retained resource lifetimes and conservative transient alias opportunities.
     *
     * Compilation is synchronous, backend-neutral, and bounded by graph capacities. Requirements
     * must describe every transient resource exactly once and no imported resource. Culled pass uses
     * do not extend lifetimes. Compatibility is exact and conservative. Slot reuse is proposed only
     * between non-overlapping resources used exclusively by the same queue role; cross-role ordering
     * requires later effective-topology evidence. The result never chooses a native heap, alignment,
     * or allocation strategy.
     *
     * @param graph Intact finalized graph.
     * @param schedule Intact compiled schedule owned by the same graph identity.
     * @param transientRequirements Exact transient storage declarations keyed by graph resource.
     * @param limits Finite output capacities fixed before compilation begins.
     * @return Owning plan or a typed graph, schedule, requirement, descriptor, or allocation failure.
     */
    [[nodiscard]] Result<RenderGraphLifetimePlan> CompileRenderGraphLifetimePlan(
        const RenderGraph &graph, const RenderGraphSchedule &schedule,
        std::span<const RenderGraphTransientRequirement> transientRequirements, const RenderGraphLifetimeLimits &limits = {});
}  // namespace Horo::Render
