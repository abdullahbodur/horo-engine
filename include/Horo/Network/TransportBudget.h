#pragma once

/**
 * @file TransportBudget.h
 * @brief Bounded transport queue, rate, connection, and overload admission contract.
 */

#include "Horo/Foundation/Result.h"
#include "Horo/Network/NetworkHandles.h"

#include <compare>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace Horo::Network {
    /** @brief Closed traffic classes with distinct overload semantics. */
    enum class TransportTrafficClass : std::uint8_t {
        Reliable,         /**< Required traffic is rejected rather than silently discarded. */
        ReplaceableState, /**< Newest state may replace or supersede older state with the same key. */
        Count             /**< Invalid sentinel. */
    };

    /** @brief Successful queue-admission or explicit overload action. */
    enum class TransportBudgetAdmission : std::uint8_t {
        Enqueued,            /**< A new bounded queue record was admitted. */
        Replaced,            /**< An existing replaceable record was updated in place. */
        DroppedReplaceable,  /**< Replaceable input was not queued under current bounds. */
        ConnectionMustClose, /**< Sustained overload requires owner-controlled connection close. */
        Count                /**< Invalid sentinel. */
    };

    /** @brief Generation-checked identity for one prepared queued-work slot. */
    class TransportQueueTicket final {
    public:
        /** @brief Constructs the reserved invalid ticket. */
        constexpr TransportQueueTicket() = default;

        /** @brief Reports whether the ticket has a valid slot and generation. @return True for an owner-issued ticket. */
        [[nodiscard]] constexpr bool IsValid() const noexcept {
            return slot_ != InvalidSlot && generation_ != 0;
        }

        /** @brief Returns the opaque prepared slot. @return InvalidSlot only for an invalid ticket. */
        [[nodiscard]] constexpr std::uint32_t Slot() const noexcept {
            return slot_;
        }

        /** @brief Returns the exact slot generation. @return Zero only for an invalid ticket. */
        [[nodiscard]] constexpr std::uint32_t Generation() const noexcept {
            return generation_;
        }

        constexpr auto operator<=>(const TransportQueueTicket &) const noexcept = default;

    private:
        friend class TransportBudgetController;
        static constexpr std::uint32_t InvalidSlot = static_cast<std::uint32_t>(-1);

        constexpr TransportQueueTicket(const std::uint32_t slot, const std::uint32_t generation) noexcept
            : slot_(slot), generation_(generation) {}

        std::uint32_t slot_{InvalidSlot};
        std::uint32_t generation_{};
    };

    /** @brief Immutable version-one queue, rate, connection, and saturation policy. */
    struct TransportLimitPolicyV1 final {
        std::uint32_t contractVersion{1};                  /**< Closed public contract version. */
        std::uint64_t revision{};                          /**< Non-zero monotonically increasing policy revision. */
        std::size_t maximumActiveConnections{};            /**< Global concurrently active connection ceiling. */
        std::size_t maximumQueuedMessages{};               /**< Global queued-record ceiling. */
        std::size_t maximumQueuedBytes{};                  /**< Global queued-byte ceiling. */
        std::size_t maximumQueuedMessagesPerConnection{};  /**< Per-connection queued-record ceiling. */
        std::size_t maximumQueuedBytesPerConnection{};     /**< Per-connection queued-byte ceiling. */
        std::size_t maximumMessagesPerTick{};              /**< Global admitted-message rate per network tick. */
        std::size_t maximumBytesPerTick{};                 /**< Global admitted-byte rate per network tick. */
        std::size_t maximumMessagesPerConnectionPerTick{}; /**< Per-connection admitted-message rate. */
        std::size_t maximumBytesPerConnectionPerTick{};    /**< Per-connection admitted-byte rate. */
        std::uint32_t saturationGraceTicks{};              /**< Positive distinct saturated ticks before close. */

        constexpr auto operator<=>(const TransportLimitPolicyV1 &) const noexcept = default;
    };

    /** @brief Setup-time hard capacities used to prepare all controller storage. */
    struct TransportBudgetCapacity final {
        std::size_t maximumConnections{};    /**< Prepared direct-index connection slots. */
        std::size_t maximumQueuedMessages{}; /**< Prepared queued-work slots. */
        std::size_t maximumQueuedBytes{};    /**< Largest policy byte ceiling accepted later. */
    };

    /** @brief One backend-neutral submission admitted before payload copy or native queue mutation. */
    struct TransportBudgetSubmission final {
        ConnectionHandle connection{};                                  /**< Exact active connection generation. */
        TransportTrafficClass traffic{TransportTrafficClass::Reliable}; /**< Required or replaceable semantics. */
        std::uint64_t replaceableKey{};                                 /**< Non-zero only for replaceable state identity. */
        std::size_t bytes{};                                            /**< Positive complete queued byte charge. */
    };

    /** @brief Owned admission result with an exact queue ticket when work remains queued. */
    struct TransportBudgetDecision final {
        TransportBudgetAdmission admission{TransportBudgetAdmission::Enqueued}; /**< Explicit result. */
        TransportQueueTicket ticket{};                                          /**< Valid for Enqueued or Replaced only. */
        std::size_t replacedBytes{};                                            /**< Previous charge for Replaced, otherwise zero. */

        constexpr auto operator<=>(const TransportBudgetDecision &) const noexcept = default;
    };

    /** @brief Immutable bounded accounting projection for diagnostics and tests. */
    struct TransportBudgetSnapshot final {
        std::uint64_t policyRevision{};  /**< Exact active policy revision. */
        std::uint64_t tick{};            /**< Current non-zero network tick. */
        std::size_t activeConnections{}; /**< Current active connection count. */
        std::size_t queuedMessages{};    /**< Current queued record count. */
        std::size_t queuedBytes{};       /**< Current queued byte charge. */
    };

    /**
     * @brief Owner-thread bounded transport admission and accounting service.
     *
     * Create prepares all connection and queue storage. Runtime operations perform finite scans only, allocate no
     * storage, invoke no callbacks, and expose no native backend values. One owner thread serializes every call.
     */
    class TransportBudgetController final {
    public:
        /**
         * @brief Creates prepared storage and validates the initial complete policy.
         * @param capacity Immutable hard ceilings for later policy replacements.
         * @param policy Initial complete version-one policy.
         * @return Controller or a typed invalid/capacity failure.
         */
        [[nodiscard]] static Result<TransportBudgetController> Create(const TransportBudgetCapacity &capacity,
                                                                      const TransportLimitPolicyV1 &policy);

        /**
         * @brief Atomically replaces the active policy after validating all bounds and current usage.
         * @param expectedRevision Exact currently pinned revision; prevents lost updates.
         * @param candidate Complete higher-revision candidate.
         * @param state Caller-owned cancellation or shutdown state.
         * @return Success, or typed invalid, stale, cancelled, shutdown, or capacity failure; failure preserves the prior policy.
         */
        [[nodiscard]] Result<void> ReplacePolicy(std::uint64_t expectedRevision, const TransportLimitPolicyV1 &candidate,
                                                 TransportAdmissionState state = TransportAdmissionState::Accepting);

        /**
         * @brief Starts a strictly newer network tick and resets finite rate ledgers.
         * @param tick Positive monotonic network tick.
         * @param state Caller-owned cancellation or shutdown state.
         * @return Success or typed invalid, cancelled, or shutdown failure.
         */
        [[nodiscard]] Result<void> BeginTick(std::uint64_t tick, TransportAdmissionState state = TransportAdmissionState::Accepting);

        /** @brief Admits an owner-issued connection generation. @param connection Exact handle. @param state Operation state.
         * @return Success or typed invalid, stale, capacity, cancelled, or shutdown failure. */
        [[nodiscard]] Result<void> OpenConnection(ConnectionHandle connection,
                                                  TransportAdmissionState state = TransportAdmissionState::Accepting);

        /** @brief Removes a connection and all of its bounded queued work idempotently. @param connection Exact handle.
         * @return Number of discarded queue records, or typed stale failure. */
        [[nodiscard]] Result<std::size_t> CloseConnection(ConnectionHandle connection);

        /**
         * @brief Admits one reliable or replaceable submission under current queue and rate bounds.
         * @param submission Exact connection, traffic semantics, stable replacement key, and byte charge.
         * @param state Caller-owned cancellation or shutdown state.
         * @return Enqueue/replace/drop/close decision, or typed malformed, stale, reliable-backpressure, cancelled, or shutdown failure.
         */
        [[nodiscard]] Result<TransportBudgetDecision> Admit(const TransportBudgetSubmission &submission,
                                                            TransportAdmissionState state = TransportAdmissionState::Accepting);

        /** @brief Releases one exact queued-work ticket after send/discard. @param ticket Exact owner-issued ticket.
         * @return Success or typed stale-ticket failure. */
        [[nodiscard]] Result<void> Complete(TransportQueueTicket ticket);

        /** @brief Closes admission and discards all queued records once. @return Number of discarded records. */
        [[nodiscard]] std::size_t Shutdown() noexcept;

        /** @brief Returns immutable global accounting. @return Current policy/tick/connection/queue facts. */
        [[nodiscard]] TransportBudgetSnapshot Snapshot() const noexcept;

        /** @brief Returns the active immutable policy. @return Exact complete policy snapshot. */
        [[nodiscard]] const TransportLimitPolicyV1 &Policy() const noexcept {
            return policy_;
        }

        TransportBudgetController(TransportBudgetController &&) noexcept = default;
        TransportBudgetController &operator=(TransportBudgetController &&) noexcept = default;
        TransportBudgetController(const TransportBudgetController &) = delete;
        TransportBudgetController &operator=(const TransportBudgetController &) = delete;

    private:
        static constexpr std::uint32_t InvalidQueueSlot = static_cast<std::uint32_t>(-1);

        struct ConnectionEntry final {
            ConnectionHandle handle{};
            std::size_t queuedMessages{};
            std::size_t queuedBytes{};
            std::size_t tickMessages{};
            std::size_t tickBytes{};
            std::uint32_t saturationTicks{};
            std::uint64_t lastSaturationTick{};
            std::uint32_t queueHead{InvalidQueueSlot};
            bool initialized{};
            bool active{};
        };

        struct QueueEntry final {
            ConnectionHandle connection{};
            TransportTrafficClass traffic{TransportTrafficClass::Reliable};
            std::uint64_t replaceableKey{};
            std::size_t bytes{};
            std::uint32_t generation{1};
            std::uint32_t nextForConnection{InvalidQueueSlot};
            std::uint32_t previousForConnection{InvalidQueueSlot};
            bool occupied{};
        };

        TransportBudgetController(TransportBudgetCapacity capacity, TransportLimitPolicyV1 policy, std::vector<ConnectionEntry> connections,
                                  std::vector<QueueEntry> queue, std::vector<std::uint32_t> freeSlots) noexcept;
        /** @brief Validates caller admission state and controller lifetime. @param state Caller-owned operation state.
         * @return Success or the exact typed cancellation, shutdown, or malformed-state failure. */
        [[nodiscard]] Result<void> ValidateOperationalState(TransportAdmissionState state) const;
        [[nodiscard]] bool PolicyFitsUsage(const TransportLimitPolicyV1 &candidate) const noexcept;
        [[nodiscard]] ConnectionEntry *FindConnection(ConnectionHandle connection) noexcept;
        [[nodiscard]] QueueEntry *FindReplaceable(ConnectionEntry &connection, const TransportBudgetSubmission &submission) noexcept;
        [[nodiscard]] bool FitsNew(const ConnectionEntry &connection, std::size_t bytes) const noexcept;
        [[nodiscard]] bool FitsReplacement(const ConnectionEntry &connection, const QueueEntry &record, std::size_t bytes) const noexcept;
        [[nodiscard]] TransportBudgetDecision Overload(ConnectionEntry &connection, TransportTrafficClass traffic) noexcept;
        [[nodiscard]] Result<TransportBudgetDecision> AdmitReplacement(ConnectionEntry &connection, QueueEntry &record,
                                                                       const TransportBudgetSubmission &submission);
        [[nodiscard]] Result<TransportBudgetDecision> AdmitNew(ConnectionEntry &connection, const TransportBudgetSubmission &submission);
        [[nodiscard]] TransportQueueTicket AllocateTicket();
        void Release(QueueEntry &entry) noexcept;

        TransportBudgetCapacity capacity_;
        TransportLimitPolicyV1 policy_;
        std::vector<ConnectionEntry> connections_;
        std::vector<QueueEntry> queue_;
        std::vector<std::uint32_t> freeSlots_;
        std::size_t freeCount_{};
        std::size_t activeConnections_{};
        std::size_t queuedMessages_{};
        std::size_t queuedBytes_{};
        std::size_t tickMessages_{};
        std::size_t tickBytes_{};
        std::uint64_t tick_{};
        bool shuttingDown_{};
    };
}  // namespace Horo::Network
