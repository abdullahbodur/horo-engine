#include "Horo/PlatformServices/PlatformDefinitionRegistries.h"
#include "PlatformDefinitionRegistryDetail.h"

namespace Horo::PlatformServices::PlatformDefinitionErrors {
    using DefinitionRegistryDetail::MakeDescriptor;

    namespace {
        const ErrorDomainId Domain{"horo.platform.definition"};
    }

    const ErrorCodeDescriptor UnsupportedVersion =
        MakeDescriptor(Domain, "platform.definition.version_unsupported", "Definition schema version is unsupported.",
                       "Migrate to the supported schema.", true);
    const ErrorCodeDescriptor CapacityExceeded = MakeDescriptor(Domain, "platform.definition.capacity_exceeded",
                                                                "Definition bounds were exceeded.", "Reduce the document or bounds.", true);
    const ErrorCodeDescriptor InvalidDefinition =
        MakeDescriptor(Domain, "platform.definition.invalid", "Definition is malformed.", "Correct the diagnosed field.", true);
    const ErrorCodeDescriptor DuplicateDefinition =
        MakeDescriptor(Domain, "platform.definition.duplicate", "Stable identity is defined more than once.",
                       "Retain exactly one definition.", true);
    const ErrorCodeDescriptor UnknownIdentity =
        MakeDescriptor(Domain, "platform.definition.identity_unknown", "Definition references no active stable identity.",
                       "Use an active identity of the right kind.", true);
    const ErrorCodeDescriptor IncompleteRegistry =
        MakeDescriptor(Domain, "platform.definition.registry_incomplete", "An active identity has no definition.",
                       "Define every active identity.", true);
    const ErrorCodeDescriptor InvalidCrossReference =
        MakeDescriptor(Domain, "platform.definition.cross_reference_invalid", "Definition cross-reference is missing or incompatible.",
                       "Reference a compatible definition in the captured registry.", true);
    const ErrorCodeDescriptor StaleIdentityRegistry =
        MakeDescriptor(Domain, "platform.definition.identity_registry_stale", "Definitions target another identity generation.",
                       "Rebuild against the captured stable-ID registry.", false);
    const ErrorCodeDescriptor ImmutableContractChanged =
        MakeDescriptor(Domain, "platform.definition.immutable_contract_changed", "Published semantic fields changed without migration.",
                       "Create an explicit product migration.", true);
}  // namespace Horo::PlatformServices::PlatformDefinitionErrors
