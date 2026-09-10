#include "Horo/PCG/PCGErrors.h"

namespace Horo::PCG::PCGErrors {
    namespace {
        const ErrorDomainId PcgDomain{"horo.pcg"};
    }

    const ErrorCodeDescriptor IdentityInvalid{.domain = PcgDomain,
                                              .code = ErrorCode{"pcg.identity.invalid"},
                                              .defaultSeverity = ErrorSeverity::Error,
                                              .summary = "The PCG identity uses a reserved representation.",
                                              .remediationHint = "Use identities issued by the owning PCG boundary."};
    const ErrorCodeDescriptor IdentityUnknown{.domain = PcgDomain,
                                              .code = ErrorCode{"pcg.identity.unknown"},
                                              .defaultSeverity = ErrorSeverity::Warning,
                                              .summary = "The PCG identity belongs to a different graph or logical owner.",
                                              .remediationHint = "Resolve the identity only through its exact graph owner."};
    const ErrorCodeDescriptor IdentityStale{.domain = PcgDomain,
                                            .code = ErrorCode{"pcg.identity.stale"},
                                            .defaultSeverity = ErrorSeverity::Warning,
                                            .summary = "The PCG identity belongs to a retired graph revision or execution.",
                                            .remediationHint = "Discard stale work and resolve the current generation explicitly."};
    const ErrorCodeDescriptor RevisionExhausted{.domain = PcgDomain,
                                                .code = ErrorCode{"pcg.revision.exhausted"},
                                                .defaultSeverity = ErrorSeverity::Critical,
                                                .summary = "The PCG graph revision cannot advance without wrapping.",
                                                .remediationHint = "Retire the graph identity permanently; never reuse a revision."};
    const ErrorCodeDescriptor SerializedIdentityInvalid{.domain = PcgDomain,
                                                        .code = ErrorCode{"pcg.identity.serialized_invalid"},
                                                        .defaultSeverity = ErrorSeverity::Error,
                                                        .summary = "Serialized PCG identity bytes contain a reserved value.",
                                                        .remediationHint = "Reject or recook the malformed PCG payload."};
    const ErrorCodeDescriptor PointSchemaInvalid{.domain = PcgDomain,
                                                 .code = ErrorCode{"pcg.point.schema_invalid"},
                                                 .defaultSeverity = ErrorSeverity::Error,
                                                 .summary = "The PCG point schema is invalid.",
                                                 .remediationHint = "Use bounded canonical namespaced keys and supported attribute types."};
    const ErrorCodeDescriptor PointAttributeDuplicate{.domain = PcgDomain,
                                                      .code = ErrorCode{"pcg.point.attribute_duplicate"},
                                                      .defaultSeverity = ErrorSeverity::Error,
                                                      .summary = "A PCG point attribute is duplicated.",
                                                      .remediationHint = "Define and provide each canonical attribute exactly once."};
    const ErrorCodeDescriptor PointAttributeUnknown{.domain = PcgDomain,
                                                    .code = ErrorCode{"pcg.point.attribute_unknown"},
                                                    .defaultSeverity = ErrorSeverity::Error,
                                                    .summary = "A PCG point column is not declared by its schema.",
                                                    .remediationHint = "Remove the unknown column or declare it in the immutable schema."};
    const ErrorCodeDescriptor PointAttributeTypeMismatch{.domain = PcgDomain,
                                                         .code = ErrorCode{"pcg.point.attribute_type_mismatch"},
                                                         .defaultSeverity = ErrorSeverity::Error,
                                                         .summary = "A PCG point column type is incompatible with its schema.",
                                                         .remediationHint = "Supply the exact closed column type declared by the schema."};
    const ErrorCodeDescriptor PointDataInvalid{.domain = PcgDomain,
                                               .code = ErrorCode{"pcg.point.data_invalid"},
                                               .defaultSeverity = ErrorSeverity::Error,
                                               .summary = "PCG point core or column data is invalid.",
                                               .remediationHint =
                                                   "Use finite transforms, valid bounds, density in [0,1], and equal column lengths."};
    const ErrorCodeDescriptor PointCapacityExceeded{.domain = PcgDomain,
                                                    .code = ErrorCode{"pcg.point.capacity_exceeded"},
                                                    .defaultSeverity = ErrorSeverity::Error,
                                                    .summary = "A PCG point tier capacity was exceeded.",
                                                    .remediationHint =
                                                        "Reduce point, attribute, payload, or memory size before admission."};
    const ErrorCodeDescriptor PointSizeOverflow{.domain = PcgDomain,
                                                .code = ErrorCode{"pcg.point.size_overflow"},
                                                .defaultSeverity = ErrorSeverity::Error,
                                                .summary = "PCG point byte accounting overflowed.",
                                                .remediationHint =
                                                    "Reject the candidate and compute all byte envelopes with checked arithmetic."};
}  // namespace Horo::PCG::PCGErrors
