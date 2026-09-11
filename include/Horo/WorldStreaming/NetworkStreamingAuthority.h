#pragma once

/**
 * @file NetworkStreamingAuthority.h
 * @brief Bounded server-intent and client-local residency readiness contract.
 */

#include "Horo/WorldStreaming/StreamingCellState.h"

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace Horo::WorldStreaming {
    namespace Detail {
        /** @brief Tag separating one admitted peer session from other runtime owners. */
        struct NetworkStreamingPeerSessionIdTag;
        /** @brief Tag separating the peer-global command sequence from local residency generations. */
        struct NetworkStreamingCommandSequenceTag;
    }  // namespace Detail

    /** @brief Stable identity for one admitted peer-session lifetime. */
    using NetworkStreamingPeerSessionId =
        Foundation::Detail::NonZeroId64<Detail::NetworkStreamingPeerSessionIdTag, WorldStreamingErrors::IdentityInvalid>;
    /** @brief Strictly increasing command sequence scoped to one peer-session lifetime. */
    using NetworkStreamingCommandSequence =
        Foundation::Detail::NonZeroId64<Detail::NetworkStreamingCommandSequenceTag, WorldStreamingErrors::IdentityInvalid>;

    /**
     * @brief Advances one peer-session command sequence without wrapping.
     * @param current Current valid peer-global sequence.
     * @return Exact successor or GenerationExhausted at the uint64 boundary.
     */
    [[nodiscard]] Result<NetworkStreamingCommandSequence> NextNetworkStreamingCommandSequence(NetworkStreamingCommandSequence current);

    /** @brief Server relevance intent; it is never direct permission to mutate client residency. */
    enum class NetworkStreamingServerIntent : std::uint8_t {
        Release,
        RequireLoaded,
        RequireActive,
    };

    /** @brief Client-owned outcome reported for the exact current server intent. */
    enum class NetworkStreamingClientReadiness : std::uint8_t {
        Pending,
        Ready,
        Unavailable,
        Failed,
    };

    /** @brief Exact server command identity in a peer-global ordered stream. */
    struct NetworkStreamingIntentCommand final {
        NetworkStreamingPeerSessionId session;    /**< Exact admitted peer-session lifetime. */
        NetworkStreamingCommandSequence sequence; /**< Peer-global sequence; first is one and successors are exact. */
        WorldPartitionId partition;               /**< Stable remote and local dataset identity; no remote epoch is accepted. */
        StreamingCellId cell;                     /**< Canonical persistent cell tuple. */
        NetworkStreamingServerIntent intent{};    /**< Relevance requirement, not a local residency command. */

        /** @brief Checks value representation and supported intent. @return Whether every command field is usable. */
        [[nodiscard]] bool IsValid() const noexcept;
        [[nodiscard]] constexpr auto operator<=>(const NetworkStreamingIntentCommand &) const noexcept = default;
    };

    /** @brief Client result for one exact current command revision. */
    struct NetworkStreamingReadinessReport final {
        NetworkStreamingPeerSessionId session;        /**< Exact admitted peer-session lifetime. */
        NetworkStreamingCommandSequence sequence;     /**< Exact current command sequence for the cell. */
        WorldPartitionId partition;                   /**< Stable dataset identity. */
        StreamingCellId cell;                         /**< Exact reported cell. */
        NetworkStreamingClientReadiness readiness{};  /**< Client-owned terminal readiness result. */
        std::optional<StreamingFence> localFence;     /**< Required only for Ready; always uses the local epoch/generation. */
        std::optional<StreamingCellState> localState; /**< Required only for Ready and must satisfy the server intent. */
    };

    /** @brief Immutable current command and client-owned response for one relevant cell. */
    struct NetworkStreamingAuthorityRecord final {
        NetworkStreamingIntentCommand command; /**< Exact current server intent for this cell. */
        NetworkStreamingClientReadiness readiness{NetworkStreamingClientReadiness::Pending}; /**< Client-owned response. */
        std::optional<StreamingFence> localFence;     /**< Client-local proof retained only for Ready. */
        std::optional<StreamingCellState> localState; /**< Satisfying local residency retained only for Ready. */

        [[nodiscard]] constexpr auto operator<=>(const NetworkStreamingAuthorityRecord &) const noexcept = default;
    };

    /** @brief Bounded construction facts for one peer against one mounted client partition. */
    struct NetworkStreamingAuthorityConfig final {
        /** @brief Hard implementation ceiling for one peer's current relevant-cell snapshot. */
        static constexpr std::uint32_t MaximumTrackedCells = 1024;

        NetworkStreamingPeerSessionId session; /**< Exact peer-session sequence namespace. */
        StreamingRuntimeOwnerToken localOwner; /**< Client-local partition, epoch and residency owner. */
        std::uint32_t maximumTrackedCells{};   /**< Positive ceiling for the immutable current snapshot. */

        /** @brief Checks identities and bounded capacity. @return Whether the config is usable. */
        [[nodiscard]] bool IsValid() const noexcept;
    };

    /** @brief Admission lifecycle; cancellation preserves the snapshot until terminal shutdown. */
    enum class NetworkStreamingAuthorityLifecycle : std::uint8_t {
        Active,
        Cancelling,
        Closed,
    };

    /**
     * @brief Client-owned bounded projection of server relevance intent and local readiness.
     * @details This owner never mutates the cell ledger, scheduler, transport, replication state or Scene. The client separately
     *          admits local residency and reports exact local fence evidence after observing its own authority snapshot.
     */
    class NetworkStreamingAuthority final {
    public:
        NetworkStreamingAuthority(const NetworkStreamingAuthority &) = delete;
        NetworkStreamingAuthority &operator=(const NetworkStreamingAuthority &) = delete;
        NetworkStreamingAuthority(NetworkStreamingAuthority &&other) noexcept;
        NetworkStreamingAuthority &operator=(NetworkStreamingAuthority &&) = delete;

        /**
         * @brief Creates an empty active authority projection with preallocated snapshot capacity.
         * @param config Exact peer, local owner and mandatory capacity facts.
         * @return Active owner or a typed invalid/capacity result.
         */
        [[nodiscard]] static Result<NetworkStreamingAuthority> Create(const NetworkStreamingAuthorityConfig &config);

        /**
         * @brief Applies the next peer-global server intent without mutating local residency.
         * @param command Exact next command. Sequence one starts a session and every later command is its exact successor.
         * @return Current record, an empty value for Release, or typed invalid/unsupported/stale/capacity/lifecycle failure.
         * @post Failure leaves the sequence and complete snapshot unchanged.
         */
        [[nodiscard]] Result<std::optional<NetworkStreamingAuthorityRecord>> ApplyServerIntent(
            const NetworkStreamingIntentCommand &command);

        /**
         * @brief Publishes client-owned readiness for one exact current intent.
         * @param localOwner Exact current local partition authority lifetime.
         * @param report Exact report; Ready requires a matching local fence/state proof.
         * @return Updated record or typed invalid/unsupported/stale/lifecycle failure.
         * @post Failure leaves the current record unchanged.
         */
        [[nodiscard]] Result<NetworkStreamingAuthorityRecord> ApplyClientReadiness(const StreamingRuntimeOwnerToken &localOwner,
                                                                                   const NetworkStreamingReadinessReport &report);

        /**
         * @brief Closes new server-intent admission while retaining the observable snapshot.
         * @param localOwner Exact current local owner lifetime.
         * @return Success including idempotent cancellation, or typed invalid/stale/lifecycle failure.
         */
        [[nodiscard]] Result<void> RequestCancellation(const StreamingRuntimeOwnerToken &localOwner) noexcept;

        /**
         * @brief Terminates the projection and discards protocol facts without claiming local residency retirement.
         * @param localOwner Exact current local owner lifetime.
         * @return Success including idempotent shutdown, or typed invalid/stale failure.
         */
        [[nodiscard]] Result<void> Shutdown(const StreamingRuntimeOwnerToken &localOwner) noexcept;

        /** @brief Returns current relevant-cell records in canonical cell order. @return Borrow valid until mutation. */
        [[nodiscard]] std::span<const NetworkStreamingAuthorityRecord> Snapshot() const noexcept;
        /** @brief Returns the last admitted peer-global command sequence. @return Empty before the first command. */
        [[nodiscard]] std::optional<NetworkStreamingCommandSequence> LastSequence() const noexcept;
        /** @brief Returns the lifecycle gate. @return Active, Cancelling or Closed. */
        [[nodiscard]] NetworkStreamingAuthorityLifecycle Lifecycle() const noexcept;

    private:
        NetworkStreamingAuthority(NetworkStreamingAuthorityConfig config, std::vector<NetworkStreamingAuthorityRecord> records) noexcept;

        NetworkStreamingAuthorityConfig config_;
        std::vector<NetworkStreamingAuthorityRecord> records_;
        std::optional<NetworkStreamingCommandSequence> lastSequence_;
        NetworkStreamingAuthorityLifecycle lifecycle_{NetworkStreamingAuthorityLifecycle::Active};
    };
}  // namespace Horo::WorldStreaming
