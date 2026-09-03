#include "Horo/Gameplay/GameplayErrors.h"

namespace Horo::Gameplay::GameplayErrors {
    namespace {
        const ErrorDomainId GameplayDomain{"horo.gameplay"};
    }

    const ErrorCodeDescriptor InvalidComponentTypeId{
        .domain = GameplayDomain,
        .code = ErrorCode{"gameplay.component_type_id_invalid"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "The gameplay component type ID is invalid.",
        .remediationHint = "Use a lowercase game.<module>.<component> identifier.",
        .retryable = false,
        .userActionable = true,
    };
    const ErrorCodeDescriptor InvalidComponentDescriptor{
        .domain = GameplayDomain,
        .code = ErrorCode{"gameplay.component_descriptor_invalid"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "The gameplay component descriptor is invalid.",
        .remediationHint = "Provide a stable identity, current schema, and unique bounded property metadata.",
        .retryable = false,
        .userActionable = true,
    };
    const ErrorCodeDescriptor InvalidSerializedComponent{
        .domain = GameplayDomain,
        .code = ErrorCode{"gameplay.serialized_component_invalid"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "The serialized gameplay component envelope is invalid.",
        .remediationHint = "Repair its stable identity, schema version, or bounded opaque payload.",
        .retryable = false,
        .userActionable = true,
    };
    const ErrorCodeDescriptor InvalidComponentMigration{
        .domain = GameplayDomain,
        .code = ErrorCode{"gameplay.component_migration_invalid"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "The gameplay component migration metadata is invalid or ambiguous.",
        .remediationHint = "Declare unique forward-only migration edges ending at the current schema.",
        .retryable = false,
        .userActionable = true,
    };
    const ErrorCodeDescriptor DuplicateComponentType{
        .domain = GameplayDomain,
        .code = ErrorCode{"gameplay.component_type_duplicate"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "A gameplay component type ID is duplicated.",
        .remediationHint = "Give every project-owned component one unique stable ID.",
        .retryable = false,
        .userActionable = true,
    };
    const ErrorCodeDescriptor ComponentRegistryFrozen{
        .domain = GameplayDomain,
        .code = ErrorCode{"gameplay.component_registry_frozen"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "The gameplay component registry is frozen.",
        .remediationHint = "Register complete component metadata before module startup.",
        .retryable = false,
        .userActionable = false,
    };
    const ErrorCodeDescriptor InvalidBehaviorTypeId{
        .domain = GameplayDomain,
        .code = ErrorCode{"gameplay.behavior_type_id_invalid"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "The behavior type ID is invalid.",
        .remediationHint = "Use a lowercase game.<module>.<behavior> identifier.",
        .retryable = false,
        .userActionable = true,
    };
    const ErrorCodeDescriptor InvalidBehaviorInstanceId{
        .domain = GameplayDomain,
        .code = ErrorCode{"gameplay.behavior_instance_id_invalid"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "The behavior instance ID is invalid.",
        .remediationHint = "Assign a non-zero stable attachment identity.",
        .retryable = false,
        .userActionable = true,
    };
    const ErrorCodeDescriptor InvalidBehaviorComponent{
        .domain = GameplayDomain,
        .code = ErrorCode{"gameplay.behavior_component_invalid"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "The behavior component payload is invalid.",
        .remediationHint = "Repair the behavior schema version or bounded authoring fields.",
        .retryable = false,
        .userActionable = true,
    };
    const ErrorCodeDescriptor DuplicateBehaviorInstance{
        .domain = GameplayDomain,
        .code = ErrorCode{"gameplay.behavior_instance_duplicate"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "A behavior instance ID is duplicated.",
        .remediationHint = "Regenerate one of the attachment identities.",
        .retryable = false,
        .userActionable = true,
    };
    const ErrorCodeDescriptor DuplicateBehaviorType{
        .domain = GameplayDomain,
        .code = ErrorCode{"gameplay.behavior_type_duplicate"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "A behavior type ID is duplicated.",
        .remediationHint = "Give each registered behavior a unique stable ID.",
        .retryable = false,
        .userActionable = true,
    };
    const ErrorCodeDescriptor RegistryFrozen{
        .domain = GameplayDomain,
        .code = ErrorCode{"gameplay.registry_frozen"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "The behavior registry is frozen.",
        .remediationHint = "Register complete descriptors before activating a runtime scene.",
        .retryable = false,
        .userActionable = false,
    };
    const ErrorCodeDescriptor BehaviorNotRegistered{
        .domain = GameplayDomain,
        .code = ErrorCode{"gameplay.behavior_not_registered"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "The behavior type is not registered.",
        .remediationHint = "Build or restore the behavior implementation before entering Play Mode.",
        .retryable = false,
        .userActionable = true,
    };
    const ErrorCodeDescriptor BehaviorMultiplicityViolation{
        .domain = GameplayDomain,
        .code = ErrorCode{"gameplay.behavior_multiplicity_violation"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "The object contains too many instances of this behavior type.",
        .remediationHint = "Remove the duplicate attachment or enable allowMultiple.",
        .retryable = false,
        .userActionable = true,
    };
    const ErrorCodeDescriptor EventQueueFull{
        .domain = GameplayDomain,
        .code = ErrorCode{"gameplay.event_queue_full"},
        .defaultSeverity = ErrorSeverity::Warning,
        .summary = "The scene gameplay event queue is full.",
        .remediationHint = "Reduce event traffic or increase the explicit scene event budget.",
        .retryable = true,
        .userActionable = true,
    };
    const ErrorCodeDescriptor InvalidEvent{
        .domain = GameplayDomain,
        .code = ErrorCode{"gameplay.event_invalid"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "The gameplay event is invalid.",
        .remediationHint = "Use a registered event type, valid target, and bounded schema payload.",
        .retryable = false,
        .userActionable = true,
    };
    const ErrorCodeDescriptor InvalidGameModuleDescriptor{
        .domain = GameplayDomain,
        .code = ErrorCode{"gameplay.module_descriptor_invalid"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "The gameplay module descriptor is malformed.",
        .remediationHint = "Rebuild the project gameplay module from valid generated inputs.",
        .retryable = false,
        .userActionable = true,
    };
    const ErrorCodeDescriptor IncompatibleGameModule{
        .domain = GameplayDomain,
        .code = ErrorCode{"gameplay.module_incompatible"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "The gameplay module does not match the requested build identity.",
        .remediationHint = "Rebuild the gameplay module with the active Horo SDK and project manifest.",
        .retryable = false,
        .userActionable = true,
    };
    const ErrorCodeDescriptor InvalidGeneratedDescriptorBundle{
        .domain = GameplayDomain,
        .code = ErrorCode{"gameplay.generated_descriptor_bundle_invalid"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "The generated gameplay descriptor bundle is malformed or incomplete.",
        .remediationHint = "Regenerate the complete descriptor bundle and rebuild the gameplay module.",
        .retryable = false,
        .userActionable = true,
    };
    const ErrorCodeDescriptor GeneratedDescriptorDiagnosticsPresent{
        .domain = GameplayDomain,
        .code = ErrorCode{"gameplay.generated_descriptor_diagnostics_present"},
        .defaultSeverity = ErrorSeverity::Error,
        .summary = "The generated gameplay descriptor bundle contains blocking diagnostics.",
        .remediationHint = "Resolve every generated descriptor diagnostic before activating gameplay code.",
        .retryable = false,
        .userActionable = true,
    };
}  // namespace Horo::Gameplay::GameplayErrors
