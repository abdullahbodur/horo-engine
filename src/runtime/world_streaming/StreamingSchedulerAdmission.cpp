#include "Horo/WorldStreaming/StreamingSchedulerAdmission.h"

#include "Horo/WorldStreaming/WorldStreamingErrors.h"

#include <algorithm>

namespace Horo::WorldStreaming {
    namespace {
        template <typename T> [[nodiscard]] Result<T> Failure(const ErrorCodeDescriptor &descriptor) {
            return Result<T>::Failure(MakeError(descriptor));
        }

        [[nodiscard]] StreamingSchedulerReservation MakeReservation(const StreamingCellOperationHandle &operation) {
            return {
                .id = StreamingSchedulerReservationId::Create(operation.operation.Value()).Value(),
                .operation = operation,
            };
        }
    }  // namespace

    /** @copydoc StreamingSchedulerReservation::IsValid */
    bool StreamingSchedulerReservation::IsValid() const noexcept {
        return id.IsValid() && operation.IsValid() && id.Value() == operation.operation.Value();
    }

    /** @copydoc StreamingSchedulerAdmissionLedger::Create */
    Result<StreamingSchedulerAdmissionLedger> StreamingSchedulerAdmissionLedger::Create(const std::uint32_t capacity) {
        if (capacity == 0)
            return Failure<StreamingSchedulerAdmissionLedger>(WorldStreamingErrors::SchedulerAdmissionInvalid);
        return Result<StreamingSchedulerAdmissionLedger>::Success(StreamingSchedulerAdmissionLedger{capacity});
    }

    /** @copydoc StreamingSchedulerAdmissionLedger::TryAdmit */
    Result<StreamingSchedulerAdmission> StreamingSchedulerAdmissionLedger::TryAdmit(const StreamingCellOperation &operation) {
        if (state_ != StreamingSchedulerAdmissionState::Accepting)
            return Failure<StreamingSchedulerAdmission>(WorldStreamingErrors::SchedulerLifecycleUnavailable);
        if (!operation.Handle().IsValid() || operation.State() != StreamingCellOperationState::Queued || operation.IsTerminal())
            return Failure<StreamingSchedulerAdmission>(WorldStreamingErrors::SchedulerAdmissionInvalid);
        if (reservations_.size() >= capacity_)
            return Failure<StreamingSchedulerAdmission>(WorldStreamingErrors::SchedulerCapacityExceeded);

        const auto reservation = MakeReservation(operation.Handle());
        if (std::ranges::any_of(reservations_, [&reservation](const StreamingSchedulerReservation &current) {
            return current.id == reservation.id || current.operation == reservation.operation;
        }))
            return Failure<StreamingSchedulerAdmission>(WorldStreamingErrors::SchedulerReservationConflict);

        const auto admitted = operation.Advance(operation.Handle(), StreamingCellOperationTransition::Admit);
        if (admitted.HasError())
            return Result<StreamingSchedulerAdmission>::Failure(admitted.ErrorValue());

        reservations_.push_back(reservation);
        return Result<StreamingSchedulerAdmission>::Success({.operation = admitted.Value(), .reservation = reservation});
    }

    /** @copydoc StreamingSchedulerAdmissionLedger::Release */
    Result<void> StreamingSchedulerAdmissionLedger::Release(const StreamingSchedulerReservation &reservation,
                                                            const StreamingCellOperation &operation) {
        if (!reservation.IsValid())
            return Result<void>::Failure(MakeError(WorldStreamingErrors::SchedulerAdmissionInvalid));
        const auto found = std::ranges::find(reservations_, reservation.id, &StreamingSchedulerReservation::id);
        if (found == reservations_.end() || *found != reservation || operation.Handle() != reservation.operation)
            return Result<void>::Failure(MakeError(WorldStreamingErrors::SchedulerReservationStale));
        if (!operation.IsTerminal())
            return Result<void>::Failure(MakeError(WorldStreamingErrors::SchedulerLifecycleUnavailable));

        reservations_.erase(found);
        if (state_ == StreamingSchedulerAdmissionState::Draining && reservations_.empty())
            state_ = StreamingSchedulerAdmissionState::Closed;
        return Result<void>::Success();
    }

    /** @copydoc StreamingSchedulerAdmissionLedger::BeginShutdown */
    void StreamingSchedulerAdmissionLedger::BeginShutdown() noexcept {
        if (state_ == StreamingSchedulerAdmissionState::Accepting)
            state_ = reservations_.empty() ? StreamingSchedulerAdmissionState::Closed : StreamingSchedulerAdmissionState::Draining;
    }

    /** @copydoc StreamingSchedulerAdmissionLedger::State */
    StreamingSchedulerAdmissionState StreamingSchedulerAdmissionLedger::State() const noexcept {
        return state_;
    }

    /** @copydoc StreamingSchedulerAdmissionLedger::Capacity */
    std::uint32_t StreamingSchedulerAdmissionLedger::Capacity() const noexcept {
        return capacity_;
    }

    /** @copydoc StreamingSchedulerAdmissionLedger::ReservedCount */
    std::size_t StreamingSchedulerAdmissionLedger::ReservedCount() const noexcept {
        return reservations_.size();
    }

    StreamingSchedulerAdmissionLedger::StreamingSchedulerAdmissionLedger(const std::uint32_t capacity) : capacity_(capacity) {
        reservations_.reserve(capacity);
    }
}  // namespace Horo::WorldStreaming
