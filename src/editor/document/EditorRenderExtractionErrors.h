#pragma once

#include "Horo/Foundation/ErrorCode.h"

namespace Horo::Editor::ViewportSceneErrors
{
    extern const ErrorCodeDescriptor HierarchyCycle;
    extern const ErrorCodeDescriptor InstanceIdentityMismatch;
    extern const ErrorCodeDescriptor InvalidCamera;
    extern const ErrorCodeDescriptor InvalidObjectId;
    extern const ErrorCodeDescriptor InvalidResult;
    extern const ErrorCodeDescriptor ObjectNotFound;
    extern const ErrorCodeDescriptor ParentNotFound;
} // namespace Horo::Editor::ViewportSceneErrors

namespace Horo::Editor::ViewportPickingErrors
{
    extern const ErrorCodeDescriptor InvalidIdentity;
    extern const ErrorCodeDescriptor InvalidQuery;
    extern const ErrorCodeDescriptor InvalidScene;
} // namespace Horo::Editor::ViewportPickingErrors
