#pragma once

/**
 * @file NetworkHandles.h
 * @brief Backend-neutral transport handles, channel identity, and admission validation.
 */

#include "Horo/Foundation/Handles.h"
#include "Horo/Foundation/Result.h"
#include "Horo/Network/NetworkErrors.h"

#include <compare>
#include <cstdint>
#include <limits>

namespace Horo::Network {
    /** @brief Pure caller-owned state checked before transport work is admitted. */
    enum class TransportAdmissionState : std::uint8_t {
        Accepting,
        Cancelled,
        ShuttingDown,
        Count
    };

    /** @brief Safe diagnostic projection of a process-local generation handle. */
    struct TransportHandleDiagnostic final {
        std::uint32_t slot{std::numeric_limits<std::uint32_t>::max()}; /**< Opaque owner slot. */
        std::uint32_t generation{};                                    /**< Exact observed generation. */

        constexpr auto operator<=>(const TransportHandleDiagnostic &) const noexcept = default;
    };

    /**
     * @brief Process-local owner-issued transport handle with an exact non-zero generation.
     *
     * Representation validation does not prove that the issuing registry still owns the slot. Every operation
     * must compare the complete handle with the registry's current slot generation. Handles are never serialized,
     * used as authenticated peer identity, or translated into a native socket handle by public code.
     */
    template <typename Tag> class TransportHandle final {
    public:
        using SlotHandle = Horo::Handle<Tag>;

        /** @brief Constructs the reserved invalid handle. */
        constexpr TransportHandle() = default;

        /**
         * @brief Validates an owner-issued slot and generation.
         * @param slot Process-local owner slot; InvalidIndex is reserved.
         * @param generation Non-zero slot generation.
         * @return Typed handle or NetworkErrors::TransportHandleInvalid.
         */
        [[nodiscard]] static Result<TransportHandle> Create(const std::uint32_t slot, const std::uint32_t generation) {
            if (slot == SlotHandle::InvalidIndex || generation == 0)
                return Result<TransportHandle>::Failure(MakeError(NetworkErrors::TransportHandleInvalid));
            return Result<TransportHandle>::Success(TransportHandle{SlotHandle{slot, generation}});
        }

        /** @brief Checks representation, not registry residency. @return Whether slot and generation are valid. */
        [[nodiscard]] constexpr bool IsValid() const noexcept {
            return slot_.index != SlotHandle::InvalidIndex && slot_.generation != 0;
        }

        /** @brief Returns the opaque process-local slot. @return InvalidIndex only for an invalid handle. */
        [[nodiscard]] constexpr std::uint32_t Slot() const noexcept {
            return slot_.index;
        }

        /** @brief Returns the exact slot generation. @return Zero only for an invalid handle. */
        [[nodiscard]] constexpr std::uint32_t Generation() const noexcept {
            return slot_.generation;
        }

        /**
         * @brief Produces the next handle for the same reclaimed slot without wrapping.
         * @return Next generation or NetworkErrors::TransportGenerationExhausted.
         * @pre The owner has made the current handle terminal and removed it from admission.
         * @post Success returns a handle unequal to this handle; the owner must publish it atomically with new slot state.
         */
        [[nodiscard]] Result<TransportHandle> NextGeneration() const {
            if (!IsValid())
                return Result<TransportHandle>::Failure(MakeError(NetworkErrors::TransportHandleInvalid));
            if (slot_.generation == std::numeric_limits<std::uint32_t>::max())
                return Result<TransportHandle>::Failure(MakeError(NetworkErrors::TransportGenerationExhausted));
            return Result<TransportHandle>::Success(TransportHandle{SlotHandle{slot_.index, slot_.generation + 1}});
        }

        /** @brief Projects only safe opaque numeric evidence. @return Slot and exact observed generation. */
        [[nodiscard]] constexpr TransportHandleDiagnostic Diagnostic() const noexcept {
            return {slot_.index, slot_.generation};
        }

        constexpr auto operator<=>(const TransportHandle &) const noexcept = default;

    private:
        explicit constexpr TransportHandle(const SlotHandle slot) noexcept : slot_(slot) {}

        SlotHandle slot_{};
    };

    struct ListenerHandleTag;
    struct ConnectionHandleTag;
    struct PeerHandleTag;

    /** @brief Generation-checked identity of one transport-owned listener slot. */
    using ListenerHandle = TransportHandle<ListenerHandleTag>;
    /** @brief Generation-checked identity of one transport-owned connection slot. */
    using ConnectionHandle = TransportHandle<ConnectionHandleTag>;
    /** @brief Transport correlation handle only; never an authenticated application principal. */
    using PeerHandle = TransportHandle<PeerHandleTag>;

    /**
     * @brief Validates operation state and an exact current owner generation before registry access.
     * @param submitted Handle supplied by a caller or queued record.
     * @param current Exact handle currently published for the owner slot.
     * @param state Caller-owned cancellation or shutdown state.
     * @return Success only for Accepting and an exact valid match; otherwise a typed network error.
     */
    template <typename Tag>
    [[nodiscard]] Result<void> ValidateTransportHandle(const TransportHandle<Tag> submitted, const TransportHandle<Tag> current,
                                                       const TransportAdmissionState state = TransportAdmissionState::Accepting) {
        if (state == TransportAdmissionState::Cancelled)
            return Result<void>::Failure(MakeError(NetworkErrors::TransportOperationCancelled));
        if (state == TransportAdmissionState::ShuttingDown)
            return Result<void>::Failure(MakeError(NetworkErrors::TransportShuttingDown));
        if (state != TransportAdmissionState::Accepting || !submitted.IsValid() || !current.IsValid() || submitted != current)
            return Result<void>::Failure(MakeError(NetworkErrors::TransportHandleInvalid));
        return Result<void>::Success();
    }

    /** @brief Zero-based backend-neutral message channel identity validated against negotiated capacity. */
    class ChannelId final {
    public:
        /** @brief Constructs channel zero; admission still validates it against a positive negotiated count. */
        constexpr ChannelId() = default;

        /**
         * @brief Validates a channel against one positive negotiated channel count.
         * @param value Zero-based channel value.
         * @param channelCount Positive exclusive channel bound.
         * @return Typed channel or NetworkErrors::TransportLimitExceeded.
         */
        [[nodiscard]] static Result<ChannelId> Create(const std::uint32_t value, const std::uint32_t channelCount) {
            if (channelCount == 0 || value >= channelCount)
                return Result<ChannelId>::Failure(MakeError(NetworkErrors::TransportLimitExceeded));
            return Result<ChannelId>::Success(ChannelId{value});
        }

        /** @brief Checks this channel against an exclusive bound. @param channelCount Positive negotiated count.
         * @return Whether the channel can be admitted under that count.
         */
        [[nodiscard]] constexpr bool IsWithin(const std::uint32_t channelCount) const noexcept {
            return channelCount != 0 && value_ < channelCount;
        }

        /** @brief Returns the zero-based channel value. @return Raw value for bounded wire/backend translation. */
        [[nodiscard]] constexpr std::uint32_t Value() const noexcept {
            return value_;
        }

        constexpr auto operator<=>(const ChannelId &) const noexcept = default;

    private:
        explicit constexpr ChannelId(const std::uint32_t value) noexcept : value_(value) {}

        std::uint32_t value_{};
    };
}  // namespace Horo::Network
