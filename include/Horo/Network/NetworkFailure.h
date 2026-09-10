#pragma once

/**
 * @file NetworkFailure.h
 * @brief Canonical bounded network terminal failures and generation-safe publication.
 */

#include "Horo/Network/NetworkHandles.h"
#include "Horo/Network/NetworkObjectIdentity.h"
#include "Horo/Network/ProtocolIdentity.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <variant>

namespace Horo::Network {
    /** @brief Maximum safe typed fields retained by one network terminal record. */
    inline constexpr std::size_t MaximumNetworkFailureContextEntries = 8;
    /** @brief Maximum private backend bytes inspected before all original text is discarded. */
    inline constexpr std::size_t MaximumPrivateBackendDetailBytes = 256;

    /** @brief Owning network layer for one terminal outcome. */
    enum class NetworkFailureLayer : std::uint8_t {
        Transport,
        Protocol,
        Session,
        Replication,
        GameplayDispatch,
        Count
    };

    /** @brief Closed recovery decision used without parsing diagnostic text. */
    enum class NetworkFailureDisposition : std::uint8_t {
        Retryable,
        LocalPolicy,
        RemoteRejection,
        Incompatible,
        Fatal,
        Count
    };

    /** @brief Stable typed terminal kind mapped explicitly to one existing or declared network error code. */
    enum class NetworkFailureKind : std::uint8_t {
        NameResolutionFailed,
        TransportUnavailable,
        TransportSaturated,
        ProtocolMalformed,
        ProtocolIncompatible,
        SessionLocalPolicyRejected,
        SessionRemoteRejected,
        SessionCancelled,
        SessionTimedOut,
        SessionShutdown,
        ReplicationIncompatible,
        GameplayDispatchRejected,
        FatalInternal,
        Count
    };

    /** @brief Closed typed vocabulary for safe bounded public diagnostic context. */
    enum class NetworkFailureContextKey : std::uint8_t {
        Connection,
        Protocol,
        CloseReason,
        NetworkObject,
        SessionGeneration,
        SchemaVersion,
        MessageType,
        RequestedBytes,
        Capacity,
        Attempt,
        Count
    };

    /** @brief Closed value set accepted by public network failure context fields. */
    using NetworkFailureContextValue =
        std::variant<std::uint64_t, TransportHandleDiagnostic, ProtocolId, CloseReasonId, MessageTypeId, NetworkObjectId>;

    /** @brief One safe typed context field; records require strictly increasing keys. */
    struct NetworkFailureContextEntry final {
        NetworkFailureContextKey key{NetworkFailureContextKey::Connection}; /**< Stable context field identity. */
        NetworkFailureContextValue value;                                   /**< Typed field value required by key. */
    };

    /** @brief Bounded proof that private backend detail was discarded before publication. */
    struct NetworkBackendEvidenceSummary final {
        std::uint16_t observedBytes{}; /**< Saturates at MaximumPrivateBackendDetailBytes + 1. */
        bool observed{};               /**< Whether the private adapter supplied detail. */
        bool truncated{};              /**< Whether input exceeded the inspection bound. */
        bool malformed{};              /**< Whether inspected bytes were not safe printable UTF-8. */

        constexpr auto operator<=>(const NetworkBackendEvidenceSummary &) const noexcept = default;
    };

    /** @brief Immutable canonical terminal record with no raw backend text, payload, credential, or native identifier. */
    class NetworkTerminalRecord final {
    public:
        /** @brief Returns the owning layer. @return Canonical failure layer. */
        [[nodiscard]] constexpr NetworkFailureLayer Layer() const noexcept {
            return layer_;
        }

        /** @brief Returns the stable terminal kind. @return Canonical terminal kind. */
        [[nodiscard]] constexpr NetworkFailureKind Kind() const noexcept {
            return kind_;
        }

        /** @brief Returns the typed recovery decision. @return Canonical disposition. */
        [[nodiscard]] constexpr NetworkFailureDisposition Disposition() const noexcept {
            return disposition_;
        }

        /** @brief Returns the canonical stable descriptor. @return Process-lifetime network descriptor. */
        [[nodiscard]] const ErrorCodeDescriptor &Descriptor() const noexcept;
        /** @brief Creates the canonical error for adapter presentation. @return Typed error with no private backend detail. */
        [[nodiscard]] Error ToError() const;

        /** @brief Returns immutable safe context. @return Borrowed fields owned by this record. */
        [[nodiscard]] std::span<const NetworkFailureContextEntry> Context() const noexcept {
            return {context_.data(), contextCount_};
        }

        /** @brief Returns bounded proof about discarded private detail. @return Safe evidence summary. */
        [[nodiscard]] constexpr NetworkBackendEvidenceSummary BackendEvidence() const noexcept {
            return backendEvidence_;
        }

    private:
        friend Result<NetworkTerminalRecord> MakeNetworkTerminalRecord(NetworkFailureLayer, NetworkFailureKind,
                                                                       std::span<const NetworkFailureContextEntry>);
        friend Result<NetworkTerminalRecord> NormalizePrivateBackendFailure(NetworkFailureLayer, NetworkFailureKind, std::string_view,
                                                                            std::span<const NetworkFailureContextEntry>, bool);

        NetworkTerminalRecord() = default;

        NetworkFailureLayer layer_{NetworkFailureLayer::Transport};
        NetworkFailureKind kind_{NetworkFailureKind::NameResolutionFailed};
        NetworkFailureDisposition disposition_{NetworkFailureDisposition::Retryable};
        std::array<NetworkFailureContextEntry, MaximumNetworkFailureContextEntries> context_{};
        std::size_t contextCount_{};
        NetworkBackendEvidenceSummary backendEvidence_{};
    };

    /**
     * @brief Builds a canonical terminal result from typed Horo evidence.
     * @param layer Owning failure layer.
     * @param kind Stable terminal kind valid for that layer.
     * @param context Safe typed fields in strictly increasing key order.
     * @return Immutable record or a typed malformed/capacity failure.
     */
    [[nodiscard]] Result<NetworkTerminalRecord> MakeNetworkTerminalRecord(NetworkFailureLayer layer, NetworkFailureKind kind,
                                                                          std::span<const NetworkFailureContextEntry> context = {});

    /**
     * @brief Normalizes borrowed private-adapter text without retaining or exposing any byte of it.
     * @param layer Owning failure layer.
     * @param kind Stable terminal kind valid for that layer.
     * @param privateDetail Borrowed native/backend detail inspected only for bounded evidence flags.
     * @param context Safe typed public context.
     * @param instrumentationEnabled Whether evidence flags may be populated; terminal semantics never change.
     * @return Canonical record or a typed malformed/capacity failure.
     */
    [[nodiscard]] Result<NetworkTerminalRecord> NormalizePrivateBackendFailure(NetworkFailureLayer layer, NetworkFailureKind kind,
                                                                               std::string_view privateDetail,
                                                                               std::span<const NetworkFailureContextEntry> context = {},
                                                                               bool instrumentationEnabled = true);

    /** @brief Owner-thread exactly-once terminal slot fenced by a transport handle generation. */
    class NetworkTerminalOwner final {
    public:
        NetworkTerminalOwner() = delete;
        /** @brief Creates an accepting slot. @param connection Exact current connection. @return Owner or invalid-handle failure. */
        [[nodiscard]] static Result<NetworkTerminalOwner> Create(ConnectionHandle connection);
        /**
         * @brief Publishes the first terminal result for the exact current generation.
         * @param observed Generation carried by the completion/callback.
         * @param terminal Immutable candidate terminal record.
         * @return Success, or deterministic stale/already-resolved failure without mutation.
         */
        [[nodiscard]] Result<void> Resolve(ConnectionHandle observed, const NetworkTerminalRecord &terminal);
        /**
         * @brief Reopens the slot for the exact next handle generation after terminal observation.
         * @param retired Exact terminal generation being replaced.
         * @param replacement Same slot with the next non-zero generation.
         * @return Success or typed stale/exhaustion failure preserving current state.
         */
        [[nodiscard]] Result<void> Replace(ConnectionHandle retired, ConnectionHandle replacement);

        /** @brief Returns the exact active connection. @return Current generation-tagged handle. */
        [[nodiscard]] constexpr ConnectionHandle Connection() const noexcept {
            return connection_;
        }

        /** @brief Reports whether this owner can accept a terminal result. @return True before resolution. */
        [[nodiscard]] constexpr bool IsAccepting() const noexcept {
            return accepting_;
        }

        /** @brief Returns immutable terminal evidence. @return Borrowed record or nullptr while accepting. */
        [[nodiscard]] const NetworkTerminalRecord *Terminal() const noexcept;

    private:
        explicit NetworkTerminalOwner(ConnectionHandle connection) : connection_(connection) {}

        ConnectionHandle connection_{};
        std::optional<NetworkTerminalRecord> terminal_;
        bool accepting_{true};
    };
}  // namespace Horo::Network
