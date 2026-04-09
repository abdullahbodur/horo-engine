#include "editor/EditorSchema.h"

#include <fstream>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace Monolith {
namespace Editor {
namespace {

const char* TypeName(SceneObjectType t) {
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

FieldDef ParseFieldDef(const json& fd) {
  FieldDef field;
  field.key = fd.value("key", "");
  field.label = fd.value("label", field.key);
  field.description = fd.value("description", "");
  field.hasDefault = fd.contains("default");
  field.defaultValue = fd.value("default", "");
  field.required = fd.value("required", false);
  field.allowEmpty = fd.value("allowEmpty", true);
  field.allowCustomValue = fd.value("allowCustomValue", false);

  const std::string wtype = fd.value("type", "string");
  if (wtype == "float") {
    field.widget = FieldDef::Widget::Float;
    field.hasMin = fd.contains("min");
    field.hasMax = fd.contains("max");
    field.minVal = fd.value("min", 0.0f);
    field.maxVal = fd.value("max", 1.0f);
  } else if (wtype == "bool") {
    field.widget = FieldDef::Widget::Bool;
  } else if (wtype == "enum") {
    field.widget = FieldDef::Widget::Enum;
    if (fd.contains("options"))
      for (const auto& opt : fd["options"])
        field.options.push_back(opt.get<std::string>());
  } else if (wtype == "color3") {
    field.widget = FieldDef::Widget::Color3;
  } else {
    field.widget = FieldDef::Widget::String;
  }

  return field;
}

void ParseAppliesTo(const json& definition, std::vector<std::string>* outAppliesTo) {
  if (!outAppliesTo)
    return;
  outAppliesTo->clear();
  if (!definition.contains("appliesTo") || !definition["appliesTo"].is_array())
    return;
  for (const auto& item : definition["appliesTo"]) {
    if (item.is_string())
      outAppliesTo->push_back(item.get<std::string>());
  }
}

}  // namespace

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

  m_schemas.clear();
  m_componentSchemas.clear();

  if (j.contains("types") && j["types"].is_object()) {
    for (auto& [typeName, typeDef] : j["types"].items()) {
      if (!typeDef.contains("fields") || !typeDef["fields"].is_array())
        continue;

      TypeSchema schema;
      schema.name = typeName;
      schema.label = typeDef.value("label", typeName);
      ParseAppliesTo(typeDef, &schema.appliesTo);
      for (const auto& fd : typeDef["fields"]) {
        FieldDef field = ParseFieldDef(fd);
        if (field.key.empty())
          continue;

        // Legacy cleanup:
        // - Prop.isLight is deprecated (use Light object/component instead)
        // - Prop.mesh should always be an enum picker in editor UI
        if (typeName == "Prop" && field.key == "isLight")
          continue;

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

  if (j.contains("components") && j["components"].is_object()) {
    for (auto& [componentType, componentDef] : j["components"].items()) {
      if (!componentDef.contains("fields") || !componentDef["fields"].is_array())
        continue;

      ComponentSchema schema;
      schema.name = componentType;
      schema.label = componentDef.value("label", componentType);
      ParseAppliesTo(componentDef, &schema.appliesTo);
      for (const auto& fd : componentDef["fields"]) {
        FieldDef field = ParseFieldDef(fd);
        if (field.key.empty())
          continue;
        schema.fields.push_back(std::move(field));
      }
      if (!schema.fields.empty())
        m_componentSchemas[componentType] = std::move(schema);
    }
  }
}

const TypeSchema* EditorSchema::GetSchema(SceneObjectType t) const {
  auto it = m_schemas.find(TypeName(t));
  return (it != m_schemas.end()) ? &it->second : nullptr;
}

const TypeSchema* EditorSchema::GetSchemaByName(const std::string& typeName) const {
  const auto it = m_schemas.find(typeName);
  return (it != m_schemas.end()) ? &it->second : nullptr;
}

const ComponentSchema* EditorSchema::GetComponentSchema(const std::string& componentType) const {
  const auto it = m_componentSchemas.find(componentType);
  return (it != m_componentSchemas.end()) ? &it->second : nullptr;
}

}  // namespace Editor
}  // namespace Monolith
