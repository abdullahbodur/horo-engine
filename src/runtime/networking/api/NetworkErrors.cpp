#include "Horo/Network/NetworkErrors.h"

namespace Horo::Network::NetworkErrors {
    namespace {
        const ErrorDomainId NetworkDomain{"horo.network"};
    }

    const ErrorCodeDescriptor
        NetworkAddressInvalid{NetworkDomain,
                              ErrorCode{"network.address.invalid"},
                              ErrorSeverity::Error,
                              "The network endpoint is malformed or ambiguous.",
                              "Use canonical IPv4, bracketed IPv6, or a bounded ASCII DNS hostname with a numeric port.",
                              false,
                              true};
    const ErrorCodeDescriptor
        NetworkAddressUnsupported{NetworkDomain,
                                  ErrorCode{"network.address.unsupported"},
                                  ErrorSeverity::Error,
                                  "The network endpoint representation is unsupported.",
                                  "Remove schemes and zones; provide an ASCII DNS name already resolved by host policy.",
                                  false,
                                  true};
    const ErrorCodeDescriptor NetworkAddressCapacityExceeded{NetworkDomain,
                                                             ErrorCode{"network.address.capacity_exceeded"},
                                                             ErrorSeverity::Error,
                                                             "The network endpoint exceeds its finite text capacity.",
                                                             "Use a DNS name of at most 253 bytes and a canonical numeric port.",
                                                             false,
                                                             true};
    const ErrorCodeDescriptor TransportHandleInvalid{NetworkDomain,
                                                     ErrorCode{"network.transport.handle_invalid"},
                                                     ErrorSeverity::Error,
                                                     "The transport handle is invalid or stale.",
                                                     "Use the exact current slot generation issued by the owning transport.",
                                                     false,
                                                     false};
    const ErrorCodeDescriptor TransportGenerationExhausted{NetworkDomain,
                                                           ErrorCode{"network.transport.generation_exhausted"},
                                                           ErrorSeverity::Error,
                                                           "The transport handle generation cannot advance without wrapping.",
                                                           "Retire the exhausted slot permanently and allocate a different bounded slot.",
                                                           false,
                                                           false};

    const ErrorCodeDescriptor PacketBufferInvalid{NetworkDomain,
                                                  ErrorCode{"network.packet.buffer_invalid"},
                                                  ErrorSeverity::Error,
                                                  "Packet buffer pool bounds are invalid.",
                                                  "Use positive finite slot and byte bounds.",
                                                  false,
                                                  true};
    const ErrorCodeDescriptor PacketBufferCapacityExceeded{NetworkDomain,
                                                           ErrorCode{"network.packet.buffer_capacity_exceeded"},
                                                           ErrorSeverity::Error,
                                                           "Packet buffer capacity was exceeded.",
                                                           "Reduce the payload or prepare a larger bounded pool.",
                                                           false,
                                                           true};
    const ErrorCodeDescriptor PacketBufferPoolExhausted{NetworkDomain,
                                                        ErrorCode{"network.packet.buffer_pool_exhausted"},
                                                        ErrorSeverity::Error,
                                                        "No prepared packet buffer slot is available.",
                                                        "Release a lease or apply explicit queue backpressure.",
                                                        true,
                                                        false};
    const ErrorCodeDescriptor PacketQueueInvalid{NetworkDomain,
                                                 ErrorCode{"network.packet.queue_invalid"},
                                                 ErrorSeverity::Error,
                                                 "Packet queue input or bounds are invalid.",
                                                 "Use valid identities and positive finite bounds.",
                                                 false,
                                                 true};
    const ErrorCodeDescriptor PacketQueueCapacityExceeded{NetworkDomain,
                                                          ErrorCode{"network.packet.queue_capacity_exceeded"},
                                                          ErrorSeverity::Error,
                                                          "A packet can never fit the queue's declared byte bounds.",
                                                          "Reduce the packet or explicitly revise queue bounds.",
                                                          false,
                                                          true};
    const ErrorCodeDescriptor PacketQueueFull{NetworkDomain,
                                              ErrorCode{"network.packet.queue_full"},
                                              ErrorSeverity::Error,
                                              "The bounded packet queue rejected admission.",
                                              "Apply the queue's explicit overload policy or retry after bounded drain.",
                                              true,
                                              false};

    const ErrorCodeDescriptor IdentityInvalid{
        .domain = NetworkDomain,
        .code = ErrorCode{"network.identity.invalid"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "A stable network identity is invalid.",
        .remediationHint =
            "Use a non-zero identity allocated by the declaring owner; never derive identity from a name or storage address.",
        .retryable = false,
        .userActionable = true,
    };
    const ErrorCodeDescriptor ReplicationDescriptorInvalid{
        .domain = NetworkDomain,
        .code = ErrorCode{"network.replication.descriptor_invalid"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "Replication descriptor metadata is malformed.",
        .remediationHint = "Provide known typed policies, finite limits, canonical defaults, and valid stable identities.",
        .retryable = false,
        .userActionable = true,
    };
    const ErrorCodeDescriptor ReplicationDescriptorConflict{
        .domain = NetworkDomain,
        .code = ErrorCode{"network.replication.descriptor_conflict"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "Replication descriptor identities conflict.",
        .remediationHint = "Allocate each schema and field identity once and keep removed field identities tombstoned.",
        .retryable = false,
        .userActionable = true,
    };
    const ErrorCodeDescriptor ReplicationDescriptorIncompatible{
        .domain = NetworkDomain,
        .code = ErrorCode{"network.replication.descriptor_incompatible"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "Replication descriptor evolution is incompatible.",
        .remediationHint = "Preserve existing field semantics, add compatible fields as optional defaults, or declare a new major-version "
                           "translation later.",
        .retryable = false,
        .userActionable = true,
    };
    const ErrorCodeDescriptor ReplicationSchemaUnknown{
        .domain = NetworkDomain,
        .code = ErrorCode{"network.replication.schema_unknown"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "The requested replication schema is absent.",
        .remediationHint = "Use an exact schema identity from the pinned registry snapshot; no default schema is substituted.",
        .retryable = false,
        .userActionable = false,
    };
    const ErrorCodeDescriptor ReplicationCapacityExceeded{
        .domain = NetworkDomain,
        .code = ErrorCode{"network.replication.capacity_exceeded"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "Replication descriptor construction exceeded its finite capacity.",
        .remediationHint = "Reduce the schema, field, owner-identity, or canonical-default footprint within the admitted limits.",
        .retryable = false,
        .userActionable = true,
    };
    const ErrorCodeDescriptor TransportCapabilityDescriptorInvalid{
        .domain = NetworkDomain,
        .code = ErrorCode{"network.transport.capability_descriptor_invalid"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "Transport capability evidence or delivery requirements are malformed.",
        .remediationHint = "Use version-one evidence, an exact non-zero revision, known policies, and finite positive limits.",
        .retryable = false,
        .userActionable = true,
    };
    const ErrorCodeDescriptor TransportCapabilityStale{
        .domain = NetworkDomain,
        .code = ErrorCode{"network.transport.capability_stale"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "Transport capability evidence changed during admission.",
        .remediationHint = "Capture the current candidate evidence and repeat complete delivery negotiation.",
        .retryable = true,
        .userActionable = false,
    };
    const ErrorCodeDescriptor TransportDeliveryUnsupported{
        .domain = NetworkDomain,
        .code = ErrorCode{"network.transport.delivery_unsupported"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "The selected transport candidate cannot provide the exact required delivery semantics.",
        .remediationHint = "Choose an explicit candidate supporting the required policy; do not weaken reliability or ordering.",
        .retryable = false,
        .userActionable = true,
    };
    const ErrorCodeDescriptor TransportCapabilityUnavailable{
        .domain = NetworkDomain,
        .code = ErrorCode{"network.transport.capability_unavailable"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "A required transport capability is not currently available.",
        .remediationHint = "Restore the explicit candidate or retry after it publishes a new capability revision.",
        .retryable = true,
        .userActionable = false,
    };
    const ErrorCodeDescriptor TransportLimitExceeded{
        .domain = NetworkDomain,
        .code = ErrorCode{"network.transport.limit_exceeded"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "A transport delivery requirement exceeds the candidate's finite limits.",
        .remediationHint = "Reduce the requested channel, message, or deadline bound or choose an explicit capable candidate.",
        .retryable = false,
        .userActionable = true,
    };
    const ErrorCodeDescriptor TransportOperationCancelled{
        .domain = NetworkDomain,
        .code = ErrorCode{"network.transport.operation_cancelled"},
        .defaultSeverity = ErrorSeverity::Info,
        .summary = "Transport admission was cancelled before queue mutation.",
        .remediationHint = "Submit again only if the owning caller still requires the operation.",
        .retryable = true,
        .userActionable = false,
    };
    const ErrorCodeDescriptor TransportShuttingDown{
        .domain = NetworkDomain,
        .code = ErrorCode{"network.transport.shutting_down"},
        .defaultSeverity = ErrorSeverity::Info,
        .summary = "Transport admission was rejected because shutdown has begun.",
        .remediationHint = "Do not enqueue new transport work after the owning runtime begins shutdown.",
        .retryable = false,
        .userActionable = false,
    };
    const ErrorCodeDescriptor ProtocolIdentityDescriptorInvalid{
        .domain = NetworkDomain,
        .code = ErrorCode{"network.protocol.identity_descriptor_invalid"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "Protocol identity metadata is malformed.",
        .remediationHint = "Use non-zero fixed-width identities, matching namespaces, and valid bounded versions.",
        .retryable = false,
        .userActionable = true,
    };
    const ErrorCodeDescriptor ProtocolIdentityConflict{
        .domain = NetworkDomain,
        .code = ErrorCode{"network.protocol.identity_conflict"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "Protocol identity registrations conflict.",
        .remediationHint = "Allocate each protocol-scoped wire identity once and preserve it across renames.",
        .retryable = false,
        .userActionable = true,
    };
    const ErrorCodeDescriptor ProtocolIdentityUnknown{
        .domain = NetworkDomain,
        .code = ErrorCode{"network.protocol.identity_unknown"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "The requested protocol identity is not registered.",
        .remediationHint = "Use an exact identity from the pinned registry; no name or default fallback is applied.",
        .retryable = false,
        .userActionable = false,
    };
    const ErrorCodeDescriptor ProtocolVersionIncompatible{
        .domain = NetworkDomain,
        .code = ErrorCode{"network.protocol.version_incompatible"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "Protocol versions have no compatible overlap.",
        .remediationHint = "Advertise an explicit same-major interval permitted by both protocol peers.",
        .retryable = false,
        .userActionable = true,
    };
    const ErrorCodeDescriptor ProtocolIdentityCapacityExceeded{
        .domain = NetworkDomain,
        .code = ErrorCode{"network.protocol.identity_capacity_exceeded"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "Protocol identity registry construction exceeded its finite capacity.",
        .remediationHint = "Reduce protocol, message, feature, or close-reason contributions within configured limits.",
        .retryable = false,
        .userActionable = true,
    };
    const ErrorCodeDescriptor MessageEnvelopeInvalid{
        .domain = NetworkDomain,
        .code = ErrorCode{"network.message.envelope_invalid"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "Message envelope framing or codec metadata is malformed.",
        .remediationHint = "Use the canonical fixed header, ordered bounded fields, and exact declared lengths.",
        .retryable = false,
        .userActionable = true,
    };
    const ErrorCodeDescriptor MessageEnvelopeCapacityExceeded{
        .domain = NetworkDomain,
        .code = ErrorCode{"network.message.envelope_capacity_exceeded"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "Message envelope exceeds an admitted finite bound.",
        .remediationHint = "Reduce frame, payload, extension count, or extension bytes within the active limits.",
        .retryable = false,
        .userActionable = true,
    };
    const ErrorCodeDescriptor MessageCodecConflict{
        .domain = NetworkDomain,
        .code = ErrorCode{"network.message.codec_conflict"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "Message codec metadata contains a duplicate stable identity.",
        .remediationHint = "Publish one codec and one extension descriptor for each protocol-scoped identity.",
        .retryable = false,
        .userActionable = true,
    };
    const ErrorCodeDescriptor MessageCodecUnknown{
        .domain = NetworkDomain,
        .code = ErrorCode{"network.message.codec_unknown"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "No exact payload codec metadata is registered for the message.",
        .remediationHint = "Use an explicitly registered protocol and message identity; no fallback codec is selected.",
        .retryable = false,
        .userActionable = false,
    };
    const ErrorCodeDescriptor MessageSchemaIncompatible{
        .domain = NetworkDomain,
        .code = ErrorCode{"network.message.schema_incompatible"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "The payload schema identity or version is incompatible.",
        .remediationHint = "Use the codec's exact schema identity and an explicitly supported same-major version.",
        .retryable = false,
        .userActionable = true,
    };
    const ErrorCodeDescriptor MessageEnvelopeUnknownRequiredField{
        .domain = NetworkDomain,
        .code = ErrorCode{"network.message.unknown_required_field"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "A required message-envelope extension is unknown.",
        .remediationHint = "Negotiate support before sending required extensions; only optional unknown fields may be skipped.",
        .retryable = false,
        .userActionable = true,
    };
    const ErrorCodeDescriptor MessageCounterExhausted{
        .domain = NetworkDomain,
        .code = ErrorCode{"network.message.counter_exhausted"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "A message wire counter cannot advance without wrapping.",
        .remediationHint = "Close or replace the owning protocol/session generation before reusing a 32-bit counter value.",
        .retryable = false,
        .userActionable = false,
    };
    const ErrorCodeDescriptor MessageEnvelopeCancelled{
        .domain = NetworkDomain,
        .code = ErrorCode{"network.message.cancelled"},
        .defaultSeverity = ErrorSeverity::Warning,
        .summary = "Message codec admission was cancelled.",
        .remediationHint = "Do not retry unless the caller supplies a new active operation generation.",
        .retryable = false,
        .userActionable = false,
    };
    const ErrorCodeDescriptor MessageEnvelopeTimedOut{
        .domain = NetworkDomain,
        .code = ErrorCode{"network.message.timed_out"},
        .defaultSeverity = ErrorSeverity::Warning,
        .summary = "Message codec admission deadline expired.",
        .remediationHint = "Discard the late frame and continue only under a new caller-owned operation generation.",
        .retryable = false,
        .userActionable = false,
    };
    const ErrorCodeDescriptor MessageEnvelopeShuttingDown{
        .domain = NetworkDomain,
        .code = ErrorCode{"network.message.shutting_down"},
        .defaultSeverity = ErrorSeverity::Info,
        .summary = "Message codec admission is closed for shutdown.",
        .remediationHint = "Stop producing new frames and complete bounded owner-controlled teardown.",
        .retryable = false,
        .userActionable = false,
    };
}  // namespace Horo::Network::NetworkErrors
