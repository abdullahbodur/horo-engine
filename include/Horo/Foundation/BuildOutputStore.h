#pragma once

#include "Horo/Foundation/OperationStore.h"

/**
 * @file BuildOutputStore.h
 * @brief Bounded thread-safe typed build diagnostics and source locations.
 */

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace Horo {
    /** @brief Typed build-result severity used by filtering and presentation. */
    enum class BuildOutputStatus : std::uint8_t {
        Info,
        Succeeded,
        Failed,
        Cached,
        Cancelled,
    };

    /** @brief Optional navigable origin for one diagnostic. */
    struct DiagnosticSourceLocation {
        std::string absolutePath;
        std::uint32_t line{};
        std::uint32_t column{};
    };

    /** @brief Immutable typed build output entry. */
    struct BuildOutputRecord {
        std::uint64_t sequence{};
        std::chrono::system_clock::time_point timestampUtc;
        BuildOutputStatus status{BuildOutputStatus::Info};
        std::string phase;
        std::string message;
        std::optional<DiagnosticSourceLocation> source;
        std::optional<OperationId> operationId;
    };

    /** @brief Owned bounded build-output projection. */
    struct BuildOutputSnapshot {
        std::uint64_t revision{};
        std::uint64_t droppedRecordCount{};
        std::size_t capacity{};
        std::vector<BuildOutputRecord> records;
    };

    /** @brief Read-only build-output capability exposed to presentation surfaces. */
    class IBuildOutputQuery {
    public:
        virtual ~IBuildOutputQuery() = default;
        /** @brief Returns a new owned snapshot only when the store revision changed. */
        [[nodiscard]] virtual std::optional<BuildOutputSnapshot> SnapshotIfChanged(std::uint64_t knownRevision) const = 0;
    };

    /** @brief Fixed-capacity build diagnostic store with overwrite-oldest retention. */
    class BuildOutputStore final : public IBuildOutputQuery {
    public:
        /** @brief Creates an empty store; zero capacity is clamped to one. */
        explicit BuildOutputStore(std::size_t capacity);

        /** @brief Appends one typed output entry and assigns its sequence. */
        void Append(BuildOutputRecord record);

        /** @copydoc IBuildOutputQuery::SnapshotIfChanged */
        [[nodiscard]] std::optional<BuildOutputSnapshot> SnapshotIfChanged(std::uint64_t knownRevision) const override;

    private:
        const std::size_t capacity_;
        mutable std::mutex mutex_;
        std::deque<BuildOutputRecord> records_;
        std::uint64_t nextSequence_{1};
        std::uint64_t revision_{};
        std::uint64_t droppedRecordCount_{};
    };
}  // namespace Horo
