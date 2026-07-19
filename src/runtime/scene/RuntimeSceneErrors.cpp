#include "RuntimeSceneErrors.h"

namespace Horo::Runtime::SceneErrors
{
namespace
{
const ErrorDomainId kDomain{"horo.scene"};
constexpr auto kError = ErrorSeverity::Error;
} // namespace

const ErrorCodeDescriptor InvalidDefinition{kDomain, ErrorCode{"scene.definition.invalid"}, kError,
                                            "The runtime scene definition is invalid.",
                                            "Correct the typed authoring data before activation."};
const ErrorCodeDescriptor InvalidAssetDependency{kDomain, ErrorCode{"scene.asset.dependency_invalid"}, kError,
                                                 "A runtime scene asset dependency is invalid.",
                                                 "Use a non-zero AssetId and canonical AssetTypeId."};
const ErrorCodeDescriptor ConflictingAssetDependency{kDomain, ErrorCode{"scene.asset.dependency_conflict"}, kError,
                                                     "One asset is required with conflicting types.",
                                                     "Keep one expected type for each AssetId."};
const ErrorCodeDescriptor AssetServicesUnavailable{kDomain, ErrorCode{"scene.asset.services_unavailable"}, kError,
                                                   "Asset-bearing scene preparation has no asset services.",
                                                   "Inject the registry and load service at composition."};
const ErrorCodeDescriptor AssetMissing{kDomain, ErrorCode{"scene.asset.missing"}, kError,
                                       "A required asset is absent from the captured registry.",
                                       "Register and cook every required asset."};
const ErrorCodeDescriptor AssetTypeMismatch{kDomain, ErrorCode{"scene.asset.type_mismatch"}, kError,
                                            "A required asset has an unexpected type.",
                                            "Correct the scene dependency or registry metadata."};
const ErrorCodeDescriptor AssetPayloadEmpty{kDomain, ErrorCode{"scene.asset.payload_empty"}, kError,
                                            "A required cooked asset has an empty payload.",
                                            "Rebuild the cooked artifact."};
const ErrorCodeDescriptor AssetBudgetExceeded{kDomain, ErrorCode{"scene.asset.budget_exceeded"}, kError,
                                              "The scene asset budget was exceeded.",
                                              "Reduce dependencies or raise the explicit scene limit."};
const ErrorCodeDescriptor AssetRevisionStale{kDomain, ErrorCode{"scene.asset.registry_stale"}, kError,
                                             "The asset registry changed during scene preparation.",
                                             "Retry against the authoritative registry snapshot."};
const ErrorCodeDescriptor AssetLimitsInvalid{kDomain, ErrorCode{"scene.asset.limits_invalid"}, kError,
                                             "Runtime scene asset limits are invalid.",
                                             "Use non-zero bounded dependency, concurrency, and byte limits."};
const ErrorCodeDescriptor ServiceShutdown{kDomain, ErrorCode{"scene.service.shutdown"}, kError,
                                          "The runtime scene service is shut down.",
                                          "Start the service before submitting scene work."};
const ErrorCodeDescriptor InvalidEntity{kDomain, ErrorCode{"scene.entity.invalid"}, kError,
                                        "The runtime entity payload is invalid.",
                                        "Supply finite typed component and transform values."};
const ErrorCodeDescriptor DuplicateObject{kDomain, ErrorCode{"scene.object.duplicate"}, kError,
                                          "A stable scene object identity is duplicated.",
                                          "Use one non-zero identity per authored object."};
const ErrorCodeDescriptor ParentNotFound{kDomain, ErrorCode{"scene.hierarchy.parent_not_found"}, kError,
                                         "A scene entity references a missing parent.",
                                         "Reference an object in the same definition."};
const ErrorCodeDescriptor HierarchyCycle{kDomain, ErrorCode{"scene.hierarchy.cycle"}, kError,
                                         "The scene hierarchy contains a cycle.",
                                         "Remove cyclic parent relationships."};
const ErrorCodeDescriptor StaleEntity{kDomain, ErrorCode{"scene.entity.stale"}, kError,
                                      "The entity reference is stale or belongs to another runtime.",
                                      "Resolve the current entity reference."};
const ErrorCodeDescriptor StaleView{kDomain, ErrorCode{"scene.view.stale"}, kError,
                                    "The borrowed scene view was invalidated by a structural commit.",
                                    "Acquire a new view from the current runtime scene."};
const ErrorCodeDescriptor OperationInProgress{kDomain, ErrorCode{"scene.operation.in_progress"}, kError,
                                              "Another scene transition or structural operation is pending.",
                                              "Retry after the lifecycle commit boundary."};
const ErrorCodeDescriptor NoActiveScene{kDomain, ErrorCode{"scene.runtime.no_active_scene"}, kError,
                                        "No active runtime scene exists.",
                                        "Activate a prepared scene before queuing structural changes."};
const ErrorCodeDescriptor InvalidCandidate{kDomain, ErrorCode{"scene.runtime.invalid_candidate"}, kError,
                                           "The prepared runtime scene candidate is invalid.",
                                           "Pass a non-null candidate from Prepare."};
const ErrorCodeDescriptor StructuralCommitFailed{kDomain, ErrorCode{"scene.structural.commit_failed"}, kError,
                                                 "The structural command batch could not be committed.",
                                                 "Correct the command batch and retry."};
} // namespace Horo::Runtime::SceneErrors
