#pragma once

#include "Horo/Foundation/ErrorCode.h"

namespace Horo::Render::RegistryErrors
{
extern const ErrorCodeDescriptor BackendNotFound;
extern const ErrorCodeDescriptor DuplicateBackend;
extern const ErrorCodeDescriptor InvalidDescriptor;
extern const ErrorCodeDescriptor NotSealed;
extern const ErrorCodeDescriptor ProviderException;
extern const ErrorCodeDescriptor ProviderReturnedNull;
extern const ErrorCodeDescriptor Sealed;
} // namespace Horo::Render::RegistryErrors
