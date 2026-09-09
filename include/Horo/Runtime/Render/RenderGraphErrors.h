#pragma once

/**
 * @file RenderGraphErrors.h
 * @brief Stable typed error descriptors for render-graph authoring.
 */

#include "Horo/Foundation/ErrorCode.h"

namespace Horo::Render::RenderGraphErrors {
    extern const ErrorCodeDescriptor AllocationFailed;
    extern const ErrorCodeDescriptor BuilderClosed;
    extern const ErrorCodeDescriptor CapacityExceeded;
    extern const ErrorCodeDescriptor DependencyCycle;
    extern const ErrorCodeDescriptor EmptyGraph;
    extern const ErrorCodeDescriptor IncompatibleQueue;
    extern const ErrorCodeDescriptor InvalidDependency;
    extern const ErrorCodeDescriptor InvalidExport;
    extern const ErrorCodeDescriptor InvalidGraph;
    extern const ErrorCodeDescriptor InvalidImport;
    extern const ErrorCodeDescriptor InvalidLimits;
    extern const ErrorCodeDescriptor InvalidPass;
    extern const ErrorCodeDescriptor InvalidResource;
    extern const ErrorCodeDescriptor InvalidUsage;
    extern const ErrorCodeDescriptor OwnerExhausted;
    extern const ErrorCodeDescriptor ReadBeforeWrite;
    extern const ErrorCodeDescriptor UnsupportedDependencyKind;
    extern const ErrorCodeDescriptor UnsupportedPassKind;
    extern const ErrorCodeDescriptor UnsupportedPassCullPolicy;
    extern const ErrorCodeDescriptor UnsupportedQueueRole;
    extern const ErrorCodeDescriptor UnsupportedResourceKind;
    extern const ErrorCodeDescriptor UnsupportedResourceClass;
    extern const ErrorCodeDescriptor UnsupportedUsage;
    extern const ErrorCodeDescriptor WrongOwner;
    extern const ErrorCodeDescriptor WrongThread;
}  // namespace Horo::Render::RenderGraphErrors
