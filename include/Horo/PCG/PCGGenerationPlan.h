#pragma once

/**
 * @file PCGGenerationPlan.h
 * @brief Immutable bounded PCG output plans, provenance, and owner-authorized deltas.
 */

#include "Horo/Foundation/Result.h"
#include "Horo/Foundation/Sha256.h"
#include "Horo/PCG/PCGIdentity.h"
#include "Horo/PCG/PCGRegistry.h"

#include <compare>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace Horo::PCG {
    /** @brief Stable identity of one immutable generation plan. */
    using GenerationPlanId = PcgStableIdentity<struct GenerationPlanIdentityTag>;
    /** @brief Stable replacement lineage shared by revisions of one generated set. */
    using GenerationLineageId = PcgStableIdentity<struct GenerationLineageIdentityTag>;
    /** @brief Stable logical generated-set identity within one lineage and scope. */
    using GeneratedSetId = PcgStableIdentity<struct GeneratedSetIdentityTag>;
    /** @brief Stable target-owner identity supplied by host composition. */
    using GenerationTargetOwnerId = PcgStableIdentity<struct GenerationTargetOwnerIdentityTag>;
    /** @brief Stable world/cell scope identity; it is never inferred from coordinates. */
    using GenerationCellId = PcgStableIdentity<struct GenerationCellIdentityTag>;
    /** @brief Stable semantic dependency identity captured by evaluation. */
    using GenerationDependencyId = PcgStableIdentity<struct GenerationDependencyIdentityTag>;
    /** @brief Target-issued validation receipt identity. */
    using GenerationValidationReceiptId = PcgStableIdentity<struct GenerationValidationReceiptIdentityTag>;
    /** @brief Stable logical output identity preserved across evaluation executions. */
    using GenerationLogicalOutputId = PcgStableIdentity<struct GenerationLogicalOutputIdentityTag>;

    /** @brief Fixed content fingerprint used only as typed provenance evidence. */
    using PCGGenerationDigest = Sha256Digest;

    /** @brief Generation-plan contract schema version. */
    struct PCGGenerationPlanVersion final {
        std::uint16_t major{1};
        std::uint16_t minor{};
        [[nodiscard]] constexpr auto operator<=>(const PCGGenerationPlanVersion &) const noexcept = default;
    };

    /** @brief Exact schema understood by this generation-plan boundary. */
    inline constexpr PCGGenerationPlanVersion CurrentPCGGenerationPlanVersion{1, 0};

    /** @brief Closed output delta vocabulary; retain is represented by absence from the delta. */
    enum class PCGOutputDeltaKind : std::uint8_t {
        Create,
        Update,
        Remove,
        Count
    };

    /** @brief Lifecycle gate captured before any plan allocation or validation. */
    enum class PCGGenerationPlanAdmission : std::uint8_t {
        Accepting,
        CancellationRequested,
        ShuttingDown,
        Count
    };

    /** @brief Complete resource charge for one output operation. */
    struct PCGGenerationResourceEstimate final {
        std::uint64_t workUnits{};        /**< Versioned semantic work units. */
        std::uint64_t residentBytes{};    /**< Bytes retained by committed target state. */
        std::uint64_t preparationBytes{}; /**< Detached preparation bytes. */
        std::uint64_t retirementBytes{};  /**< Bytes retained while prior state drains. */
        [[nodiscard]] constexpr auto operator<=>(const PCGGenerationResourceEstimate &) const noexcept = default;
    };

    /** @brief Exact immutable semantic dependency evidence. */
    struct PCGGenerationDependency final {
        GenerationDependencyId id{};  /**< Stable dependency identity. */
        std::uint64_t revision{};     /**< Non-zero exact dependency revision. */
        PCGGenerationDigest digest{}; /**< Non-zero content fingerprint. */
        [[nodiscard]] constexpr auto operator<=>(const PCGGenerationDependency &) const noexcept = default;
    };

    /** @brief Exact target state and capability evidence validated before planning. */
    struct PCGGenerationTargetReceipt final {
        GenerationValidationReceiptId receipt{}; /**< Target-issued receipt identity. */
        GenerationTargetOwnerId owner{};         /**< Exact target owner. */
        std::uint64_t ownerGeneration{};         /**< Current owner state generation. */
        std::uint64_t capabilityGeneration{};    /**< Current capability publication generation. */
        PCGCapabilitySet capabilities{};         /**< Explicit supported capabilities. */
        [[nodiscard]] constexpr auto operator<=>(const PCGGenerationTargetReceipt &) const noexcept = default;
    };

    /** @brief Exact provenance that a target owner stores beside one generated output. */
    struct PCGGeneratedOutputProvenance final {
        GenerationLogicalOutputId logicalOutput{}; /**< Stable output identity across replacement executions. */
        GeneratedOutputId sourceOutput{};          /**< Exact evaluation-local derivation evidence. */
        GenerationLineageId lineage{};             /**< Replacement lineage. */
        GeneratedSetId set{};                      /**< Logical generated set. */
        std::uint64_t setRevision{};               /**< Non-zero set revision. */
        std::uint64_t ownershipGeneration{};       /**< Non-zero target-issued ownership generation. */
        GenerationTargetOwnerId targetOwner{};     /**< Canonical committed-state owner. */
        GenerationCellId cell{};                   /**< Exact world/cell scope. */
        [[nodiscard]] constexpr auto operator<=>(const PCGGeneratedOutputProvenance &) const noexcept = default;
    };

    /** @brief Target-owned immutable record used to authorize update or removal. */
    struct PCGOwnedGeneratedOutput final {
        PCGGeneratedOutputProvenance provenance{}; /**< Exact currently committed provenance. */
        PCGGenerationDigest content{};             /**< Exact current canonical content. */
        [[nodiscard]] constexpr auto operator<=>(const PCGOwnedGeneratedOutput &) const noexcept = default;
    };

    /** @brief One detached create, update, or owner-authorized removal proposal. */
    struct PCGOutputDelta final {
        PCGOutputDeltaKind kind{PCGOutputDeltaKind::Create}; /**< Requested mutation class. */
        PCGGeneratedOutputProvenance provenance{};           /**< Desired provenance for the resulting set revision. */
        std::uint64_t expectedSetRevision{};                 /**< Exact prior set revision; zero only for create. */
        PCGGenerationDigest priorContent{};                  /**< Required for update/remove; zero for create. */
        PCGGenerationDigest desiredContent{};                /**< Required for create/update; zero for remove. */
        std::vector<GenerationDependencyId> dependencies{};  /**< Canonical dependency IDs used by this output. */
        PCGGenerationResourceEstimate resources{};           /**< Complete bounded target charge. */
        [[nodiscard]] bool operator==(const PCGOutputDelta &) const noexcept = default;
    };

    /** @brief Independent caller-lowerable safety limits for one plan. */
    struct PCGGenerationPlanLimits final {
        std::size_t maximumOutputs{16'384};                     /**< Maximum create/update/remove operations. */
        std::size_t maximumDependencies{64};                    /**< Maximum unique semantic dependencies. */
        std::size_t maximumDependenciesPerOutput{64};           /**< Maximum dependency references per output. */
        std::uint64_t maximumWorkUnits{262'144};                /**< Aggregate semantic work ceiling. */
        std::uint64_t maximumChargedBytes{32U * 1024U * 1024U}; /**< Aggregate lifecycle byte ceiling. */
    };

    /** @brief Complete detached plan candidate produced by pure evaluation. */
    struct PCGGenerationPlanCandidate final {
        PCGGenerationPlanVersion version{CurrentPCGGenerationPlanVersion}; /**< Exact immutable plan schema. */
        GenerationPlanId plan{};                                           /**< Unique immutable plan identity. */
        ExecutionId execution{};                                           /**< Exact graph revision and evaluation. */
        std::uint64_t seed{};                                              /**< Captured deterministic seed, including zero. */
        GenerationLineageId lineage{};                                     /**< Replacement lineage. */
        GeneratedSetId set{};                                              /**< Stable generated set. */
        std::uint64_t setRevision{};                                       /**< Non-zero monotonic set revision. */
        GenerationCellId cell{};                                           /**< Exact world/cell scope. */
        GenerationTargetOwnerId targetOwner{};                             /**< Sole intended target owner. */
        PCGCapabilitySet requiredCapabilities{};                           /**< Exact target capabilities required. */
        PCGGenerationTargetReceipt validation{};                           /**< Exact target validation evidence. */
        std::vector<PCGGenerationDependency> dependencies{};               /**< Canonicalizable closed dependency set. */
        std::vector<PCGOutputDelta> outputs{};                             /**< Canonicalizable bounded delta. */
    };

    /** @brief Explicit validation context; borrowed ownership records live only for the call. */
    struct PCGGenerationPlanContext final {
        PCGGenerationPlanAdmission admission{PCGGenerationPlanAdmission::Accepting}; /**< Captured lifecycle state. */
        PCGGenerationTargetReceipt currentTarget{};                                  /**< Current target/capability state. */
        std::span<const PCGOwnedGeneratedOutput> currentOutputs{};                   /**< Immutable target-owned provenance snapshot. */
        PCGGenerationPlanLimits limits{};                                            /**< Finite caller-selected ceilings. */
    };

    /** @brief Immutable owning plan safe for concurrent readers and retention after replacement. */
    class PCGGenerationPlan final {
    public:
        struct State;
        PCGGenerationPlan() = delete;

        /** @brief Opaque construction gate restricted to validated plan creation. */
        class ConstructionKey final {
            friend Result<PCGGenerationPlan> CreatePCGGenerationPlan(PCGGenerationPlanCandidate, const PCGGenerationPlanContext &);
            ConstructionKey() = default;
        };

        /** @brief Adopts fully validated immutable plan state. */
        explicit PCGGenerationPlan(ConstructionKey, std::shared_ptr<const State> state) noexcept : state_(std::move(state)) {}

        /** @brief Returns plan identity. @return Unique immutable plan identity. */
        [[nodiscard]] GenerationPlanId Id() const noexcept;
        /** @brief Returns plan schema. @return Exact validated contract version. */
        [[nodiscard]] PCGGenerationPlanVersion Version() const noexcept;
        /** @brief Returns exact evaluation identity. @return Revision-fenced execution. */
        [[nodiscard]] ExecutionId Execution() const noexcept;
        /** @brief Returns deterministic seed. @return Captured seed. */
        [[nodiscard]] std::uint64_t Seed() const noexcept;
        /** @brief Returns replacement lineage. @return Stable lineage identity. */
        [[nodiscard]] GenerationLineageId Lineage() const noexcept;
        /** @brief Returns generated set. @return Stable logical set identity. */
        [[nodiscard]] GeneratedSetId Set() const noexcept;
        /** @brief Returns set revision. @return Non-zero exact revision. */
        [[nodiscard]] std::uint64_t SetRevision() const noexcept;
        /** @brief Returns exact cell scope. @return Stable cell identity. */
        [[nodiscard]] GenerationCellId Cell() const noexcept;
        /** @brief Returns target receipt. @return Immutable exact target evidence. */
        [[nodiscard]] const PCGGenerationTargetReceipt &Target() const noexcept;
        /** @brief Returns required capabilities. @return Exact immutable requirement set. */
        [[nodiscard]] PCGCapabilitySet RequiredCapabilities() const noexcept;
        /** @brief Returns canonical dependencies. @return Identity-ordered immutable view. */
        [[nodiscard]] std::span<const PCGGenerationDependency> Dependencies() const noexcept;
        /** @brief Returns canonical output delta. @return Output-identity-ordered immutable view. */
        [[nodiscard]] std::span<const PCGOutputDelta> Outputs() const noexcept;
        /** @brief Returns aggregate resources. @return Checked complete plan charge. */
        [[nodiscard]] const PCGGenerationResourceEstimate &Resources() const noexcept;

    private:
        std::shared_ptr<const State> state_;
    };

    /**
     * @brief Validates, canonicalizes, and owns a detached immutable generation plan.
     * @param candidate Complete pure-evaluation result.
     * @param context Current target evidence, exact ownership snapshot, lifecycle gate, and finite limits.
     * @return Immutable plan or typed lifecycle, capability, capacity, provenance, or stale failure.
     * @post Failure authorizes and mutates no target state.
     */
    [[nodiscard]] Result<PCGGenerationPlan> CreatePCGGenerationPlan(PCGGenerationPlanCandidate candidate,
                                                                    const PCGGenerationPlanContext &context);

    /**
     * @brief Creates a detached replacement while retaining the current immutable plan.
     * @param current Existing plan whose lineage, set, cell, and target owner must be preserved.
     * @param candidate Strictly newer detached set revision and distinct plan/execution.
     * @param context Current target and ownership evidence.
     * @return New immutable plan or typed replacement/stale/provenance failure.
     * @post Existing readers remain valid regardless of outcome.
     */
    [[nodiscard]] Result<PCGGenerationPlan> ReplacePCGGenerationPlan(const PCGGenerationPlan &current, PCGGenerationPlanCandidate candidate,
                                                                     const PCGGenerationPlanContext &context);
}  // namespace Horo::PCG
