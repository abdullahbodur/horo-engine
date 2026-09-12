#pragma once

/**
 * @file RuntimeEntityCellExitOperation.h
 * @brief Transactional retirement or ownership handoff for a runtime-spawned cell-owned entity.
 */

#include "Horo/WorldStreaming/WorldObjectOwnership.h"

#include <compare>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace Horo::WorldStreaming {
    namespace Detail {
        struct RuntimeEntityCellExitOperationIdTag;
    }  // namespace Detail

    /** @brief Stable identity of one runtime-entity cell-exit transaction. */
    using RuntimeEntityCellExitOperationId =
        Foundation::Detail::NonZeroId64<Detail::RuntimeEntityCellExitOperationIdTag, WorldStreamingErrors::IdentityInvalid>;

    /** @brief Policy-authorized terminal action for the source entity. */
    enum class RuntimeEntityCellExitDisposition : std::uint8_t {
        Retire,
        Handoff,
    };

    /** @brief Explicit execution phase; source authority remains current until source retirement begins. */
    enum class RuntimeEntityCellExitState : std::uint8_t {
        Queued,
        Admitted,
        PreparingDestination,
        DestinationAccepted,
        RetiringSource,
        RollingBackDestination,
        Terminal,
    };

    /** @brief Terminal result, or the result retained while committed work drains. */
    enum class RuntimeEntityCellExitOutcome : std::uint8_t {
        None,
        Retired,
        HandedOff,
        Cancelled,
        Failed,
        Replaced,
        Shutdown,
    };

    /** @brief Typed command applied to one exact cell-exit operation state. */
    enum class RuntimeEntityCellExitTransition : std::uint8_t {
        Admit,
        BeginDestinationPreparation,
        AcceptDestination,
        BeginSourceRetirement,
        Cancel,
        Fail,
        Replace,
        Shutdown,
        AcknowledgeSourceRetirement,
        AcknowledgeDestinationRollback,
    };

    /** @brief Exact identity and source-generation fence required by every command and acknowledgement. */
    struct RuntimeEntityCellExitHandle final {
        RuntimeEntityCellExitOperationId operation{}; /**< Stable transaction identity. */
        RuntimeSpawnedObjectId entity{};              /**< Stable runtime-spawned entity identity. */
        StreamingFence source{};                      /**< Exact originating cell attempt. */

        /** @brief Checks representation, not live authority. @return True when every identity component is valid. */
        [[nodiscard]] bool IsValid() const noexcept;
        [[nodiscard]] constexpr auto operator<=>(const RuntimeEntityCellExitHandle &) const noexcept = default;
    };

    /** @brief Complete immutable request to retire an entity or stage its ownership successor. */
    struct RuntimeEntityCellExitRequest final {
        RuntimeEntityCellExitHandle handle{};                        /**< Exact operation, entity, and source-cell fence. */
        WorldObjectOwnershipDescriptor sourceOwnership{};            /**< Caller-observed current cell ownership. */
        std::optional<WorldObjectOwnershipDescriptor> destination{}; /**< Required exact successor for handoff only. */
    };

    /** @brief Immutable authority and capacity snapshot used for pure operation creation. */
    struct RuntimeEntityCellExitAdmissionContext final {
        StreamingRuntimeOwnerToken expectedWorld{};                                   /**< Exact active mounted-world authority. */
        StreamingFence retiringCell{};                                                /**< Exact cell generation being unloaded. */
        WorldObjectOwnershipDescriptor currentOwnership{};                            /**< Authority-observed current entity ownership. */
        std::size_t inFlightOperations{};                                             /**< Operations already charged to this owner. */
        std::size_t operationCapacity{};                                              /**< Maximum simultaneously admitted operations. */
        WorldObjectOwnershipOwnerState state{WorldObjectOwnershipOwnerState::Closed}; /**< Owner lifecycle gate. */
    };

    /** @brief Immutable transaction value for one runtime-spawned entity leaving an exact source cell generation. */
    class RuntimeEntityCellExitOperation final {
    public:
        /**
         * @brief Validates and creates a queued retirement or handoff transaction without mutating authority state.
         * @param request Exact source observation and optional ownership successor.
         * @param context Current authority, cell generation, ownership fact, capacity, and lifecycle snapshot.
         * @return Queued operation or a typed invalid, stale, unsupported, capacity, revision, or lifecycle error.
         * @post Failure leaves all supplied values and live ownership state unchanged.
         */
        [[nodiscard]] static Result<RuntimeEntityCellExitOperation> Create(const RuntimeEntityCellExitRequest &request,
                                                                           const RuntimeEntityCellExitAdmissionContext &context);

        /** @brief Returns the exact routing fence. @return Handle owned by this immutable value. */
        [[nodiscard]] const RuntimeEntityCellExitHandle &Handle() const noexcept;
        /** @brief Returns the policy-authorized source disposition. @return Retire or Handoff. */
        [[nodiscard]] RuntimeEntityCellExitDisposition Disposition() const noexcept;
        /** @brief Returns the current execution phase. @return Current state-machine phase. */
        [[nodiscard]] RuntimeEntityCellExitState State() const noexcept;
        /** @brief Returns the terminal or draining result. @return None until commit or interruption. */
        [[nodiscard]] RuntimeEntityCellExitOutcome Outcome() const noexcept;
        /** @brief Returns the exact source ownership fact captured at creation. @return Immutable source descriptor. */
        [[nodiscard]] const WorldObjectOwnershipDescriptor &SourceOwnership() const noexcept;
        /** @brief Returns the validated ownership successor for handoff. @return Destination descriptor, or no value for retirement. */
        [[nodiscard]] const std::optional<WorldObjectOwnershipDescriptor> &DestinationOwnership() const noexcept;
        /** @brief Reports whether no more transitions are accepted. @return True only in Terminal. */
        [[nodiscard]] bool IsTerminal() const noexcept;
        /** @brief Reports whether ownership publication/source retirement has crossed its non-cancellable commit boundary. */
        [[nodiscard]] bool IsCommitted() const noexcept;

        /**
         * @brief Applies one command against an exact operation, entity, and source-generation fence without mutating this value.
         * @param expected Exact routing handle supplied by an owner command or completion.
         * @param transition Requested lifecycle transition.
         * @return Successor value, or a typed invalid, stale, unsupported, or illegal-transition error.
         * @post Before commit, interruption never retires the source; after commit, only source-retirement acknowledgement may finish it.
         * @post Failure leaves this operation unchanged.
         */
        [[nodiscard]] Result<RuntimeEntityCellExitOperation> Advance(const RuntimeEntityCellExitHandle &expected,
                                                                     RuntimeEntityCellExitTransition transition) const;

    private:
        RuntimeEntityCellExitOperation(RuntimeEntityCellExitHandle handle, RuntimeEntityCellExitDisposition disposition,
                                       WorldObjectOwnershipDescriptor source, std::optional<WorldObjectOwnershipDescriptor> destination,
                                       RuntimeEntityCellExitState state, RuntimeEntityCellExitOutcome outcome) noexcept;

        RuntimeEntityCellExitHandle handle_{};
        RuntimeEntityCellExitDisposition disposition_{RuntimeEntityCellExitDisposition::Retire};
        WorldObjectOwnershipDescriptor source_{};
        std::optional<WorldObjectOwnershipDescriptor> destination_{};
        RuntimeEntityCellExitState state_{RuntimeEntityCellExitState::Queued};
        RuntimeEntityCellExitOutcome outcome_{RuntimeEntityCellExitOutcome::None};
    };
}  // namespace Horo::WorldStreaming
