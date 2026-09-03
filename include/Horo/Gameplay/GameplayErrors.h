#pragma once

/**
 * @file GameplayErrors.h
 * @brief Typed errors shared by gameplay authoring and runtime validation.
 */

#include "Horo/Foundation/ErrorCode.h"

namespace Horo::Gameplay::GameplayErrors {
    extern const ErrorCodeDescriptor InvalidComponentTypeId;
    extern const ErrorCodeDescriptor InvalidComponentDescriptor;
    extern const ErrorCodeDescriptor InvalidSerializedComponent;
    extern const ErrorCodeDescriptor InvalidComponentMigration;
    extern const ErrorCodeDescriptor DuplicateComponentType;
    extern const ErrorCodeDescriptor ComponentRegistryFrozen;
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
    extern const ErrorCodeDescriptor InvalidGameModuleDescriptor;
    extern const ErrorCodeDescriptor IncompatibleGameModule;
    extern const ErrorCodeDescriptor InvalidGeneratedDescriptorBundle;
    extern const ErrorCodeDescriptor GeneratedDescriptorDiagnosticsPresent;
}  // namespace Horo::Gameplay::GameplayErrors
