#pragma once

#include "Horo/Foundation/ErrorCode.h"

namespace Horo::Extensions::ExtensionErrors {
    extern const ErrorCodeDescriptor InvalidManifest;
    extern const ErrorCodeDescriptor LoadFailed;
    extern const ErrorCodeDescriptor MissingEntryPoint;
    extern const ErrorCodeDescriptor ContributionRejected;
    extern const ErrorCodeDescriptor InvocationFailed;
}  // namespace Horo::Extensions::ExtensionErrors
