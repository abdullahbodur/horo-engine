#pragma once

#include "Horo/Foundation/ErrorCode.h"

namespace Horo::ConfigurationErrors
{
extern const ErrorCodeDescriptor SchemaInvalid;
extern const ErrorCodeDescriptor SchemaSealed;
extern const ErrorCodeDescriptor DraftStale;
extern const ErrorCodeDescriptor ValueInvalid;
extern const ErrorCodeDescriptor JsonParseError;
extern const ErrorCodeDescriptor FileNotFound;
extern const ErrorCodeDescriptor FileWriteError;
} // namespace Horo::ConfigurationErrors

namespace Horo::JobErrors
{
extern const ErrorCodeDescriptor Cancelled;
extern const ErrorCodeDescriptor Failed;
extern const ErrorCodeDescriptor InvalidHandle;
extern const ErrorCodeDescriptor NotFound;
extern const ErrorCodeDescriptor QueueFull;
extern const ErrorCodeDescriptor Shutdown;
} // namespace Horo::JobErrors

namespace Horo::Math::Errors
{
extern const ErrorCodeDescriptor InvalidAffineMatrix;
extern const ErrorCodeDescriptor InvalidBounds;
extern const ErrorCodeDescriptor InvalidHomogeneousPoint;
extern const ErrorCodeDescriptor InvalidPlane;
extern const ErrorCodeDescriptor InvalidProjection;
extern const ErrorCodeDescriptor InvalidRay;
extern const ErrorCodeDescriptor InvalidView;
extern const ErrorCodeDescriptor NonFiniteInput;
extern const ErrorCodeDescriptor SingularMatrix;
extern const ErrorCodeDescriptor ZeroLength;
} // namespace Horo::Math::Errors
