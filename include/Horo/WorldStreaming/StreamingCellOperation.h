#pragma once

/**
 * @file StreamingCellOperation.h
 * @brief Transactional lifecycle contract for one fenced cell operation.
 */

#include "Horo/Foundation/StrongId.h"
#include "Horo/WorldStreaming/WorldStreamingIdentity.h"

#include <cstdint>

namespace Horo::WorldStreaming {
    namespace Detail {
        /** @brief Tag that keeps cell-operation identities distinct from other non-zero counters. */
        struct StreamingCellOperationIdTag;
    }  // namespace Detail

    /** @brief Stable identity for one cell operation; zero is reserved as invalid. */
    using StreamingCellOperationId =
        Foundation::Detail::NonZeroId64<Detail::StreamingCellOperationIdTag, WorldStreamingErrors::IdentityInvalid>;

    /** @brief Execution phase of one operation; it does not replace canonical cell residency state. */
    enum class StreamingCellOperationState : std::uint8_t {
        Queued,
        Admitted,
        Preparing,
        Activating,
        Retiring,
        Terminal,
    };

    /** @brief Final disposition, or the requested disposition while accepted work retires. */
    enum class StreamingCellOperationOutcome : std::uint8_t {
        None,
        Succeeded,
        Cancelled,
        Failed,
        Replaced,
        Shutdown,
    };

    /** @brief Typed command applied to one exact operation state. */
    enum class StreamingCellOperationTransition : std::uint8_t {
        Admit,
        BeginPreparation,
        BeginActivation,
        Complete,
        Cancel,
        Fail,
        Replace,
        Shutdown,
        AcknowledgeRetirement,
    };

    /** @brief Exact routing identity required to mutate or acknowledge one operation. */
    struct StreamingCellOperationHandle final {
        StreamingCellOperationId operation; /**< Exact operation identity. */
        StreamingFence fence;               /**< Exact mounted cell-attempt fence. */

        /** @brief Checks representation, not current authority ownership. @return True when both identity parts are valid. */
        [[nodiscard]] bool IsValid() const noexcept;
        [[nodiscard]] constexpr auto operator<=>(const StreamingCellOperationHandle &) const noexcept = default;
    };

    /** @brief Immutable state-machine value for one fenced cell operation. */
    class StreamingCellOperation final {
    public:
        /**
         * @brief Creates a queued operation without accepting work or resources.
         * @param handle Exact non-zero operation identity and cell fence.
         * @return Queued operation or WorldStreamingErrors::CellOperationInvalid.
         */
        [[nodiscard]] static Result<StreamingCellOperation> Create(StreamingCellOperationHandle handle);

        /** @brief Returns the exact routing identity. @return Borrowed handle owned by this value. */
        [[nodiscard]] const StreamingCellOperationHandle &Handle() const noexcept;
        /** @brief Returns the execution phase. @return Current operation phase. */
        [[nodiscard]] StreamingCellOperationState State() const noexcept;
        /** @brief Returns the terminal or pending-retirement disposition. @return None until completion or interruption is requested. */
        [[nodiscard]] StreamingCellOperationOutcome Outcome() const noexcept;
        /** @brief Reports whether no more transitions are accepted. @return True only in Terminal. */
        [[nodiscard]] bool IsTerminal() const noexcept;

        /**
         * @brief Applies one transition against an exact handle without mutating this value.
         * @param expected Exact operation identity and fence supplied by the completion or owner command.
         * @param transition Requested typed lifecycle transition.
         * @return Successor value, or a typed invalid, stale, unsupported, or illegal-transition error.
         * @post On failure this operation remains unchanged.
         */
        [[nodiscard]] Result<StreamingCellOperation> Advance(const StreamingCellOperationHandle &expected,
                                                             StreamingCellOperationTransition transition) const;

    private:
        StreamingCellOperation(StreamingCellOperationHandle handle, StreamingCellOperationState state,
                               StreamingCellOperationOutcome outcome) noexcept;

        StreamingCellOperationHandle handle_{};
        StreamingCellOperationState state_{StreamingCellOperationState::Queued};
        StreamingCellOperationOutcome outcome_{StreamingCellOperationOutcome::None};
    };
}  // namespace Horo::WorldStreaming
