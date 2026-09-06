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
}  // namespace Horo::Network::NetworkErrors
