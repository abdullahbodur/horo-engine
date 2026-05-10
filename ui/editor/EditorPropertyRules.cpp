/**
 * @file EditorPropertyRules.cpp
 * @brief Schema-driven defaults and Prop/Camera construction helpers shared by the editor.
 */
#include "ui/editor/EditorPropertyRules.h"

#include "ui/editor/EditorSchema.h"
#include "ui/editor/EditorSelectionRules.h"
#include "ui/editor/SceneDocument.h"

namespace Horo::Editor {

/** @copydoc MakeObjectFromAsset */
SceneObject MakeObjectFromAsset(const SceneDocument &doc,
                                const std::string &assetId,
                                const EditorSchema &schema) {
  using enum SceneObjectType;

  SceneObject obj;
  obj.id = GenerateUniqueId(doc, "obj");
  obj.type = Prop;
  obj.assetId = assetId;

  if (const auto assetIt = doc.assets.find(assetId);
      assetIt != doc.assets.end()) {
    obj.props["_assetRenderScale"] = assetIt->second.renderScale.empty()
                                         ? "1.0000,1.0000,1.0000"
                                         : assetIt->second.renderScale;
  }

  if (const TypeSchema *typeSchema = schema.GetSchema(obj.type); typeSchema) {
    for (const auto &fd : typeSchema->fields)
      obj.props[fd.key] = fd.defaultValue;
  }

  return obj;
}

/** @copydoc ApplySchemaFieldDefaults */
void ApplySchemaFieldDefaults(SceneObject &obj, const EditorSchema &schema) {
  const TypeSchema *typeSchema = schema.GetSchema(obj.type);
  if (!typeSchema)
    return;
  for (const auto &fd : typeSchema->fields) {
    if (fd.hasDefault && !obj.props.contains(fd.key))
      obj.props[fd.key] = fd.defaultValue;
  }
}

/** @copydoc ApplyComponentFieldDefaults */
void ApplyComponentFieldDefaults(ComponentDesc &comp,
                                 const EditorSchema &schema) {
  const ComponentSchema *compSchema = schema.GetComponentSchema(comp.type);
  if (!compSchema)
    return;
  for (const FieldDef &field : compSchema->fields) {
    if (field.hasDefault && !comp.props.contains(field.key))
      comp.props[field.key] = field.defaultValue;
  }
}

/** @copydoc ApplyCameraBuiltinDefaults */
void ApplyCameraBuiltinDefaults(SceneObject &obj) {
  if (!obj.props.contains("fov"))
    obj.props["fov"] = "60";
  if (!obj.props.contains("nearClip"))
    obj.props["nearClip"] = "0.1";
  if (!obj.props.contains("farClip"))
    obj.props["farClip"] = "500";
  if (!obj.props.contains("followTargetId"))
    obj.props["followTargetId"] = "";
}

} // namespace Horo::Editor
