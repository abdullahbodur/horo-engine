#pragma once

/** @file ProtocolIdentityRegistry.h @brief Bounded immutable protocol identity registry. */

#include "Horo/Network/ProtocolIdentity.h"

#include <cstddef>
#include <optional>
#include <span>
#include <vector>

namespace Horo::Network {
    /** @brief Declares one stable protocol identity and its supported same-major version interval. */
    struct ProtocolIdentityDescriptor final {
        ProtocolId id;                 /**< Stable wire identity. */
        ProtocolVersionRange versions; /**< Locally supported closed version interval. */
        constexpr auto operator<=>(const ProtocolIdentityDescriptor &) const noexcept = default;
    };

    /** @brief Declares one stable message identity and its independently versioned schema. */
    struct MessageIdentityDescriptor final {
        ProtocolId protocol;          /**< Owning protocol identity. */
        MessageTypeId id;             /**< Stable message-type wire identity. */
        MessageSchemaId schema;       /**< Stable message-schema wire identity. */
        MessageSchemaVersion version; /**< Exact schema version emitted locally. */
        bool optional{};              /**< Whether an unknown peer may safely omit this message. */
        constexpr auto operator<=>(const MessageIdentityDescriptor &) const noexcept = default;
    };

    /** @brief Declares one protocol feature and the first version that can negotiate it. */
    struct FeatureIdentityDescriptor final {
        ProtocolId protocol;        /**< Owning protocol identity. */
        ProtocolFeatureId id;       /**< Stable feature wire identity. */
        ProtocolVersion introduced; /**< First compatible protocol version. */
        constexpr auto operator<=>(const FeatureIdentityDescriptor &) const noexcept = default;
    };

    /** @brief Declares one stable terminal reason and its safe local classification. */
    struct CloseReasonIdentityDescriptor final {
        ProtocolId protocol;                           /**< Owning protocol identity. */
        CloseReasonId id;                              /**< Stable close-reason wire identity. */
        CloseReasonKind kind{CloseReasonKind::Normal}; /**< Bounded local terminal classification. */
        constexpr auto operator<=>(const CloseReasonIdentityDescriptor &) const noexcept = default;
    };

    /** @brief Hard admission bounds applied before registry-owned copies are allocated. */
    struct ProtocolIdentityRegistryLimits final {
        std::size_t maximumProtocols{64};
        std::size_t maximumMessages{1024};
        std::size_t maximumFeatures{256};
        std::size_t maximumCloseReasons{256};
    };

    /** @brief Borrowed descriptor input copied synchronously by Create; spans need not outlive the call. */
    struct ProtocolIdentityContributions final {
        std::span<const ProtocolIdentityDescriptor> protocols;
        std::span<const MessageIdentityDescriptor> messages;
        std::span<const FeatureIdentityDescriptor> features;
        std::span<const CloseReasonIdentityDescriptor> closeReasons;
    };

    /** @brief Owned canonical registry snapshot with exact value-returning lookups. */
    class ProtocolIdentityRegistry final {
    public:
        /**
         * @brief Copies and validates inert contributions transactionally.
         * @param contributions Borrowed input whose backing storage must remain valid for this call only.
         * @param limits Non-zero hard bounds checked before allocating registry storage.
         * @return An immutable owned canonical snapshot or a typed validation/capacity error.
         */
        [[nodiscard]] static Result<ProtocolIdentityRegistry> Create(const ProtocolIdentityContributions &contributions,
                                                                     const ProtocolIdentityRegistryLimits &limits = {});
        /** @brief Looks up a protocol without exposing registry storage. @param id Stable protocol ID. @return Owned descriptor when known.
         */
        [[nodiscard]] std::optional<ProtocolIdentityDescriptor> FindProtocol(ProtocolId id) const noexcept;
        /** @brief Looks up a message without exposing registry storage. @param protocol Owning protocol. @param id Message ID. @return
         * Owned descriptor when known. */
        [[nodiscard]] std::optional<MessageIdentityDescriptor> FindMessage(ProtocolId protocol, MessageTypeId id) const noexcept;
        /** @brief Looks up a feature without exposing registry storage. @param protocol Owning protocol. @param id Feature ID. @return
         * Owned descriptor when known. */
        [[nodiscard]] std::optional<FeatureIdentityDescriptor> FindFeature(ProtocolId protocol, ProtocolFeatureId id) const noexcept;
        /** @brief Looks up a close reason without exposing registry storage. @param protocol Owning protocol. @param id Reason ID. @return
         * Owned descriptor when known. */
        [[nodiscard]] std::optional<CloseReasonIdentityDescriptor> FindCloseReason(ProtocolId protocol, CloseReasonId id) const noexcept;
        /** @brief Selects the highest exact mutual version without cross-major fallback. @param protocol Protocol to negotiate. @param peer
         * Peer's supported interval. @return Selected version or typed unknown/incompatible error. */
        [[nodiscard]] Result<ProtocolVersion> SelectVersion(ProtocolId protocol, const ProtocolVersionRange &peer) const;

    private:
        std::vector<ProtocolIdentityDescriptor> protocols_;
        std::vector<MessageIdentityDescriptor> messages_;
        std::vector<FeatureIdentityDescriptor> features_;
        std::vector<CloseReasonIdentityDescriptor> closeReasons_;
    };
}  // namespace Horo::Network
