#pragma once

/**
 * @file NavigationRuntimeQueues.h
 * @brief Bounded nonblocking transport between navigation callers, the owner, and provider workers.
 */

#include "Horo/Foundation/Telemetry/Operation.h"
#include "Horo/Navigation/NavigationOutcomes.h"
#include "Horo/Navigation/NavigationWorldLifecycle.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <variant>

namespace Horo::Navigation {
    inline constexpr std::uint32_t MaximumNavigationRuntimeQueueSlots = 65'536;

    /** @brief Caller-to-owner request admission command; successful queueing does not yet create a request handle. */
    struct NavigationSubmitPathCommand final {
        std::uint64_t sequence{};              /**< Non-zero caller ordering identity. */
        NavigationPathRequest request;         /**< Owned immutable provider-neutral query input. */
        CancellationToken cancellation;        /**< Caller cancellation propagated through later work. */
        Telemetry::OperationContext operation; /**< Owned cross-thread diagnostic context. */
    };

    /** @brief Caller-to-owner cancellation intent for one already admitted request generation. */
    struct NavigationCancelRequestCommand final {
        std::uint64_t sequence{}; /**< Non-zero caller ordering identity. */
        NavRequestHandle request; /**< Exact admitted request generation. */
        NavigationWorldId world;  /**< Exact world expected by the caller. */
    };

    /** @brief Closed caller-to-owner command set; adding behavior requires an explicit contract revision. */
    using NavigationRuntimeCommand = std::variant<NavigationSubmitPathCommand, NavigationCancelRequestCommand>;

    /** @brief Owner-to-worker query record with all lifetime, cancellation, and diagnostic context owned by value. */
    struct NavigationQueuedQuery final {
        std::uint64_t acceptedSequence{};      /**< Non-zero stable owner admission order. */
        NavRequestHandle handle;               /**< Exact bounded request-record generation. */
        NavigationPathRequest request;         /**< Immutable backend input. */
        NavigationWorldReadLease worldLease;   /**< Provider lifetime pin issued on the owner thread. */
        CancellationToken requestCancellation; /**< Per-request cancellation, independent of world revocation. */
        Telemetry::OperationContext operation; /**< Captured operation and diagnostic context. */
    };

    /** @brief Worker-to-owner terminal candidate; only NavIntentCommit may publish it to the request record. */
    struct NavigationQueuedCompletion final {
        std::uint64_t acceptedSequence{};          /**< Original stable owner admission order. */
        NavRequestHandle handle;                   /**< Exact bounded request-record generation. */
        NavigationSceneRuntimeId scene;            /**< Exact Scene incarnation captured at admission. */
        NavigationSceneGeneration sceneGeneration; /**< Exact Scene generation captured at admission. */
        NavigationWorldId world;                   /**< Exact navigation-world incarnation captured at admission. */
        NavigationGeneration topology;             /**< Exact immutable topology generation executed by the worker. */
        NavigationOutcome<NavigationPath> outcome; /**< Owned terminal candidate, never an inline callback. */
        Telemetry::OperationContext operation;     /**< Captured operation and diagnostic context. */
    };

    /** @brief Preparation-only power-of-two capacities and aggregate owned-storage ceiling. */
    struct NavigationRuntimeQueueDescriptor final {
        std::uint32_t commandSlots{};    /**< Caller-to-owner slots in [2, MaximumNavigationRuntimeQueueSlots]. */
        std::uint32_t querySlots{};      /**< Owner-to-worker slots in [2, MaximumNavigationRuntimeQueueSlots]. */
        std::uint32_t completionSlots{}; /**< Worker-to-owner slots in [2, MaximumNavigationRuntimeQueueSlots]. */
        std::size_t maximumOwnedBytes{}; /**< Aggregate queue-slot ceiling, excluding the fixed queue owner. */
    };

    /** @brief Saturating pressure counters for one queue direction. */
    struct NavigationQueueStats final {
        std::uint64_t enqueued{};       /**< Records successfully transferred into the queue. */
        std::uint64_t dequeued{};       /**< Records successfully transferred out of the queue. */
        std::uint64_t rejectedFull{};   /**< Records retained by callers because capacity/contention rejected admission. */
        std::uint64_t rejectedClosed{}; /**< Records retained by callers after the direction closed. */
    };

    /** @brief Snapshot of all three queue directions. */
    struct NavigationRuntimeQueueStats final {
        NavigationQueueStats commands;
        NavigationQueueStats queries;
        NavigationQueueStats completions;
    };

    /** @brief Allocation-free result of one bounded queue publication attempt. */
    enum class NavigationQueueEnqueueResult : std::uint8_t {
        Enqueued,      /**< The record moved into one preallocated slot. */
        InvalidRecord, /**< Validation rejected the record; caller ownership is unchanged. */
        Full,          /**< Capacity or the bounded contention budget rejected the record. */
        Closed,        /**< Lifecycle shutdown already closed this direction. */
    };

    /**
     * @brief Preallocated lock-free MPMC transport for navigation runtime phase boundaries.
     * @details TryEnqueue and TryDequeue perform no allocation, locking, waiting, callbacks, provider work, or live-Scene
     *          mutation. Records remain caller-owned on rejection. CloseAdmission rejects new command/query records while
     *          preserving already accepted work. The host stops and joins producers before destroying this object;
     *          CloseCompletions is called only after provider jobs have joined, then the owner drains terminal candidates.
     */
    class NavigationRuntimeQueues final {
    public:
        /** @brief Prepare all queue storage transactionally outside frame-hot work.
         * @param descriptor Valid power-of-two capacities and sufficient finite storage ceiling.
         * @return Prepared queues or typed invalid/capacity failure.
         */
        [[nodiscard]] static Result<NavigationRuntimeQueues> Create(const NavigationRuntimeQueueDescriptor &descriptor);

        NavigationRuntimeQueues(const NavigationRuntimeQueues &) = delete;
        NavigationRuntimeQueues &operator=(const NavigationRuntimeQueues &) = delete;
        /** @brief Transfer quiescent queue ownership. @param other Source becomes inert. */
        NavigationRuntimeQueues(NavigationRuntimeQueues &&other) noexcept;
        NavigationRuntimeQueues &operator=(NavigationRuntimeQueues &&) = delete;
        /** @brief Destroy storage only after producers and consumers have detached. */
        ~NavigationRuntimeQueues();

        /** @brief Nonblocking caller publication. @param command Owned command moved only on success.
         * @return Enqueued or typed invalid/full/closed rejection without allocating an Error.
         */
        [[nodiscard]] NavigationQueueEnqueueResult TryEnqueueCommand(NavigationRuntimeCommand &command) noexcept;
        /** @brief Nonblocking owner acquisition. @return One owned command, or empty when none is ready. */
        [[nodiscard]] std::optional<NavigationRuntimeCommand> TryDequeueCommand() noexcept;

        /** @brief Nonblocking owner publication to provider workers. @param query Fully owned work moved only on success.
         * @return Enqueued or typed invalid/full/closed rejection without allocating an Error.
         */
        [[nodiscard]] NavigationQueueEnqueueResult TryEnqueueQuery(NavigationQueuedQuery &query) noexcept;
        /** @brief Nonblocking worker acquisition. @return One owned query, or empty when none is ready. */
        [[nodiscard]] std::optional<NavigationQueuedQuery> TryDequeueQuery() noexcept;

        /** @brief Nonblocking worker publication to the owner. @param completion Fully owned candidate moved only on success.
         * @return Enqueued or typed invalid/full/closed rejection; never allocates or invokes a consumer inline.
         */
        [[nodiscard]] NavigationQueueEnqueueResult TryEnqueueCompletion(NavigationQueuedCompletion &completion) noexcept;
        /** @brief Nonblocking owner acquisition for NavIntentCommit. @return One candidate, or empty when none is ready. */
        [[nodiscard]] std::optional<NavigationQueuedCompletion> TryDequeueCompletion() noexcept;

        /** @brief Reject new caller commands and owner query dispatch without discarding accepted records. */
        void CloseAdmission() noexcept;
        /** @brief Reject late worker completions after jobs have joined and before Scene/service destruction. */
        void CloseCompletions() noexcept;
        /** @brief Report closed admission and completion directions with no retained records. @return True when fully drained. */
        [[nodiscard]] bool IsDrained() const noexcept;
        /** @brief Return atomic pressure accounting. @return Coherent per-counter snapshot. */
        [[nodiscard]] NavigationRuntimeQueueStats Stats() const noexcept;

    private:
        struct State;
        /** @brief Publish fully prepared queue storage. */
        explicit NavigationRuntimeQueues(std::unique_ptr<State> state) noexcept;
        std::unique_ptr<State> state_;
    };
}  // namespace Horo::Navigation
