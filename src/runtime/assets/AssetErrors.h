#pragma once

#include "Horo/Foundation/ErrorCode.h"

namespace Horo::Assets::AssetErrors
{
extern const ErrorCodeDescriptor IdentityInvalid;
extern const ErrorCodeDescriptor TypeInvalid;
extern const ErrorCodeDescriptor SidecarMissing;
extern const ErrorCodeDescriptor IdentityMissing;
extern const ErrorCodeDescriptor RegistryIdentityInvalid;
extern const ErrorCodeDescriptor SidecarMalformed;
extern const ErrorCodeDescriptor SchemaUnsupported;
extern const ErrorCodeDescriptor SourceMissing;
extern const ErrorCodeDescriptor DuplicateId;
extern const ErrorCodeDescriptor DuplicatePath;
extern const ErrorCodeDescriptor PathCollision;
extern const ErrorCodeDescriptor RootInvalid;
extern const ErrorCodeDescriptor SymlinkAmbiguous;
extern const ErrorCodeDescriptor IndexMalformed;
extern const ErrorCodeDescriptor IndexIo;
extern const ErrorCodeDescriptor ProviderNotFound;
extern const ErrorCodeDescriptor ProviderTooLarge;
extern const ErrorCodeDescriptor ProviderReadFailed;
extern const ErrorCodeDescriptor LoadCancelled;
extern const ErrorCodeDescriptor LoadNotReady;
extern const ErrorCodeDescriptor LoadConsumed;
extern const ErrorCodeDescriptor LoadQueueFull;
extern const ErrorCodeDescriptor LoadShutdown;
} // namespace Horo::Assets::AssetErrors
