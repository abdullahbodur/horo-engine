#include "Horo/Physics/CharacterErrors.h"

namespace Horo::Character::CharacterErrors {
    namespace {
        const ErrorDomainId CharacterDomain{"horo.character"};
    }

    const ErrorCodeDescriptor WorldInvalid{CharacterDomain,
                                           ErrorCode{"character.world.invalid"},
                                           ErrorSeverity::Error,
                                           "The Character world identity is invalid.",
                                           "Use the non-zero Character world generation published with the active scene.",
                                           false,
                                           false};
    const ErrorCodeDescriptor HandleMalformed{CharacterDomain,
                                              ErrorCode{"character.handle.malformed"},
                                              ErrorSeverity::Error,
                                              "The controller handle is malformed.",
                                              "Use a controller handle with non-zero scene, world and slot generations.",
                                              false,
                                              false};
    const ErrorCodeDescriptor HandleWorldMismatch{CharacterDomain,
                                                  ErrorCode{"character.handle.world_mismatch"},
                                                  ErrorSeverity::Error,
                                                  "The controller handle belongs to another owner generation.",
                                                  "Resolve the stable controller binding against the active scene and Character world.",
                                                  false,
                                                  false};
    const ErrorCodeDescriptor HandleStale{CharacterDomain,
                                          ErrorCode{"character.handle.stale"},
                                          ErrorSeverity::Error,
                                          "The controller slot is absent, retired or replaced.",
                                          "Discard the handle and resolve its stable authored binding again.",
                                          false,
                                          false};
    const ErrorCodeDescriptor DescriptorInvalid{CharacterDomain,
                                                ErrorCode{"character.descriptor.invalid"},
                                                ErrorSeverity::Error,
                                                "The controller descriptor is invalid.",
                                                "Provide finite geometry, unit basis, valid filtering and coherent bounds.",
                                                false,
                                                true};
    const ErrorCodeDescriptor RequestInvalid{CharacterDomain,
                                             ErrorCode{"character.request.invalid"},
                                             ErrorSeverity::Error,
                                             "The Character movement request is invalid.",
                                             "Provide one finite, explicitly tick-addressed movement intent.",
                                             false,
                                             false};
    const ErrorCodeDescriptor CapacityExceeded{CharacterDomain,
                                               ErrorCode{"character.capacity.exceeded"},
                                               ErrorSeverity::Error,
                                               "A Character operation exceeded its admitted bounded capacity.",
                                               "Lower the requested contact count or admit a larger qualified profile.",
                                               false,
                                               true};
    const ErrorCodeDescriptor InvalidState{CharacterDomain,
                                           ErrorCode{"character.state.invalid"},
                                           ErrorSeverity::Error,
                                           "The Character world lifecycle cannot admit this operation.",
                                           "Submit work only during the declared fixed-tick owner phase.",
                                           false,
                                           false};
    const ErrorCodeDescriptor OperationUnsupported{CharacterDomain,
                                                   ErrorCode{"character.operation.unsupported"},
                                                   ErrorSeverity::Error,
                                                   "The Character operation contains an unknown typed value.",
                                                   "Use a stance, collision flag or operation supported by this contract version.",
                                                   false,
                                                   true};
}  // namespace Horo::Character::CharacterErrors
