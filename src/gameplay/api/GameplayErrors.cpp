#include "Horo/Gameplay/GameplayErrors.h"

namespace Horo::Gameplay::GameplayErrors {
    namespace {
        const ErrorDomainId GameplayDomain{"horo.gameplay"};
    }

    const ErrorCodeDescriptor InvalidBehaviorTypeId{GameplayDomain,
                                                    ErrorCode{"gameplay.behavior_type_id_invalid"},
                                                    ErrorSeverity::Error,
                                                    "The behavior type ID is invalid.",
                                                    "Use a lowercase game.<module>.<behavior> identifier.",
                                                    false,
                                                    true};
    const ErrorCodeDescriptor InvalidBehaviorInstanceId{GameplayDomain,
                                                        ErrorCode{"gameplay.behavior_instance_id_invalid"},
                                                        ErrorSeverity::Error,
                                                        "The behavior instance ID is invalid.",
                                                        "Assign a non-zero stable attachment identity.",
                                                        false,
                                                        true};
    const ErrorCodeDescriptor InvalidBehaviorComponent{GameplayDomain,
                                                       ErrorCode{"gameplay.behavior_component_invalid"},
                                                       ErrorSeverity::Error,
                                                       "The behavior component payload is invalid.",
                                                       "Repair the behavior schema version or bounded authoring fields.",
                                                       false,
                                                       true};
    const ErrorCodeDescriptor DuplicateBehaviorInstance{GameplayDomain,
                                                        ErrorCode{"gameplay.behavior_instance_duplicate"},
                                                        ErrorSeverity::Error,
                                                        "A behavior instance ID is duplicated.",
                                                        "Regenerate one of the attachment identities.",
                                                        false,
                                                        true};
    const ErrorCodeDescriptor DuplicateBehaviorType{GameplayDomain,
                                                    ErrorCode{"gameplay.behavior_type_duplicate"},
                                                    ErrorSeverity::Error,
                                                    "A behavior type ID is duplicated.",
                                                    "Give each registered behavior a unique stable ID.",
                                                    false,
                                                    true};
    const ErrorCodeDescriptor RegistryFrozen{GameplayDomain,
                                             ErrorCode{"gameplay.registry_frozen"},
                                             ErrorSeverity::Error,
                                             "The behavior registry is frozen.",
                                             "Register complete descriptors before activating a runtime scene.",
                                             false,
                                             false};
    const ErrorCodeDescriptor BehaviorNotRegistered{GameplayDomain,
                                                    ErrorCode{"gameplay.behavior_not_registered"},
                                                    ErrorSeverity::Error,
                                                    "The behavior type is not registered.",
                                                    "Build or restore the behavior implementation before entering Play Mode.",
                                                    false,
                                                    true};
    const ErrorCodeDescriptor BehaviorMultiplicityViolation{GameplayDomain,
                                                            ErrorCode{"gameplay.behavior_multiplicity_violation"},
                                                            ErrorSeverity::Error,
                                                            "The object contains too many instances of this behavior type.",
                                                            "Remove the duplicate attachment or enable allowMultiple.",
                                                            false,
                                                            true};
    const ErrorCodeDescriptor EventQueueFull{GameplayDomain,
                                             ErrorCode{"gameplay.event_queue_full"},
                                             ErrorSeverity::Warning,
                                             "The scene gameplay event queue is full.",
                                             "Reduce event traffic or increase the explicit scene event budget.",
                                             true,
                                             true};
    const ErrorCodeDescriptor InvalidEvent{GameplayDomain,
                                           ErrorCode{"gameplay.event_invalid"},
                                           ErrorSeverity::Error,
                                           "The gameplay event is invalid.",
                                           "Use a registered event type, valid target, and bounded schema payload.",
                                           false,
                                           true};
}  // namespace Horo::Gameplay::GameplayErrors
