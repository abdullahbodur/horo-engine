#pragma once

#include "Horo/Foundation/ErrorCode.h"

namespace Horo::Runtime::SceneErrors
{
extern const ErrorCodeDescriptor InvalidDefinition;
extern const ErrorCodeDescriptor InvalidAssetDependency;
extern const ErrorCodeDescriptor ConflictingAssetDependency;
extern const ErrorCodeDescriptor AssetServicesUnavailable;
extern const ErrorCodeDescriptor AssetMissing;
extern const ErrorCodeDescriptor AssetTypeMismatch;
extern const ErrorCodeDescriptor AssetPayloadEmpty;
extern const ErrorCodeDescriptor AssetBudgetExceeded;
extern const ErrorCodeDescriptor AssetRevisionStale;
extern const ErrorCodeDescriptor AssetLimitsInvalid;
extern const ErrorCodeDescriptor ServiceShutdown;
extern const ErrorCodeDescriptor InvalidEntity;
extern const ErrorCodeDescriptor DuplicateObject;
extern const ErrorCodeDescriptor ParentNotFound;
extern const ErrorCodeDescriptor HierarchyCycle;
extern const ErrorCodeDescriptor StaleEntity;
extern const ErrorCodeDescriptor StaleView;
extern const ErrorCodeDescriptor OperationInProgress;
extern const ErrorCodeDescriptor NoActiveScene;
extern const ErrorCodeDescriptor InvalidCandidate;
extern const ErrorCodeDescriptor StructuralCommitFailed;
} // namespace Horo::Runtime::SceneErrors
