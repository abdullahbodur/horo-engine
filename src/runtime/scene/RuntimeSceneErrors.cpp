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
