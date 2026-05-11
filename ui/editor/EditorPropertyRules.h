/** @file EditorPropertyRules.h
 *  @brief Pure property mutation and object-creation rules extracted from EditorLayer.
 */
#pragma once

#include <string>

#include "ui/editor/EditorSchema.h"
#include "ui/editor/SceneDocument.h"

namespace Horo::Editor {

/** @brief Creates a new SceneObject of type Prop bound to the given asset.
 *
 *  Applies TypeSchema field defaults from schema and sets _assetRenderScale
 *  from the asset definition.  The returned object has a generated ID;
 *  callers are responsible for inserting it into the document.
 *
 *  @param doc     Scene document providing the asset map and schema context.
 *  @param assetId Logical identifier of the asset to bind to the new object.
 *  @param schema  Editor schema supplying type and component field defaults.
 *  @return A fully initialised SceneObject ready for insertion.
 */
SceneObject MakeObjectFromAsset(const SceneDocument &doc,
                                const std::string &assetId,
                                const EditorSchema &schema);

/** @brief Applies TypeSchema field defaults for obj.type to obj.props.
 *
 *  Only writes fields where hasDefault is true and the key is not already set.
 *
 *  @param obj    Scene object whose props are updated in place.
 *  @param schema Editor schema providing the TypeSchema for obj.type.
 */
void ApplySchemaFieldDefaults(SceneObject &obj, const EditorSchema &schema);

/** @brief Applies ComponentSchema field defaults to comp.props.
 *
 *  Only writes fields where hasDefault is true and the key is not already set.
 *
 *  @param comp   Component descriptor whose props are updated in place.
 *  @param schema Editor schema providing the ComponentSchema defaults.
 */
void ApplyComponentFieldDefaults(ComponentDesc &comp,
                                 const EditorSchema &schema);

/** @brief Populates the built-in Camera props when they are not already present.
 *
 *  Fills fov, nearClip, farClip, and followTargetId with sensible defaults if
 *  those keys are absent from obj.props.
 *
 *  @param obj Camera scene object whose props are updated in place.
 */
void ApplyCameraBuiltinDefaults(SceneObject &obj);

} // namespace Horo::Editor
