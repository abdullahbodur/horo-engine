#pragma once

/**
 * @file NetworkErrors.h
 * @brief Stable backend-neutral network error identities.
 */

#include "Horo/Foundation/ErrorCode.h"

namespace Horo::Network::NetworkErrors {
    /** @brief Endpoint text is malformed or has an ambiguous non-canonical representation. */
    extern const ErrorCodeDescriptor NetworkAddressInvalid;
    /** @brief Endpoint text uses a deliberately unsupported scheme, zone, or IDNA representation. */
    extern const ErrorCodeDescriptor NetworkAddressUnsupported;
    /** @brief Endpoint text exceeds the finite public address bound. */
    extern const ErrorCodeDescriptor NetworkAddressCapacityExceeded;
    /** @brief A transport handle is malformed, stale, or does not match the current owner generation. */
    extern const ErrorCodeDescriptor TransportHandleInvalid;
    /** @brief A reclaimed handle slot cannot advance its generation without wrapping. */
    extern const ErrorCodeDescriptor TransportGenerationExhausted;
    /** @brief Packet pool descriptor is malformed. */
    extern const ErrorCodeDescriptor PacketBufferInvalid;
    /** @brief Packet bytes or prepared storage exceed finite capacity. */
    extern const ErrorCodeDescriptor PacketBufferCapacityExceeded;
    /** @brief Every prepared packet slot is leased. */
    extern const ErrorCodeDescriptor PacketBufferPoolExhausted;
    /** @brief Queue descriptor or packet metadata is malformed. */
    extern const ErrorCodeDescriptor PacketQueueInvalid;
    /** @brief A packet cannot fit the queue's declared byte bounds. */
    extern const ErrorCodeDescriptor PacketQueueCapacityExceeded;
    /** @brief Explicit reject/replace policy could not admit a packet. */
    extern const ErrorCodeDescriptor PacketQueueFull;
    /** @brief A stable network identity uses its reserved zero representation. */
    extern const ErrorCodeDescriptor IdentityInvalid;
    /** @brief Replication descriptor metadata is malformed or exceeds its declared bounds. */
    extern const ErrorCodeDescriptor ReplicationDescriptorInvalid;
    /** @brief Schema, field, or tombstone identities collide in one candidate snapshot. */
    extern const ErrorCodeDescriptor ReplicationDescriptorConflict;
    /** @brief A replacement reuses stable identity with incompatible semantics. */
    extern const ErrorCodeDescriptor ReplicationDescriptorIncompatible;
    /** @brief An exact replication schema identity is absent from the pinned snapshot. */
    extern const ErrorCodeDescriptor ReplicationSchemaUnknown;
    /** @brief Descriptor snapshot construction exceeded its explicit finite capacity. */
    extern const ErrorCodeDescriptor ReplicationCapacityExceeded;
    /** @brief Transport capability evidence or a bounded requirement is malformed. */
    extern const ErrorCodeDescriptor TransportCapabilityDescriptorInvalid;
    /** @brief Transport capability evidence changed after the caller captured its revision. */
    extern const ErrorCodeDescriptor TransportCapabilityStale;
    /** @brief The explicit transport candidate cannot provide an exact required delivery semantic. */
    extern const ErrorCodeDescriptor TransportDeliveryUnsupported;
    /** @brief Known transport functionality is not currently available. */
    extern const ErrorCodeDescriptor TransportCapabilityUnavailable;
    /** @brief A channel, message, or deadline requirement exceeds the candidate's finite limits. */
    extern const ErrorCodeDescriptor TransportLimitExceeded;
    /** @brief Caller-owned cancellation rejected transport admission before queue mutation. */
    extern const ErrorCodeDescriptor TransportOperationCancelled;
    /** @brief Caller-owned shutdown state rejected transport admission before queue mutation. */
    extern const ErrorCodeDescriptor TransportShuttingDown;
    /** @brief Protocol identity contributions or version ranges are malformed. */
    extern const ErrorCodeDescriptor ProtocolIdentityDescriptorInvalid;
    /** @brief A protocol-scoped stable identity is registered more than once. */
    extern const ErrorCodeDescriptor ProtocolIdentityConflict;
    /** @brief An exact protocol-scoped identity is absent from the immutable registry. */
    extern const ErrorCodeDescriptor ProtocolIdentityUnknown;
    /** @brief Protocol version ranges have no explicit compatible overlap. */
    extern const ErrorCodeDescriptor ProtocolVersionIncompatible;
    /** @brief Protocol identity registry construction exceeded an explicit finite bound. */
    extern const ErrorCodeDescriptor ProtocolIdentityCapacityExceeded;
    /** @brief Message framing or codec metadata is malformed or non-canonical. */
    extern const ErrorCodeDescriptor MessageEnvelopeInvalid;
    /** @brief Declared message framing exceeds an explicit finite bound. */
    extern const ErrorCodeDescriptor MessageEnvelopeCapacityExceeded;
    /** @brief Message codec or field metadata conflicts with another stable identity. */
    extern const ErrorCodeDescriptor MessageCodecConflict;
    /** @brief No exact inert codec metadata exists for the requested message. */
    extern const ErrorCodeDescriptor MessageCodecUnknown;
    /** @brief The payload schema identity or version is incompatible with codec metadata. */
    extern const ErrorCodeDescriptor MessageSchemaIncompatible;
    /** @brief A required envelope extension is not understood by the local registry. */
    extern const ErrorCodeDescriptor MessageEnvelopeUnknownRequiredField;
    /** @brief A 32-bit message sequence or acknowledgement counter cannot advance without wrapping. */
    extern const ErrorCodeDescriptor MessageCounterExhausted;
    /** @brief Caller-owned cancellation state rejected message codec admission. */
    extern const ErrorCodeDescriptor MessageEnvelopeCancelled;
    /** @brief Caller-owned timeout state rejected message codec admission. */
    extern const ErrorCodeDescriptor MessageEnvelopeTimedOut;
    /** @brief Caller-owned shutdown state rejected message codec admission. */
    extern const ErrorCodeDescriptor MessageEnvelopeShuttingDown;
}  // namespace Horo::Network::NetworkErrors
