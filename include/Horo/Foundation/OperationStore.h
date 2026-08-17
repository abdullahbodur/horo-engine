#pragma once

/**
 * @file OperationStore.h
 * @brief Bounded thread-safe snapshots for user-facing asynchronous operations.
 */

#include "Horo/Foundation/ErrorCode.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace Horo {
    using OperationId = std::uint64_t;

    /** @brief User-facing operation lifecycle. */
    enum class OperationState : std::uint8_t {
        Queued,
        Running,
        Waiting,
        Cancelling,
        Succeeded,
        Failed,
        Cancelled,
    };

    /** @brief Stable coarse operation category used by presentation filters. */
    enum class OperationKind : std::uint8_t {
        Build,
        Cook,
        Import,
        Index,
        Validation,
        Other,
    };

    /** @brief Immutable operation state copied to presentation clients. */
    struct OperationRecord {
        OperationId id{};
        OperationKind kind{OperationKind::Other};
        OperationState state{OperationState::Queued};
        std::string title;
        std::string phase;
        std::string message;
        std::optional<float> progress;
        std::optional<Error> error;
        std::chrono::steady_clock::time_point startedAt;
        std::optional<std::chrono::steady_clock::time_point> finishedAt;
        bool cancellable{false};
    };

    /** @brief Inputs retained when a user-facing operation begins. */
    struct OperationDescriptor {
        OperationKind kind{OperationKind::Other};
        std::string title;
        std::string phase;
        std::string message;
        std::optional<float> progress;
        bool cancellable{false};
        std::function<void()> requestCancel;
    };

    /** @brief Partial operation state replacement applied atomically. */
    struct OperationUpdate {
        OperationState state{OperationState::Running};
        std::string phase;
        std::string message;
        std::optional<float> progress;
        std::optional<Error> error;
    };

    /** @brief Owned bounded projection returned to slow presentation consumers. */
    struct OperationStoreSnapshot {
        std::uint64_t revision{};
        std::size_t activeCapacity{};
        std::size_t recentCapacity{};
        std::uint64_t droppedTerminalCount{};
        std::vector<OperationRecord> operations;
    };

    /** @brief Terminal operation consumer used for persistent diagnostic history. */
    class IOperationHistorySink {
    public:
        virtual ~IOperationHistorySink() = default;

        /** @brief Accepts one immutable terminal operation outside the store lock. */
        virtual void AppendTerminal(const OperationRecord &record) = 0;
    };

    /** @brief Persists terminal operation summaries through the process asynchronous logger. */
    class LoggingOperationHistorySink final : public IOperationHistorySink {
    public:
        /** @copydoc IOperationHistorySink::AppendTerminal */
        void AppendTerminal(const OperationRecord &record) override;
    };

    /** @brief Read-only operation capability exposed to presentation surfaces. */
    class IOperationQuery {
    public:
        virtual ~IOperationQuery() = default;
        /** @brief Returns a new owned snapshot only when the store revision changed. */
        [[nodiscard]] virtual std::optional<OperationStoreSnapshot> SnapshotIfChanged(std::uint64_t knownRevision) const = 0;
    };

    /** @brief Narrow operation-control capability exposed to presentation surfaces. */
    class IOperationControl {
    public:
        virtual ~IOperationControl() = default;
        /** @brief Requests cooperative cancellation without waiting for completion. */
        [[nodiscard]] virtual bool RequestCancel(OperationId id) = 0;
    };

    /** @brief Bounded active/recent operation authority with race-safe cancellation. */
    class OperationStore final : public IOperationQuery, public IOperationControl {
    public:
        /**
         * @brief Creates a store with independent active and terminal retention limits.
         * @param activeCapacity Maximum simultaneously active operations.
         * @param recentCapacity Maximum in-memory terminal operations.
         * @param historySink Optional process-owned persistent terminal history sink.
         */
        OperationStore(std::size_t activeCapacity, std::size_t recentCapacity,
                       std::shared_ptr<IOperationHistorySink> historySink = nullptr);

        /** @brief Begins an operation, or returns no ID when active admission is full. */
        [[nodiscard]] std::optional<OperationId> Begin(OperationDescriptor descriptor);

        /** @brief Applies a valid lifecycle/progress update to an active operation. */
        [[nodiscard]] bool Update(OperationId id, OperationUpdate update);

        /** @copydoc IOperationControl::RequestCancel */
        [[nodiscard]] bool RequestCancel(OperationId id) override;

        /** @copydoc IOperationQuery::SnapshotIfChanged */
        [[nodiscard]] std::optional<OperationStoreSnapshot> SnapshotIfChanged(std::uint64_t knownRevision) const override;

    private:
        struct ActiveOperation {
            OperationRecord record;
            std::function<void()> requestCancel;
            bool cancellationDispatched{false};
        };

        const std::size_t activeCapacity_;
        const std::size_t recentCapacity_;
        mutable std::mutex mutex_;
        std::unordered_map<OperationId, ActiveOperation> active_;
        std::deque<OperationRecord> recent_;
        OperationId nextId_{1};
        std::uint64_t revision_{};
        std::uint64_t droppedTerminalCount_{};
        std::shared_ptr<IOperationHistorySink> historySink_;
    };
}  // namespace Horo
