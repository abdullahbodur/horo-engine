#pragma once

/**
 * @file WorldStreamingDiagnosticSnapshot.h
 * @brief Immutable bounded diagnostic projection of World Streaming authority facts.
 */

#include "Horo/WorldStreaming/StreamingBudgetModel.h"
#include "Horo/WorldStreaming/StreamingCellState.h"
#include "Horo/WorldStreaming/StreamingDesiredState.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace Horo::WorldStreaming {
    namespace Detail {
        /** @brief Tag separating diagnostic revisions from authority and policy revisions. */
        struct WorldStreamingDiagnosticRevisionTag;
    }  // namespace Detail

    /** @brief Monotonic revision of one immutable diagnostic projection. */
    using WorldStreamingDiagnosticRevision =
        Foundation::Detail::NonZeroId64<Detail::WorldStreamingDiagnosticRevisionTag, WorldStreamingErrors::IdentityInvalid>;

    /** @brief Mandatory caller ceilings for owned diagnostic rows. */
    struct WorldStreamingDiagnosticLimits final {
        static constexpr std::uint32_t MaximumSources = 4096;
        static constexpr std::uint32_t MaximumCells = StreamingCellStateLedgerConfig::MaximumTrackedAttempts;
        static constexpr std::uint32_t MaximumFailures = StreamingCellStateLedgerConfig::MaximumTrackedAttempts;

        std::uint32_t sources{};  /**< Maximum source rows copied into one snapshot. */
        std::uint32_t cells{};    /**< Maximum current or retiring cell-attempt rows. */
        std::uint32_t failures{}; /**< Maximum terminal or pending-retirement failure rows. */

        /** @brief Validates positive implementation-bounded row ceilings. @return True when every ceiling is usable. */
        [[nodiscard]] bool IsValid() const noexcept;
    };

    /** @brief Immutable queue pressure facts read from the authority-owned scheduler ledger. */
    struct StreamingDiagnosticQueueSnapshot final {
        StreamingSchedulerLedgerId owner;         /**< Exact scheduler-ledger owner lifetime. */
        StreamingSchedulerAdmissionLimits limits; /**< Immutable admitted queue/capacity ceilings. */
        StreamingSchedulerAdmissionState state{}; /**< Accepting, draining or closed admission state. */
        std::uint32_t reservedOperations{};       /**< Operations still retained by the ledger. */
        std::uint64_t reservedCapacityUnits{};    /**< Generic capacity still charged. */
    };

    /** @brief Typed terminal or pending-retirement cause for one exact cell operation. */
    struct StreamingDiagnosticFailureRecord final {
        StreamingCellOperationHandle operation; /**< Exact operation and cell-attempt fence. */
        StreamingCellOperationOutcome reason{}; /**< Cancelled, Failed, Replaced or Shutdown. */

        [[nodiscard]] constexpr auto operator<=>(const StreamingDiagnosticFailureRecord &) const noexcept = default;
    };

    /** @brief Borrowed authority-safe-point facts copied transactionally by snapshot creation. */
    struct WorldStreamingDiagnosticSnapshotInput final {
        StreamingRuntimeOwnerToken owner;                  /**< Exact mounted authority lifetime. */
        WorldStreamingDiagnosticRevision revision;         /**< Non-zero immutable snapshot revision. */
        WorldStreamingRuntimeCompositionState lifecycle{}; /**< Current composition lifecycle fact. */
        WorldStreamingDiagnosticLimits limits;             /**< Mandatory owned-row ceilings. */
        StreamingDiagnosticQueueSnapshot queue;            /**< Scheduler queue pressure at the same safe point. */
    };

    /**
     * @brief Owned immutable, deterministic and bounded World Streaming diagnostic snapshot.
     * @details Creation copies authority-safe-point facts only. The value performs no discovery, logging,
     *          serialization, runtime mutation or independent residency/budget accounting.
     */
    class WorldStreamingDiagnosticSnapshot final {
    public:
        /**
         * @brief Validates and owns one complete diagnostic projection.
         * @param input Owner, revision, lifecycle, queue and mandatory capacity facts.
         * @param policy Current immutable multidimensional budget policy.
         * @param sample Current usage sample for exactly @p policy.
         * @param sources Current immutable source desired-state rows.
         * @param cells Current and retained retiring cell-attempt rows.
         * @param failures Typed causes correlated to exact rows in @p cells.
         * @return Canonically ordered snapshot, or a typed invalid, stale, unsupported, conflict or capacity failure.
         * @post Failure publishes no partial snapshot and leaves every input unchanged.
         */
        [[nodiscard]] static Result<WorldStreamingDiagnosticSnapshot> Create(const WorldStreamingDiagnosticSnapshotInput &input,
                                                                             const StreamingBudgetPolicy &policy,
                                                                             const StreamingBudgetSample &sample,
                                                                             std::span<const StreamingSourceDesiredState> sources,
                                                                             std::span<const StreamingCellStateRecord> cells,
                                                                             std::span<const StreamingDiagnosticFailureRecord> failures);

        /** @brief Returns the exact mounted authority lifetime. @return Immutable owner token. */
        [[nodiscard]] const StreamingRuntimeOwnerToken &Owner() const noexcept;
        /** @brief Returns this immutable projection revision. @return Non-zero revision. */
        [[nodiscard]] WorldStreamingDiagnosticRevision Revision() const noexcept;
        /** @brief Returns the captured authority lifecycle. @return Active, cancelling, draining or closed. */
        [[nodiscard]] WorldStreamingRuntimeCompositionState Lifecycle() const noexcept;
        /** @brief Returns captured scheduler queue pressure. @return Immutable queue facts. */
        [[nodiscard]] const StreamingDiagnosticQueueSnapshot &Queue() const noexcept;
        /** @brief Returns the captured multidimensional budget policy. @return Immutable owned policy. */
        [[nodiscard]] const StreamingBudgetPolicy &BudgetPolicy() const noexcept;
        /** @brief Returns the captured multidimensional usage sample. @return Immutable owned sample. */
        [[nodiscard]] const StreamingBudgetSample &BudgetSample() const noexcept;
        /** @brief Returns source rows in stable source-identity order. @return Borrow valid for this value's lifetime. */
        [[nodiscard]] std::span<const StreamingSourceDesiredState> Sources() const noexcept;
        /** @brief Returns cell attempts in canonical cell/generation order. @return Borrow valid for this value's lifetime. */
        [[nodiscard]] std::span<const StreamingCellStateRecord> Cells() const noexcept;
        /** @brief Returns failures in canonical cell/generation/operation order. @return Borrow valid for this value's lifetime. */
        [[nodiscard]] std::span<const StreamingDiagnosticFailureRecord> Failures() const noexcept;

    private:
        /** @brief Owns an already validated and canonically ordered projection. */
        WorldStreamingDiagnosticSnapshot(WorldStreamingDiagnosticSnapshotInput input, StreamingBudgetPolicy policy,
                                         StreamingBudgetSample sample, std::vector<StreamingSourceDesiredState> sources,
                                         std::vector<StreamingCellStateRecord> cells,
                                         std::vector<StreamingDiagnosticFailureRecord> failures) noexcept;

        WorldStreamingDiagnosticSnapshotInput input_;
        StreamingBudgetPolicy policy_;
        StreamingBudgetSample sample_;
        std::vector<StreamingSourceDesiredState> sources_;
        std::vector<StreamingCellStateRecord> cells_;
        std::vector<StreamingDiagnosticFailureRecord> failures_;
    };
}  // namespace Horo::WorldStreaming
