#pragma once

/**
 * @file TransportCapabilities.h
 * @brief Backend-neutral delivery capability, requirement, negotiation, and admission contracts.
 */

#include "Horo/Foundation/Result.h"
#include "Horo/Network/NetworkHandles.h"

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>

namespace Horo::Network {
    /** @brief Exact message delivery semantics; values are never ordered by strength or used for fallback. */
    enum class DeliveryPolicy : std::uint8_t {
        UnreliableUnordered,
        UnreliableSequenced,
        ReliableOrdered,
        ReliableUnordered,
        Count
    };

    /** @brief Permanent lack, temporary lack, and currently usable support remain distinct evidence. */
    enum class TransportSupport : std::uint8_t {
        Unknown,
        Unsupported,
        Unavailable,
        Available,
        Count
    };

    /** @brief Whether a per-message deadline is absent, negotiable, or mandatory. */
    enum class DeadlineRequirement : std::uint8_t {
        None,
        Optional,
        Required,
        Count
    };

    /**
     * @brief Immutable capability evidence for one explicit transport candidate.
     *
     * Revision is non-zero and changes whenever support or limits change. Delivery policies are exact
     * alternatives, not an ordered quality scale. Positive channel/message limits accompany any available
     * delivery mode. Deadline support carries a positive maximum only while Available. No backend or native
     * value is named, and this evidence does not select, initialize, or activate a transport.
     */
    struct TransportCapabilities final {
        std::uint32_t contractVersion{1}; /**< Closed Horo descriptor contract version. */
        std::uint64_t revision{};         /**< Non-zero exact evidence revision. */
        std::array<TransportSupport, static_cast<std::size_t>(DeliveryPolicy::Count)> delivery{}; /**< Per-mode support. */
        std::uint32_t maximumChannels{};                           /**< Channel count, or zero when no delivery is available. */
        std::uint64_t maximumMessageBytes{};                       /**< Inclusive byte ceiling, or zero when no delivery is available. */
        TransportSupport deadlines{TransportSupport::Unsupported}; /**< Per-message deadline feature support. */
        std::uint32_t maximumDeadlineMilliseconds{};               /**< Inclusive deadline ceiling only when available. */

        constexpr auto operator<=>(const TransportCapabilities &) const noexcept = default;
    };

    /** @brief Bounded session-level requirements compared against one explicit candidate without fallback. */
    struct TransportRequirements final {
        std::array<bool, static_cast<std::size_t>(DeliveryPolicy::Count)> requiredDelivery{}; /**< Exact required modes. */
        std::uint32_t requiredChannels{};                                                     /**< Positive session channel count. */
        std::uint64_t requiredMaximumMessageBytes{};             /**< Positive inclusive session byte ceiling. */
        DeadlineRequirement deadline{DeadlineRequirement::None}; /**< Whether deadline support may be omitted. */
        std::uint32_t requiredMaximumDeadlineMilliseconds{};     /**< Positive only when deadline is requested. */

        constexpr auto operator<=>(const TransportRequirements &) const noexcept = default;
    };

    /** @brief Owned immutable result of exact candidate negotiation, suitable for later send admission. */
    struct TransportSelectionEvidence final {
        std::uint64_t capabilityRevision{}; /**< Exact candidate revision that produced this evidence. */
        std::array<bool, static_cast<std::size_t>(DeliveryPolicy::Count)> admittedDelivery{}; /**< Exact admitted modes. */
        std::uint32_t channelCount{};                                                         /**< Admitted zero-based channel count. */
        std::uint64_t maximumMessageBytes{};         /**< Admitted inclusive per-message byte ceiling. */
        bool deadlinesEnabled{};                     /**< Whether deadline support was explicitly admitted. */
        std::uint32_t maximumDeadlineMilliseconds{}; /**< Admitted ceiling, or zero when disabled. */

        constexpr auto operator<=>(const TransportSelectionEvidence &) const noexcept = default;
    };

    /** @brief One bounded send request validated before any payload copy or queue mutation. */
    struct TransportSendRequirement final {
        DeliveryPolicy delivery{DeliveryPolicy::ReliableOrdered}; /**< Exact mode; no substitution. */
        ChannelId channel{};                                      /**< Exact zero-based negotiated channel. */
        std::uint64_t payloadBytes{};                             /**< Includes valid zero-length messages. */
        std::uint32_t deadlineMilliseconds{};                     /**< Zero disables the per-message deadline. */

        constexpr auto operator<=>(const TransportSendRequirement &) const noexcept = default;
    };

    /**
     * @brief Validates complete immutable capability evidence without probing a backend.
     * @param capabilities Candidate-owned Horo capability descriptor.
     * @return True only for coherent version-one evidence with finite usable limits.
     */
    [[nodiscard]] bool ValidateTransportCapabilities(const TransportCapabilities &capabilities) noexcept;

    /**
     * @brief Compares exact bounded requirements against one explicitly chosen transport candidate.
     * @param capabilities Immutable candidate evidence captured by the caller.
     * @param expectedRevision Exact non-zero revision retained for this comparison.
     * @param requirements Required delivery modes, finite limits, and deadline policy.
     * @return Owned admitted evidence, or a typed malformed, stale, unavailable, unsupported, or limit error.
     * @post Reliability, ordering, channel, and message-size requirements are never weakened. Only an explicitly
     * optional deadline may be disabled. The function performs no backend selection, initialization, or mutation.
     */
    [[nodiscard]] Result<TransportSelectionEvidence> ResolveTransportCapabilities(const TransportCapabilities &capabilities,
                                                                                  std::uint64_t expectedRevision,
                                                                                  const TransportRequirements &requirements);

    /**
     * @brief Validates one send against owned negotiated evidence before queue admission.
     * @param selection Owned evidence returned by ResolveTransportCapabilities.
     * @param expectedRevision Exact capability revision retained by the caller.
     * @param requirement Exact delivery, channel, payload size, and optional deadline for this send.
     * @param state Pure caller-supplied cancellation/shutdown state; this function owns no live session lifecycle.
     * @return Success or a typed malformed, stale, cancelled, shutdown, unsupported, or limit error.
     * @post Success performs no allocation, payload copy, callback, backend call, or queue mutation.
     */
    [[nodiscard]] Result<void> AdmitTransportSend(const TransportSelectionEvidence &selection, std::uint64_t expectedRevision,
                                                  const TransportSendRequirement &requirement, TransportAdmissionState state);
}  // namespace Horo::Network
