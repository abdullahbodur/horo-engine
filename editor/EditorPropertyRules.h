#pragma once
// Pure property mutation and object-creation rules extracted from EditorLayer.
// No ImGui or GL dependencies; fully unit-testable.

#include <string>

#include "editor/EditorSchema.h"
#include "editor/SceneDocument.h"

namespace Horo::Editor {

// Creates a new SceneObject of type Prop bound to assetId.
// Applies TypeSchema field defaults from schema and sets _assetRenderScale from
// the asset definition.  The returned object has a generated id — callers are
// responsible for inserting it into the document.
SceneObject MakeObjectFromAsset(const SceneDocument &doc,
                                const std::string &assetId,
                                const EditorSchema &schema);

// Applies TypeSchema field defaults for obj.type to obj.props.
// Only writes fields where hasDefault is true and the key is not already set.
void ApplySchemaFieldDefaults(SceneObject &obj, const EditorSchema &schema);

// Applies ComponentSchema field defaults to comp.props.
// Only writes fields where hasDefault is true and the key is not already set.
void ApplyComponentFieldDefaults(ComponentDesc &comp,
                                 const EditorSchema &schema);

// Populates the built-in Camera props (fov, nearClip, farClip,
// followTargetId) when they are not already present.
void ApplyCameraBuiltinDefaults(SceneObject &obj);

} // namespace Horo::Editor
