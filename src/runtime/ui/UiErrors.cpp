#include "Horo/Runtime/Ui/UiErrors.h"

namespace Horo::Runtime::Ui::UiErrors {
    namespace {
        const ErrorDomainId UiDomain{"horo.runtime_ui"};
    }

    /** @copydoc IdentityInvalid */
    const ErrorCodeDescriptor IdentityInvalid{UiDomain,
                                              ErrorCode{"runtime_ui.identity.invalid"},
                                              ErrorSeverity::Error,
                                              "The stable Runtime UI identity is invalid.",
                                              "Provide a non-zero 128-bit identity owned by the authoring document.",
                                              false,
                                              true};
    /** @copydoc OwnershipGenerationInvalid */
    const ErrorCodeDescriptor OwnershipGenerationInvalid{UiDomain,
                                                         ErrorCode{"runtime_ui.ownership_generation.invalid"},
                                                         ErrorSeverity::Error,
                                                         "The Runtime UI ownership generation is invalid.",
                                                         "Use the non-zero generation issued by the active RuntimeUiService owner.",
                                                         false,
                                                         false};
    /** @copydoc HandleMalformed */
    const ErrorCodeDescriptor HandleMalformed{UiDomain,
                                              ErrorCode{"runtime_ui.handle.malformed"},
                                              ErrorSeverity::Error,
                                              "The Runtime UI handle is malformed.",
                                              "Use a handle issued by the owning registry with non-zero owner, slot, and slot generation.",
                                              false,
                                              false};
    /** @copydoc HandleOwnerMismatch */
    const ErrorCodeDescriptor HandleOwnerMismatch{UiDomain,
                                                  ErrorCode{"runtime_ui.handle.owner_mismatch"},
                                                  ErrorSeverity::Error,
                                                  "The Runtime UI handle belongs to another owner generation.",
                                                  "Resolve the stable identity against the active owner scope again.",
                                                  false,
                                                  false};
    /** @copydoc HandleStale */
    const ErrorCodeDescriptor HandleStale{UiDomain,
                                          ErrorCode{"runtime_ui.handle.stale"},
                                          ErrorSeverity::Error,
                                          "The Runtime UI handle slot is absent or retired.",
                                          "Discard the transient handle and resolve the stable identity against the current tree.",
                                          false,
                                          false};
    /** @copydoc RevisionInvalid */
    const ErrorCodeDescriptor RevisionInvalid{UiDomain,
                                              ErrorCode{"runtime_ui.revision.invalid"},
                                              ErrorSeverity::Error,
                                              "The Runtime UI revision is invalid.",
                                              "Use a non-zero revision published by the owning Runtime UI object.",
                                              false,
                                              false};
    /** @copydoc RevisionStale */
    const ErrorCodeDescriptor RevisionStale{UiDomain,
                                            ErrorCode{"runtime_ui.revision.stale"},
                                            ErrorSeverity::Error,
                                            "The expected Runtime UI revision is stale.",
                                            "Reload the current owner-published revision and prepare the command again.",
                                            true,
                                            false};
    /** @copydoc GenerationExhausted */
    const ErrorCodeDescriptor GenerationExhausted{UiDomain,
                                                  ErrorCode{"runtime_ui.generation.exhausted"},
                                                  ErrorSeverity::Critical,
                                                  "The Runtime UI generation range is exhausted.",
                                                  "Close admission and replace the owning game runtime; never wrap the identity.",
                                                  false,
                                                  false};
    /** @copydoc DocumentInvalid */
    const ErrorCodeDescriptor DocumentInvalid{UiDomain,
                                              ErrorCode{"runtime_ui.document.invalid"},
                                              ErrorSeverity::Error,
                                              "The Runtime UI document is invalid.",
                                              "Provide valid document, revision, canvas, and root identities.",
                                              false,
                                              true};
    /** @copydoc DocumentDuplicateIdentity */
    const ErrorCodeDescriptor DocumentDuplicateIdentity{UiDomain,
                                                        ErrorCode{"runtime_ui.document.duplicate_identity"},
                                                        ErrorSeverity::Error,
                                                        "The Runtime UI document repeats a stable identity.",
                                                        "Assign unique canvas and root element identities.",
                                                        false,
                                                        true};
    /** @copydoc DependencyInvalid */
    const ErrorCodeDescriptor DependencyInvalid{UiDomain,
                                                ErrorCode{"runtime_ui.dependency.invalid"},
                                                ErrorSeverity::Error,
                                                "The Runtime UI asset dependency is invalid or conflicting.",
                                                "Provide one valid expected asset type for each stable asset identity.",
                                                false,
                                                true};
    /** @copydoc CapacityExceeded */
    const ErrorCodeDescriptor CapacityExceeded{UiDomain,
                                               ErrorCode{"runtime_ui.capacity.exceeded"},
                                               ErrorSeverity::Error,
                                               "A bounded Runtime UI document limit was exceeded.",
                                               "Reduce canvas, dependency, or cooked payload size.",
                                               false,
                                               true};
    /** @copydoc PayloadInvalid */
    const ErrorCodeDescriptor PayloadInvalid{UiDomain,
                                             ErrorCode{"runtime_ui.payload.invalid"},
                                             ErrorSeverity::Error,
                                             "The cooked Runtime UI payload is invalid.",
                                             "Rebuild the asset with non-empty deterministic bytes within the declared bound.",
                                             false,
                                             true};
    /** @copydoc CanvasReferenceInvalid */
    const ErrorCodeDescriptor CanvasReferenceInvalid{UiDomain,
                                                     ErrorCode{"runtime_ui.canvas_reference.invalid"},
                                                     ErrorSeverity::Error,
                                                     "The Runtime UI canvas asset reference is invalid.",
                                                     "Provide complete asset, document, canvas, and minimum revision evidence.",
                                                     false,
                                                     true};
    /** @copydoc InstanceStateInvalid */
    const ErrorCodeDescriptor InstanceStateInvalid{UiDomain,
                                                   ErrorCode{"runtime_ui.instance.state_invalid"},
                                                   ErrorSeverity::Error,
                                                   "The Runtime UI instance cannot perform this lifecycle transition.",
                                                   "Submit the transition only from its declared owner-thread lifecycle state.",
                                                   false,
                                                   false};
}  // namespace Horo::Runtime::Ui::UiErrors
