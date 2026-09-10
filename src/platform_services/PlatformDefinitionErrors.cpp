#include "Horo/PlatformServices/PlatformDefinitionRegistries.h"

namespace Horo::PlatformServices::PlatformDefinitionErrors {
    namespace {
        const ErrorDomainId Domain{"horo.platform.definition"};
    }

    const ErrorCodeDescriptor UnsupportedVersion{Domain,
                                                 ErrorCode{"platform.definition.version_unsupported"},
                                                 ErrorSeverity::Error,
                                                 "Definition schema version is unsupported.",
                                                 "Migrate to the supported schema.",
                                                 false,
                                                 true};
    const ErrorCodeDescriptor CapacityExceeded{Domain,
                                               ErrorCode{"platform.definition.capacity_exceeded"},
                                               ErrorSeverity::Error,
                                               "Definition bounds were exceeded.",
                                               "Reduce the document or bounds.",
                                               false,
                                               true};
    const ErrorCodeDescriptor InvalidDefinition{Domain,
                                                ErrorCode{"platform.definition.invalid"},
                                                ErrorSeverity::Error,
                                                "Definition is malformed.",
                                                "Correct the diagnosed field.",
                                                false,
                                                true};
    const ErrorCodeDescriptor DuplicateDefinition{Domain,
                                                  ErrorCode{"platform.definition.duplicate"},
                                                  ErrorSeverity::Error,
                                                  "Stable identity is defined more than once.",
                                                  "Retain exactly one definition.",
                                                  false,
                                                  true};
    const ErrorCodeDescriptor UnknownIdentity{Domain,
                                              ErrorCode{"platform.definition.identity_unknown"},
                                              ErrorSeverity::Error,
                                              "Definition references no active stable identity.",
                                              "Use an active identity of the right kind.",
                                              false,
                                              true};
    const ErrorCodeDescriptor IncompleteRegistry{Domain,
                                                 ErrorCode{"platform.definition.registry_incomplete"},
                                                 ErrorSeverity::Error,
                                                 "An active identity has no definition.",
                                                 "Define every active identity.",
                                                 false,
                                                 true};
    const ErrorCodeDescriptor InvalidCrossReference{Domain,
                                                    ErrorCode{"platform.definition.cross_reference_invalid"},
                                                    ErrorSeverity::Error,
                                                    "Definition cross-reference is missing or incompatible.",
                                                    "Reference a compatible definition in the captured registry.",
                                                    false,
                                                    true};
    const ErrorCodeDescriptor StaleIdentityRegistry{Domain,
                                                    ErrorCode{"platform.definition.identity_registry_stale"},
                                                    ErrorSeverity::Error,
                                                    "Definitions target another identity generation.",
                                                    "Rebuild against the captured stable-ID registry.",
                                                    false,
                                                    false};
    const ErrorCodeDescriptor ImmutableContractChanged{Domain,
                                                       ErrorCode{"platform.definition.immutable_contract_changed"},
                                                       ErrorSeverity::Error,
                                                       "Published semantic fields changed without migration.",
                                                       "Create an explicit product migration.",
                                                       false,
                                                       true};
}  // namespace Horo::PlatformServices::PlatformDefinitionErrors
