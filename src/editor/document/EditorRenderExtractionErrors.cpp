#include "EditorRenderExtractionErrors.h"

namespace Horo::Editor
{
namespace
{
const ErrorDomainId ViewportSceneDomain{"horo.editor.viewport_scene_extractor"};
const ErrorDomainId ViewportPickingDomain{"horo.editor.viewport_picking"};

[[nodiscard]] ErrorCodeDescriptor Describe(const ErrorDomainId &domain, const char *code, const char *summary,
                                           const char *remediation, const bool userActionable = false)
{
    return {.domain = domain,
            .code = ErrorCode{code},
            .defaultSeverity = ErrorSeverity::Error,
            .summary = summary,
            .remediationHint = remediation,
            .retryable = false,
            .userActionable = userActionable};
}
} // namespace

namespace ViewportSceneErrors
{
const ErrorCodeDescriptor HierarchyCycle = Describe(ViewportSceneDomain, "viewport_scene.hierarchy_cycle",
                                                    "Scene hierarchy contains a cycle.",
                                                    "Remove the cyclic parent relationship.", true);
const ErrorCodeDescriptor InstanceIdentityMismatch = Describe(
    ViewportSceneDomain, "viewport_scene.instance_identity_mismatch", "Viewport instance identities are misaligned.",
    "Rebuild the viewport scene snapshot from the active document.");
const ErrorCodeDescriptor InvalidCamera = Describe(ViewportSceneDomain, "viewport_scene.invalid_camera",
                                                   "Viewport scene camera is invalid.",
                                                   "Use a valid editor viewport camera.", true);
const ErrorCodeDescriptor InvalidObjectId = Describe(ViewportSceneDomain, "viewport_scene.invalid_object_id",
                                                     "Viewport scene contains an invalid object ID.",
                                                     "Rebuild the scene snapshot with valid unique object IDs.");
const ErrorCodeDescriptor InvalidResult = Describe(ViewportSceneDomain, "viewport_scene.invalid_result",
                                                   "Viewport scene extraction produced invalid data.",
                                                   "Inspect scene transforms and extracted resources.");
const ErrorCodeDescriptor ObjectNotFound = Describe(ViewportSceneDomain, "viewport_scene.object_not_found",
                                                    "Viewport scene object was not found.",
                                                    "Refresh the snapshot from the active document.", true);
const ErrorCodeDescriptor ParentNotFound = Describe(ViewportSceneDomain, "viewport_scene.parent_not_found",
                                                    "Viewport scene parent object was not found.",
                                                    "Repair the scene hierarchy before extraction.", true);
} // namespace ViewportSceneErrors

namespace ViewportPickingErrors
{
const ErrorCodeDescriptor InvalidIdentity = Describe(ViewportPickingDomain, "viewport_picking.invalid_identity",
                                                     "Viewport picking identity data is invalid.",
                                                     "Rebuild the picking snapshot from valid scene identities.");
const ErrorCodeDescriptor InvalidQuery = Describe(ViewportPickingDomain, "viewport_picking.invalid_query",
                                                  "Viewport picking query is invalid.",
                                                  "Use normalized coordinates and a positive finite aspect ratio.", true);
const ErrorCodeDescriptor InvalidScene = Describe(ViewportPickingDomain, "viewport_picking.invalid_scene",
                                                  "Viewport picking scene is invalid.",
                                                  "Rebuild the viewport scene snapshot before picking.");
} // namespace ViewportPickingErrors
} // namespace Horo::Editor
