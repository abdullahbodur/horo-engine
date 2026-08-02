#pragma once

/**
 * @file GameplayErrors.h
 * @brief Typed errors shared by gameplay authoring and runtime validation.
 */

#include "Horo/Foundation/ErrorCode.h"

namespace Horo::Gameplay::GameplayErrors {
    extern const ErrorCodeDescriptor InvalidBehaviorTypeId;
    extern const ErrorCodeDescriptor InvalidBehaviorInstanceId;
    extern const ErrorCodeDescriptor InvalidBehaviorComponent;
    extern const ErrorCodeDescriptor DuplicateBehaviorInstance;
    extern const ErrorCodeDescriptor DuplicateBehaviorType;
    extern const ErrorCodeDescriptor RegistryFrozen;
    extern const ErrorCodeDescriptor BehaviorNotRegistered;
    extern const ErrorCodeDescriptor BehaviorMultiplicityViolation;
    extern const ErrorCodeDescriptor EventQueueFull;
    extern const ErrorCodeDescriptor InvalidEvent;
}  // namespace Horo::Gameplay::GameplayErrors
