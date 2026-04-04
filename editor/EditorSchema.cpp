#include "editor/EditorSchema.h"

#include <fstream>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace Monolith {
namespace Editor {

static const char* TypeName(SceneObjectType t) {
  switch (t) {
    case SceneObjectType::Prop:
      return "Prop";
    case SceneObjectType::Light:
      return "Light";
    case SceneObjectType::Camera:
      return "Camera";
    default:
      return "Panel";
  }
}

void EditorSchema::LoadFromFile(const std::string& path) {
  std::ifstream f(path);
  if (!f.is_open())
    return;  // non-fatal

  json j;
  try {
    f >> j;
  } catch (...) {
    return;
  }

  if (!j.contains("types"))
    return;

  for (auto& [typeName, typeDef] : j["types"].items()) {
    if (!typeDef.contains("fields"))
      continue;

    TypeSchema schema;
    for (auto& fd : typeDef["fields"]) {
      FieldDef field;
      field.key = fd.value("key", "");
      field.label = fd.value("label", "");
      field.defaultValue = fd.value("default", "");

      // Legacy cleanup:
      // - Prop.isLight is deprecated (use Light object/component instead)
      // - Prop.mesh should always be an enum picker in editor UI
      if (typeName == "Prop" && field.key == "isLight")
        continue;

      std::string wtype = fd.value("type", "string");
      if (wtype == "float") {
        field.widget = FieldDef::Widget::Float;
        field.minVal = fd.value("min", 0.0f);
        field.maxVal = fd.value("max", 1.0f);
      } else if (wtype == "bool") {
        field.widget = FieldDef::Widget::Bool;
      } else if (wtype == "enum") {
        field.widget = FieldDef::Widget::Enum;
        if (fd.contains("options"))
          for (auto& opt : fd["options"])
            field.options.push_back(opt.get<std::string>());
      } else if (wtype == "color3") {
        field.widget = FieldDef::Widget::Color3;
      } else {
        field.widget = FieldDef::Widget::String;
      }

      if (typeName == "Prop" && field.key == "mesh") {
        field.widget = FieldDef::Widget::Enum;
        if (field.options.empty()) {
          field.options = {"box", "sphere", "cylinder", "pyramid", "plane", "quad"};
        }
        if (field.defaultValue.empty())
          field.defaultValue = "box";
      }

      schema.fields.push_back(std::move(field));
    }
    m_schemas[typeName] = std::move(schema);
  }
}

const TypeSchema* EditorSchema::GetSchema(SceneObjectType t) const {
  auto it = m_schemas.find(TypeName(t));
  return (it != m_schemas.end()) ? &it->second : nullptr;
}

}  // namespace Editor
}  // namespace Monolith
