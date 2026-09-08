#pragma once

/**
 * @file StreamingSchedulerAdmission.h
 * @brief Atomic scheduler-slot reservation for fenced cell operations.
 */

#include "Horo/WorldStreaming/StreamingCellOperation.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace Horo::WorldStreaming {
    namespace Detail {
        /** @brief Tag that keeps scheduler reservation identities distinct from operation identities. */
        struct StreamingSchedulerReservationIdTag;
    }  // namespace Detail

    /** @brief Stable scheduler reservation identity; zero is reserved as invalid. */
    using StreamingSchedulerReservationId =
        Foundation::Detail::NonZeroId64<Detail::StreamingSchedulerReservationIdTag, WorldStreamingErrors::IdentityInvalid>;

    /** @brief Admission lifecycle owned by the partition authority's scheduler ledger. */
    enum class StreamingSchedulerAdmissionState : std::uint8_t {
        Accepting,
        Draining,
        Closed,
    };

    /** @brief Exact slot reservation retained until its operation becomes terminal. */
    struct StreamingSchedulerReservation final {
        StreamingSchedulerReservationId id;     /**< Typed identity derived from the exact operation identity. */
        StreamingCellOperationHandle operation; /**< Operation and fence that own the reserved slot. */

        /** @brief Checks the complete reservation representation. @return True when its identity and operation handle are valid. */
        [[nodiscard]] bool IsValid() const noexcept;
        [[nodiscard]] constexpr auto operator<=>(const StreamingSchedulerReservation &) const noexcept = default;
    };

    /** @brief Atomic success payload containing both the admitted operation and its reserved scheduler slot. */
    struct StreamingSchedulerAdmission final {
        StreamingCellOperation operation;          /**< Admitted successor operation. */
        StreamingSchedulerReservation reservation; /**< Slot owned by the admitted operation. */
    };

    /** @brief Bounded scheduler-slot ledger owned by one partition authority. */
    class StreamingSchedulerAdmissionLedger final {
    public:
        /**
         * @brief Creates an accepting ledger and preallocates its bounded slot storage.
         * @param capacity Maximum concurrently admitted operations; must be positive.
         * @return Empty ledger or WorldStreamingErrors::SchedulerAdmissionInvalid.
         */
        [[nodiscard]] static Result<StreamingSchedulerAdmissionLedger> Create(std::uint32_t capacity);

        /**
         * @brief Atomically reserves a slot and advances one exact queued operation to Admitted.
         * @param operation Queued operation whose handle will own the slot.
         * @return Admitted successor and reservation, or a typed failure without ledger mutation.
         */
        [[nodiscard]] Result<StreamingSchedulerAdmission> TryAdmit(const StreamingCellOperation &operation);

        /**
         * @brief Releases the exact slot after its operation reaches a terminal disposition.
         * @param reservation Exact reservation returned by TryAdmit.
         * @param operation Terminal successor retaining the reservation's exact operation handle.
         * @return Success, or a typed failure without ledger mutation.
         */
        [[nodiscard]] Result<void> Release(const StreamingSchedulerReservation &reservation, const StreamingCellOperation &operation);

        /** @brief Stops new admission while retained reservations drain. */
        void BeginShutdown() noexcept;

        /** @brief Returns the admission lifecycle. @return Accepting, Draining, or Closed. */
        [[nodiscard]] StreamingSchedulerAdmissionState State() const noexcept;
        /** @brief Returns the fixed concurrent-operation ceiling. @return Positive slot capacity. */
        [[nodiscard]] std::uint32_t Capacity() const noexcept;
        /** @brief Returns slots still owned by admitted operations. @return Current reservation count. */
        [[nodiscard]] std::size_t ReservedCount() const noexcept;

    private:
        explicit StreamingSchedulerAdmissionLedger(std::uint32_t capacity);

        std::uint32_t capacity_{};
        StreamingSchedulerAdmissionState state_{StreamingSchedulerAdmissionState::Accepting};
        std::vector<StreamingSchedulerReservation> reservations_;
    };
}  // namespace Horo::WorldStreaming
