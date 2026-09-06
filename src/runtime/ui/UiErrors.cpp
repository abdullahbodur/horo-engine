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
}  // namespace Horo::Runtime::Ui::UiErrors
