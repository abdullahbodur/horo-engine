#pragma once

#include "Horo/Foundation/Diagnostics.h"
#include "Horo/Foundation/OperationStore.h"

/**
 * @file BuildOutputStore.h
 * @brief Bounded thread-safe typed build diagnostics and source locations.
 */

#include <chrono>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace Horo {
    /** @brief Store-issued non-zero identity for one producer-owned build-output session. */
    class BuildOutputSessionId final {
    public:
        /** @brief Constructs the reserved invalid identity. */
        constexpr BuildOutputSessionId() = default;

        /** @brief Returns whether this identity was issued by a build-output store. */
        [[nodiscard]] constexpr bool IsValid() const noexcept {
            return value_ != 0U;
        }

        /** @brief Returns the store-issued value, or zero for the invalid identity. */
        [[nodiscard]] constexpr std::uint64_t Value() const noexcept {
            return value_;
        }

        [[nodiscard]] constexpr auto operator<=>(const BuildOutputSessionId &) const noexcept = default;

    private:
        friend class BuildOutputStore;

        explicit constexpr BuildOutputSessionId(const std::uint64_t value) noexcept : value_(value) {}

        std::uint64_t value_{};
    };

    /** @brief Terminal result for the independently correlated scope represented by a record. */
    enum class BuildOutputResult : std::uint8_t {
        None,
        Succeeded,
        Failed,
        Cached,
        Cancelled,
        TimedOut,
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
        std::optional<BuildOutputSessionId> sessionId;
        std::optional<OperationId> operationId;
        DiagnosticSeverity severity{DiagnosticSeverity::Note};
        BuildOutputResult result{BuildOutputResult::None};
        std::string stage;
        DiagnosticCode code;
        std::string message;
        std::optional<DiagnosticSourceLocation> source;
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
        /**
         * @brief Returns a new owned snapshot only when the store revision changed.
         * @param knownRevision Last revision already observed by the caller.
         * @return An owned immutable-by-convention copy, or no value when unchanged.
         */
        [[nodiscard]] virtual std::optional<BuildOutputSnapshot> SnapshotIfChanged(std::uint64_t knownRevision) const = 0;
    };

    /**
     * @brief Fixed-capacity build diagnostic store with overwrite-oldest retention.
     * @details The process or project-session composition root owns this store. Producers
     *          may append from any thread while it is alive; presentation clients borrow
     *          only IBuildOutputQuery and own every returned snapshot. The owner must stop
     *          all producers and detach query clients before destroying the store.
     */
    class BuildOutputStore final : public IBuildOutputQuery {
    public:
        /**
         * @brief Creates an empty store; zero capacity is clamped to one.
         * @param capacity Maximum retained record count.
         */
        explicit BuildOutputStore(std::size_t capacity);

        /**
         * @brief Allocates one deterministic non-zero session identity.
         * @return A process-local identity, or no value after the 64-bit space is exhausted.
         * @details Allocation is thread-safe and monotonically increasing for this store's lifetime.
         */
        [[nodiscard]] std::optional<BuildOutputSessionId> BeginSession();

        /**
         * @brief Appends one immutable producer value and assigns its sequence.
         * @param record Owned record copied into the bounded store.
         */
        void Append(BuildOutputRecord record);

        /** @copydoc IBuildOutputQuery::SnapshotIfChanged */
        [[nodiscard]] std::optional<BuildOutputSnapshot> SnapshotIfChanged(std::uint64_t knownRevision) const override;

    private:
        const std::size_t capacity_;
        mutable std::mutex mutex_;
        std::deque<BuildOutputRecord> records_;
        std::uint64_t nextSessionValue_{1};
        std::uint64_t nextSequence_{1};
        std::uint64_t revision_{};
        std::uint64_t droppedRecordCount_{};
    };
}  // namespace Horo
