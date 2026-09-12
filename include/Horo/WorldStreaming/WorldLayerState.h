#pragma once

/**
 * @file WorldLayerState.h
 * @brief Ordered world-layer load and activation state independent of cell residency.
 */

#include "Horo/WorldStreaming/WorldLayerOwnershipModel.h"

#include <compare>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace Horo::WorldStreaming {
    namespace Detail {
        struct WorldLayerStateRevisionTag;
    }  // namespace Detail

    /** @brief Monotonic revision of one stable layer's state publication. */
    using WorldLayerStateRevision =
        Foundation::Detail::NonZeroId64<Detail::WorldLayerStateRevisionTag, WorldStreamingErrors::IdentityInvalid>;

    /** @brief Ordered layer lifecycle, deliberately independent from physical cell residency. */
    enum class WorldLayerState : std::uint8_t {
        Unloaded,
        Loading,
        Loaded,
        Activating,
        Activated,
        Deactivating,
        Unloading,
        Failed,
    };

    /** @brief Pending cleanup outcome retained while a cancellation or failure rolls back in-flight work. */
    enum class WorldLayerStateRollbackDisposition : std::uint8_t {
        None,
        CancellationPending,
        FailurePending,
    };

    /** @brief Typed command applied to one exact layer-state publication. */
    enum class WorldLayerStateTransition : std::uint8_t {
        BeginLoad,
        CompleteLoad,
        BeginActivation,
        CompleteActivation,
        BeginDeactivation,
        CompleteDeactivation,
        BeginUnload,
        CompleteUnload,
        Cancel,
        Fail,
    };

    /** @brief Exact identity and revisions required to advance one layer state. */
    struct WorldLayerStateFence final {
        StreamingRuntimeOwnerToken world{};      /**< Exact mounted-world lifetime. */
        StreamingLayerId layer{};                /**< Stable source layer identity. */
        WorldLayerRevision ownershipRevision{};  /**< Exact classification/owner publication. */
        WorldLayerStateRevision stateRevision{}; /**< Exact current state publication. */

        /** @brief Checks the complete fence representation. @return True when every identity and revision is usable. */
        [[nodiscard]] bool IsValid() const noexcept;
        [[nodiscard]] constexpr auto operator<=>(const WorldLayerStateFence &) const noexcept = default;
    };

    /** @brief Immutable state fact retaining the exact classification and control owner it applies to. */
    struct WorldLayerStateRecord final {
        WorldLayerOwnershipDescriptor ownership{};                /**< Stable layer classification and exact control owner. */
        WorldLayerStateRevision revision{};                       /**< Monotonic state-machine revision. */
        WorldLayerState state{};                                  /**< Current ordered load/activation state. */
        WorldLayerStateRollbackDisposition rollbackDisposition{}; /**< Outcome retained until cleanup completes. */

        /** @brief Checks the retained ownership and state representation. @return True when structurally usable. */
        [[nodiscard]] bool IsValid() const noexcept;
        /** @brief Returns the exact fence required by the next operation. @return Stable identity and current revisions. */
        [[nodiscard]] WorldLayerStateFence Fence() const noexcept;
        [[nodiscard]] constexpr auto operator<=>(const WorldLayerStateRecord &) const noexcept = default;
    };

    /** @brief Lifecycle gate for admission and transition of layer state. */
    enum class WorldLayerStateAuthorityState : std::uint8_t {
        Active,
        Cancelling,
        Closed,
    };

    /** @brief Bounded immutable authority snapshot for initial layer-state admission. */
    struct WorldLayerStateAdmissionContext final {
        StreamingRuntimeOwnerToken expectedWorld{}; /**< Exact mounted-world authority. */
        std::size_t layerCount{};                   /**< Distinct state records currently charged. */
        std::size_t layerCapacity{};                /**< Maximum admitted state records. */
        WorldLayerStateAuthorityState authorityState{WorldLayerStateAuthorityState::Closed}; /**< Current lifecycle gate. */
    };

    /** @brief Lifecycle and optional exact handoff evidence for ownership replacement. */
    struct WorldLayerStateOwnershipReplacementContext final {
        WorldLayerStateAuthorityState authorityState{WorldLayerStateAuthorityState::Closed}; /**< Current lifecycle gate. */
        std::optional<WorldLayerValidatedHandoffContext> handoff{}; /**< Current authorization and fresh target evidence. */
    };

    /** @brief Exact state-machine command and compare-and-swap fence. */
    struct WorldLayerStateTransitionRequest final {
        WorldLayerStateFence expected{};        /**< Exact current record fence. */
        WorldLayerStateTransition transition{}; /**< Requested ordered transition. */
    };

    /**
     * @brief Creates one Unloaded state record without fabricating load completion.
     * @param ownership Valid stable layer classification and control owner.
     * @param context Exact world, capacity, and lifecycle snapshot.
     * @return Initial revision-one record or a typed invalid, stale, capacity, or lifecycle error.
     */
    [[nodiscard]] Result<WorldLayerStateRecord> CreateWorldLayerStateRecord(const WorldLayerOwnershipDescriptor &ownership,
                                                                            const WorldLayerStateAdmissionContext &context);

    /**
     * @brief Applies one ordered layer transition without mutating the current record.
     * @param current Current immutable layer-state fact.
     * @param request Exact current fence and requested transition.
     * @param authorityState Current owner lifecycle gate.
     * @return Successor record or a typed invalid, stale, unsupported, illegal-transition, lifecycle, or exhaustion error.
     * Repeated Cancel in a rollback state returns the exact current record without consuming a revision. Fail retains a pending
     * disposition until explicit cleanup completion can safely publish Failed.
     * @post Failure leaves @p current unchanged.
     */
    [[nodiscard]] Result<WorldLayerStateRecord> AdvanceWorldLayerState(const WorldLayerStateRecord &current,
                                                                       const WorldLayerStateTransitionRequest &request,
                                                                       WorldLayerStateAuthorityState authorityState);

    /**
     * @brief Rebinds a quiescent unloaded or cleanup-complete failed record to an admitted ownership successor.
     * @param current Current immutable layer-state fact.
     * @param replacement Exact successor classification/owner publication for the same stable layer.
     * @param expected Exact current state fence.
     * @param context Current lifecycle gate and exact handoff evidence when control ownership changes.
     * @return Unloaded successor record or a typed primary-ownership, state, lifecycle, stale, or exhaustion error.
     * @post Active or in-flight content is never replaced and failure leaves @p current unchanged.
     */
    [[nodiscard]] Result<WorldLayerStateRecord> ReplaceWorldLayerStateOwnership(const WorldLayerStateRecord &current,
                                                                                const WorldLayerOwnershipDescriptor &replacement,
                                                                                const WorldLayerStateFence &expected,
                                                                                const WorldLayerStateOwnershipReplacementContext &context);

    /** @brief Advances a layer-state revision without wrapping. @param current Current valid revision. @return Successor or
     * GenerationExhausted. */
    [[nodiscard]] Result<WorldLayerStateRevision> NextWorldLayerStateRevision(WorldLayerStateRevision current);
}  // namespace Horo::WorldStreaming
