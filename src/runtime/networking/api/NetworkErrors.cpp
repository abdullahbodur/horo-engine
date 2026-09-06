#include "Horo/Network/NetworkErrors.h"

namespace Horo::Network::NetworkErrors {
    namespace {
        const ErrorDomainId NetworkDomain{"horo.network"};
    }

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
}  // namespace Horo::Network::NetworkErrors
