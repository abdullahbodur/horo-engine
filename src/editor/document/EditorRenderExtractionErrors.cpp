#include "EditorRenderExtractionErrors.h"

namespace Horo::Editor
{
    namespace
    {
        const ErrorDomainId ViewportSceneDomain{"horo.editor.viewport_scene_extractor"};
        const ErrorDomainId ViewportPickingDomain{"horo.editor.viewport_picking"};
    } // namespace

    namespace ViewportSceneErrors
    {
        const ErrorCodeDescriptor HierarchyCycle{
            .domain = ViewportSceneDomain,
            .code = ErrorCode{"viewport_scene.hierarchy_cycle"},
            .defaultSeverity = ErrorSeverity::Error,
            .summary = "Scene hierarchy contains a cycle.",
            .remediationHint = "Remove the cyclic parent relationship.",
            .retryable = false,
            .userActionable = true
        };

        const ErrorCodeDescriptor InstanceIdentityMismatch{
            .domain = ViewportSceneDomain,
            .code = ErrorCode{"viewport_scene.instance_identity_mismatch"},
            .defaultSeverity = ErrorSeverity::Error,
            .summary = "Viewport instance identities are misaligned.",
            .remediationHint = "Rebuild the viewport scene snapshot from the active document.",
            .retryable = false,
            .userActionable = false
        };

        const ErrorCodeDescriptor InvalidCamera{
            .domain = ViewportSceneDomain,
            .code = ErrorCode{"viewport_scene.invalid_camera"},
            .defaultSeverity = ErrorSeverity::Error,
            .summary = "Viewport scene camera is invalid.",
            .remediationHint = "Use a valid editor viewport camera.",
            .retryable = false,
            .userActionable = true
        };

        const ErrorCodeDescriptor InvalidObjectId{
            .domain = ViewportSceneDomain,
            .code = ErrorCode{"viewport_scene.invalid_object_id"},
            .defaultSeverity = ErrorSeverity::Error,
            .summary = "Viewport scene contains an invalid object ID.",
            .remediationHint = "Rebuild the scene snapshot with valid unique object IDs.",
            .retryable = false,
            .userActionable = false
        };

        const ErrorCodeDescriptor InvalidResult{
            .domain = ViewportSceneDomain,
            .code = ErrorCode{"viewport_scene.invalid_result"},
            .defaultSeverity = ErrorSeverity::Error,
            .summary = "Viewport scene extraction produced invalid data.",
            .remediationHint = "Inspect scene transforms and extracted resources.",
            .retryable = false,
            .userActionable = false
        };

        const ErrorCodeDescriptor ObjectNotFound{
            .domain = ViewportSceneDomain,
            .code = ErrorCode{"viewport_scene.object_not_found"},
            .defaultSeverity = ErrorSeverity::Error,
            .summary = "Viewport scene object was not found.",
            .remediationHint = "Refresh the snapshot from the active document.",
            .retryable = false,
            .userActionable = true
        };

        const ErrorCodeDescriptor ParentNotFound{
            .domain = ViewportSceneDomain,
            .code = ErrorCode{"viewport_scene.parent_not_found"},
            .defaultSeverity = ErrorSeverity::Error,
            .summary = "Viewport scene parent object was not found.",
            .remediationHint = "Repair the scene hierarchy before extraction.",
            .retryable = false,
            .userActionable = true
        };
    } // namespace ViewportSceneErrors

    namespace ViewportPickingErrors
    {
        const ErrorCodeDescriptor InvalidIdentity{
            .domain = ViewportPickingDomain,
            .code = ErrorCode{"viewport_picking.invalid_identity"},
            .defaultSeverity = ErrorSeverity::Error,
            .summary = "Viewport picking identity data is invalid.",
            .remediationHint = "Rebuild the picking snapshot from valid scene identities.",
            .retryable = false,
            .userActionable = false
        };

        const ErrorCodeDescriptor InvalidQuery{
            .domain = ViewportPickingDomain,
            .code = ErrorCode{"viewport_picking.invalid_query"},
            .defaultSeverity = ErrorSeverity::Error,
            .summary = "Viewport picking query is invalid.",
            .remediationHint = "Use normalized coordinates and a positive finite aspect ratio.",
            .retryable = false,
            .userActionable = true
        };

        const ErrorCodeDescriptor InvalidScene{
            .domain = ViewportPickingDomain,
            .code = ErrorCode{"viewport_picking.invalid_scene"},
            .defaultSeverity = ErrorSeverity::Error,
            .summary = "Viewport picking scene is invalid.",
            .remediationHint = "Rebuild the viewport scene snapshot before picking.",
            .retryable = false,
            .userActionable = false
        };
    } // namespace ViewportPickingErrors
} // namespace Horo::Editor
