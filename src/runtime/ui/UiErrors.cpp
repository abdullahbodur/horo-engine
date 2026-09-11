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
    /** @copydoc CanvasSpaceInvalid */
    const ErrorCodeDescriptor
        CanvasSpaceInvalid{UiDomain,
                           ErrorCode{"runtime_ui.canvas_space.invalid"},
                           ErrorSeverity::Error,
                           "The Runtime UI canvas-space input is invalid.",
                           "Provide known modes, bounded reference dimensions, a non-zero viewport, and valid caller-owned scale evidence.",
                           false,
                           true};
    /** @copydoc CanvasSpaceModeMismatch */
    const ErrorCodeDescriptor
        CanvasSpaceModeMismatch{UiDomain,
                                ErrorCode{"runtime_ui.canvas_space.mode_mismatch"},
                                ErrorSeverity::Error,
                                "The Runtime UI canvas resolver does not match the canvas projection mode.",
                                "Use screen resolution for overlay/camera canvases and logical world resolution for world-space canvases.",
                                false,
                                true};
    /** @copydoc CanvasSpaceOverflow */
    const ErrorCodeDescriptor CanvasSpaceOverflow{UiDomain,
                                                  ErrorCode{"runtime_ui.canvas_space.overflow"},
                                                  ErrorSeverity::Error,
                                                  "The resolved Runtime UI canvas extent exceeds the logical geometry domain.",
                                                  "Reduce the viewport extent or provide a larger valid pixels-per-DIP scale.",
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
    /** @copydoc ElementTreeInvalid */
    const ErrorCodeDescriptor ElementTreeInvalid{UiDomain,
                                                 ErrorCode{"runtime_ui.element_tree.invalid"},
                                                 ErrorSeverity::Error,
                                                 "The retained Runtime UI element tree is invalid.",
                                                 "Provide one connected acyclic root tree within the declared depth and element bounds.",
                                                 false,
                                                 true};
    /** @copydoc ElementTreeIdentityConflict */
    const ErrorCodeDescriptor ElementTreeIdentityConflict{UiDomain,
                                                          ErrorCode{"runtime_ui.element_tree.identity_conflict"},
                                                          ErrorSeverity::Error,
                                                          "The retained Runtime UI element tree repeats a stable identity.",
                                                          "Assign one unique authored identity to every element in the tree.",
                                                          false,
                                                          true};
    /** @copydoc StructuralCommandInvalid */
    const ErrorCodeDescriptor
        StructuralCommandInvalid{UiDomain,
                                 ErrorCode{"runtime_ui.structural_command.invalid"},
                                 ErrorSeverity::Error,
                                 "The Runtime UI structural command is invalid.",
                                 "Use current handles, an owner safe point, and a child position valid after detachment.",
                                 false,
                                 false};
    /** @copydoc StructuralCommandConflict */
    const ErrorCodeDescriptor
        StructuralCommandConflict{UiDomain,
                                  ErrorCode{"runtime_ui.structural_command.conflict"},
                                  ErrorSeverity::Error,
                                  "The Runtime UI structural command conflicts with retained-tree invariants.",
                                  "Keep the root fixed and avoid self-parenting, descendant parenting, and competing topology changes.",
                                  false,
                                  false};
    /** @copydoc ElementTreeLifecycleUnavailable */
    const ErrorCodeDescriptor
        ElementTreeLifecycleUnavailable{UiDomain,
                                        ErrorCode{"runtime_ui.element_tree.lifecycle_unavailable"},
                                        ErrorSeverity::Error,
                                        "The retained Runtime UI element tree is unavailable in its current lifecycle state.",
                                        "Stop structural admission before retirement and query contents only until bounded shutdown "
                                        "completes.",
                                        false,
                                        false};
    /** @copydoc RenderSnapshotInvalid */
    const ErrorCodeDescriptor RenderSnapshotInvalid{UiDomain,
                                                    ErrorCode{"runtime_ui.render_snapshot.invalid"},
                                                    ErrorSeverity::Error,
                                                    "The immutable Runtime UI render snapshot is invalid.",
                                                    "Provide exact owner revisions and complete bounded logical projection tables.",
                                                    false,
                                                    false};
    /** @copydoc RenderCommandInvalid */
    const ErrorCodeDescriptor RenderCommandInvalid{UiDomain,
                                                   ErrorCode{"runtime_ui.render_command.invalid"},
                                                   ErrorSeverity::Error,
                                                   "A Runtime UI render command is invalid.",
                                                   "Use resident element handles, finite paint, and valid logical table references.",
                                                   false,
                                                   false};
    /** @copydoc RenderResourceReferenceInvalid */
    const ErrorCodeDescriptor
        RenderResourceReferenceInvalid{UiDomain,
                                       ErrorCode{"runtime_ui.render_resource_reference.invalid"},
                                       ErrorSeverity::Error,
                                       "A Runtime UI render resource reference is invalid.",
                                       "Provide a stable Horo asset with the exact nonzero revision and semantic role.",
                                       false,
                                       true};
    /** @copydoc RenderSnapshotStorageExhausted */
    const ErrorCodeDescriptor
        RenderSnapshotStorageExhausted{UiDomain,
                                       ErrorCode{"runtime_ui.render_snapshot.storage_exhausted"},
                                       ErrorSeverity::Error,
                                       "Every bounded Runtime UI render snapshot slot is still leased.",
                                       "Retire an in-flight snapshot before retrying; never overwrite or allocate fallback storage.",
                                       true,
                                       false};
    /** @copydoc RenderSnapshotLifecycleUnavailable */
    const ErrorCodeDescriptor
        RenderSnapshotLifecycleUnavailable{UiDomain,
                                           ErrorCode{"runtime_ui.render_snapshot.lifecycle_unavailable"},
                                           ErrorSeverity::Error,
                                           "The Runtime UI render extractor is closed.",
                                           "Create a new extractor for the active view generation before publishing another snapshot.",
                                           false,
                                           false};
    /** @copydoc DiagnosticInvalid */
    const ErrorCodeDescriptor
        DiagnosticInvalid{UiDomain,
                          ErrorCode{"runtime_ui.diagnostic.invalid"},
                          ErrorSeverity::Error,
                          "The Runtime UI diagnostic evidence is invalid.",
                          "Provide a known category, canonical Runtime UI error and bounded ordered correlation fields.",
                          false,
                          false};
    /** @copydoc DiagnosticUnsupported */
    const ErrorCodeDescriptor DiagnosticUnsupported{UiDomain,
                                                    ErrorCode{"runtime_ui.diagnostic.unsupported"},
                                                    ErrorSeverity::Error,
                                                    "The Runtime UI diagnostic source is unsupported.",
                                                    "Use a declared Runtime UI category and canonical horo.runtime_ui error descriptor.",
                                                    false,
                                                    false};
}  // namespace Horo::Runtime::Ui::UiErrors
